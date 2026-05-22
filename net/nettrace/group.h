/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef NETDUMP_GROUP_H
#define NETDUMP_GROUP_H

#include <linux/skbuff.h>
#include <linux/kallsyms.h>
#include "mm.h"

#define MAX_TP_NAME  KSYM_NAME_LEN
/* The length of group name */
#define MAX_TP_GROUP 128
/* The length of TP description */
#define MAX_TP_DESC 256

#define for_each_tp(i, p) \
    for (i = 0; i < all_tp_len && ({ p = &all_tp[i]; 1;}); i++)

/*Definition of kprobe point that we predefined*/
typedef struct trace_point {
	/* The traced function name */
	char	name[MAX_TP_NAME];
	/* To define the position index of struct sk_buff *skb in a certain
	 * trace_point funcion
	 */
	int		skb_index;
	/* To define the position index of struct sk_buff **pskb in a certain
	 * trace_point funcion
	 */
	int		pskb_index;

	int		sock_index;
	/* Count the number of skb when dumpping in the current TP. */
	unsigned int dump_cnt;
	char	desc[MAX_TP_DESC];
	char	groups[MAX_TP_GROUP];
	bool	is_ret;
	struct	kprobe *kprobe;
	struct	file *dump_file;
	struct	work_struct dump_work;
	struct	sk_buff_head dump_queue;
	struct	llist_head skbcache;
	int	nr_skb_objs;
	struct	llist_head datacache;
	int	nr_data_objs;
} TRACE_POINT;

typedef struct tp_list {
	TRACE_POINT *tp;
	struct list_head list;
} TP_LIST;

enum trace_group_level {
	BASIC,
	MOD
};

struct kretprobe_instance;

typedef struct ret_data {
	struct sk_buff	*skb;
	struct sock		*sk;
} RET_DATA;

typedef struct trace_group {
	char name[MAX_TP_NAME];
	char desc[MAX_TP_DESC];
	enum trace_group_level level;
	struct list_head list;
	struct list_head groups;
	struct list_head traces;
	bool activated;

	void (*handler)(struct kprobe *p,
					struct pt_regs *regs,
					unsigned long flags);
	int (*ret_handler)(struct kretprobe_instance *ri,
					struct pt_regs *regs);
} TRACE_GROUP;

extern const int all_tp_len;
extern struct trace_point all_tp[];

extern TRACE_GROUP *all_group;

extern void init_group(void);

extern int trace_register(void);

extern void free_all_group(void);

#endif //NETDUMP_GROUP_H
