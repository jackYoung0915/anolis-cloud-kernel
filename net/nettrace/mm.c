// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nettrace support.
 *
 * Copyright (C) 2024 ZTE Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include "group.h"
#include "help.h"
#include "mm.h"

#define MIN_CACHED_SKB_OBJS 100

struct sk_buff_nettrace *get_cached_skb(TRACE_POINT *tp)
{
	if (!tp->nr_skb_objs)
		return NULL;

	WRITE_ONCE(tp->nr_skb_objs, tp->nr_skb_objs - 1);
	return (struct sk_buff_nettrace *)llist_del_first(&tp->skbcache);
}

bool put_cached_skb(TRACE_POINT *tp, struct sk_buff_nettrace *skb)
{
	if (tp->nr_skb_objs >= param_mm)
		return false;

	llist_add((struct llist_node *) skb, &tp->skbcache);
	WRITE_ONCE(tp->nr_skb_objs, tp->nr_skb_objs + 1);
	return true;
}

u8 *get_cached_data(TRACE_POINT *tp)
{
	if (!tp->nr_data_objs)
		return NULL;

	WRITE_ONCE(tp->nr_data_objs, tp->nr_data_objs - 1);
	return (u8 *)llist_del_first(&tp->datacache);
}

bool put_cached_data(TRACE_POINT *tp, u8 *data)
{
	if (tp->nr_data_objs >= param_mm)
		return false;

	llist_add((struct llist_node *) data, &tp->datacache);
	WRITE_ONCE(tp->nr_data_objs, tp->nr_data_objs + 1);
	return true;
}

void init_dump_mm(TRACE_POINT *tp)
{
	int i;
	struct sk_buff_nettrace *skb;
	u8 *data;

	if (param_mm < MIN_CACHED_SKB_OBJS)
		param_mm = MIN_CACHED_SKB_OBJS;

	for (i = 0; i < param_mm; i++) {
		skb = (struct sk_buff_nettrace *)
			__get_free_page(GFP_NOWAIT | __GFP_NOWARN);

		if (skb)
			put_cached_skb(tp, skb);
		else
			pr_err("Failed to preallocate for nettrace dump skb! number:%d\n", i);
	}

	for (i = 0; i < param_mm; i++) {
		data = (u8 *)
			__get_free_page(GFP_NOWAIT | __GFP_NOWARN);

		if (data)
			put_cached_data(tp, data);
		else
			pr_err("Failed to preallocate for nettrace dump data! number:%d\n", i);
	}

	/* Partial failure does not affect usage. */
}

void release_dump_mm(TRACE_POINT *tp)
{
	int i;
	struct sk_buff_nettrace *skb;
	u8 *data;

	for (i = 0; i < param_mm; i++) {
		skb = get_cached_skb(tp);
		if (skb)
			free_page((unsigned long)skb);
	}

	for (i = 0; i < param_mm; i++) {
		data = get_cached_data(tp);
		free_page((unsigned long)data);
	}
}

static struct sk_buff_nettrace *__alloc_skb_nettrace(TRACE_POINT *tp)
{
	struct sk_buff_nettrace *skb;
	u8 *data;

	skb = get_cached_skb(tp);
	if (!skb)
		goto out;
	prefetchw(skb);
	memset(skb, 0, sizeof(struct sk_buff_nettrace));

	data = get_cached_data(tp);
	if (!data)
		goto nodata;

	skb->data = data;
	memset(data, 0, PAGE_SIZE);

	skb_queue_head_init(&skb->frag_list);
out:
	return skb;
nodata:
	put_cached_skb(tp, skb);
	skb = NULL;
	goto out;
}

void release_skb_nettrace(struct sk_buff_nettrace *skb, TRACE_POINT *tp)
{
	struct sk_buff *frag, *tmp;
	if (!skb_queue_empty(&skb->frag_list)) {
		skb_queue_walk_safe(&skb->frag_list, frag, tmp) {
			put_cached_data(tp, ((struct sk_buff_nettrace *)frag)->data);
			put_cached_skb(tp, (struct sk_buff_nettrace *)frag);
		}
	}
	put_cached_data(tp, skb->data);
	put_cached_skb(tp, skb);
}

struct sk_buff_nettrace *skb_copy_nettrace(const struct sk_buff *skb, TRACE_POINT *tp)
{
	int headerlen = skb_headroom(skb) - skb->mac_header;
	unsigned int size = skb->len + headerlen;
	unsigned int frag_num = size / PAGE_SIZE + 1;
	unsigned int i;
	struct sk_buff_nettrace *n;
	unsigned int dump_size = 0;

	/* Alloc skb. */
	for (i = 0; i < frag_num; i++) {
		if (!i) {
			n = __alloc_skb_nettrace(tp);
			if (!n)
				return NULL;
		} else {
			struct sk_buff_nettrace *frag = __alloc_skb_nettrace(tp);
			if (!frag)
				goto nofrag;
			__skb_queue_tail(&n->frag_list, (struct sk_buff *)frag);
		}
	}

	/* Copy first page. */
	n->len = size > PAGE_SIZE ? PAGE_SIZE : headerlen + skb->len;
	if (n->data) {
		if (skb_copy_bits(skb, -headerlen, n->data, n->len))
			pr_warn("[nettrace]: Copy SKB failed.\n");
	}
	dump_size += n->len;
	/* Only one. */
	if (size <= PAGE_SIZE) {
		n->total_len = dump_size;
		return n;
	}

	/* Copy frag pages. */
	if (!skb_queue_empty(&n->frag_list)) {
		struct sk_buff *frag, *tmp;
		unsigned int i = 0;

		skb_queue_walk_safe(&n->frag_list, frag, tmp) {
			i++;
			((struct sk_buff_nettrace *)frag)->len = size - PAGE_SIZE * i > PAGE_SIZE ?
								PAGE_SIZE : size - PAGE_SIZE * i;
			if (((struct sk_buff_nettrace *)frag)->data) {
				if (skb_copy_bits(skb, -headerlen + PAGE_SIZE * i,
						((struct sk_buff_nettrace *)frag)->data,
						((struct sk_buff_nettrace *)frag)->len))
					pr_warn("[nettrace]: Copy SKB failed.\n");
			}
			dump_size += ((struct sk_buff_nettrace *)frag)->len;
		}
	}
	if (n)
		n->total_len = dump_size;
	return n;

nofrag:
	release_skb_nettrace(n, tp);
	return NULL;
}

