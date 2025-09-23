// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nettrace support.
 *
 * Copyright (C) 2025 ZTE Corporation. All rights reserved.
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

#include <linux/if_ether.h>
#include <linux/fs.h>
#include <linux/skbuff.h>

#include "dump.h"
#include "utils.h"
#include "group.h"
#include "core.h"
#include "mm.h"

/* The number of packets which is not dumped into pcap due to insufficient memory. */
unsigned int dump_loss_due_to_no_memory;

/* The number of packets which is not dumped into pcap due to the limit of max_dump_skb_cnt. */
unsigned int dump_skb_over_cnt;

/* The number of packets which is not dumped into pcap due to the limit of max_dump_file_size. */
unsigned int dump_skb_over_size;

static int dump_skb(struct sk_buff_nettrace *skb, struct file *to)
{
	struct pcap_pkthdr phdr;
	struct timespec64 tv;
	struct sk_buff *frag, *tmp;

	ktime_get_real_ts64(&tv);
	phdr.ts.tv_sec = tv.tv_sec;
	phdr.ts.tv_usec = tv.tv_nsec / 1000;

	phdr.len = skb->total_len;
	phdr.caplen = skb->total_len;

	file_append(to, &phdr, sizeof(phdr));
	file_append(to, skb->data, skb->len);

	if (!skb_queue_empty(&skb->frag_list)) {
		skb_queue_walk_safe(&skb->frag_list, frag, tmp)
			file_append(to, ((struct sk_buff_nettrace *)frag)->data,
					((struct sk_buff_nettrace *)frag)->len);
	}

	return 0;
}

int init_pcap(struct file *f)
{
	struct pcap_file_header hdr;

	hdr.magic = PCAP_MAGIC;
	hdr.version_major = PCAP_VERSION_MAJOR;
	hdr.version_minor = PCAP_VERSION_MINOR;
	hdr.thiszone = sys_tz.tz_dsttime;
	hdr.sigfigs = 0;
	hdr.snaplen = DEFAULT_SNAPLEN;
	hdr.linktype = LINKTYPE_ETHERNET;

	file_append(f, &hdr, sizeof(hdr));

	return 0;
}

static __always_inline void dump_queue_lock(struct sk_buff_head *dump_queue, unsigned long flag)
{
	spin_lock_irqsave(&dump_queue->lock, flag);
}

static __always_inline void dump_queue_unlock(struct sk_buff_head *dump_queue, unsigned long flag)
{
	spin_unlock_irqrestore(&dump_queue->lock, flag);
}

static void dump_skb_work(struct work_struct *work)
{
	TRACE_POINT *tp = container_of(work, TRACE_POINT, dump_work);

	struct sk_buff_nettrace *skb;
	struct sk_buff_head list;
	unsigned long flag = 0;

	__skb_queue_head_init(&list);

	dump_queue_lock(&tp->dump_queue, flag);
	skb_queue_splice_tail_init(&tp->dump_queue, &list);
	dump_queue_unlock(&tp->dump_queue, flag);

	while ((skb = (struct sk_buff_nettrace *) __skb_dequeue(&list))) {

		/* Don't waste time on writing dump_file when exit */
		if (likely(READ_ONCE(nt_status) != NT_EXITING)) {
			dump_skb(skb, tp->dump_file);
			tp->dump_cnt++;
		}

		release_skb_nettrace(skb, tp);
	}
	return;
}

void try_dump_skb(struct sk_buff *skb, TRACE_POINT *tp)
{
	struct sk_buff_nettrace *new_skb;
	unsigned long flag;

	/* if dump_skb_cnt exceed the uppper limit, drop it */
	if (tp->dump_cnt > max_dump_skb_cnt) {
		dump_skb_over_cnt++;
		return;
	}

	/* if dump_file_size exceed the uppper limit, drop it */
	if (tp->dump_file->f_pos + skb->truesize
				 + sizeof(struct pcap_pkthdr) >= max_dump_file_size) {
		dump_skb_over_size++;
		return;
	}

	new_skb = skb_copy_nettrace(skb, tp);

	if (!new_skb) {
		dump_loss_due_to_no_memory++;
		return;
	}

	dump_queue_lock(&tp->dump_queue, flag);
	__skb_queue_tail(&tp->dump_queue, (struct sk_buff *)new_skb);
	dump_queue_unlock(&tp->dump_queue, flag);

	schedule_work(&tp->dump_work);
}

void init_dump_work(TRACE_POINT *tp)
{
	INIT_WORK(&tp->dump_work, dump_skb_work);
	skb_queue_head_init(&tp->dump_queue);
}
