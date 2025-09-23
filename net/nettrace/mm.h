/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef NETDUMP_MM_H
#define NETDUMP_MM_H

#include "group.h"

struct sk_buff_nettrace {
	struct sk_buff_nettrace  *next;
	struct sk_buff_nettrace  *prev;
	struct sk_buff_head	frag_list;
	unsigned int            len;
	unsigned int		total_len;
	unsigned char           *data;
};

typedef struct trace_point TRACE_POINT;
struct sk_buff_nettrace *skb_copy_nettrace(const struct sk_buff *skb, TRACE_POINT *tp);
void init_dump_mm(TRACE_POINT *tp);
void release_dump_mm(TRACE_POINT *tp);
void release_skb_nettrace(struct sk_buff_nettrace *skb, TRACE_POINT *tp);

struct sk_buff_nettrace *get_cached_skb(TRACE_POINT *tp);
bool put_cached_skb(TRACE_POINT *tp, struct sk_buff_nettrace *skb);
u8 *get_cached_data(TRACE_POINT *tp);
bool put_cached_data(TRACE_POINT *tp, u8 *data);

#endif //NETDUMP_MM_H
