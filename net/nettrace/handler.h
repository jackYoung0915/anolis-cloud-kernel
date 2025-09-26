/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Nettrace support.
 *
 * Copyright (C) 2022 ZTE Corporation. All rights reserved.
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

#ifndef NETDUMP_HANDLER_H
#define NETDUMP_HANDLER_H

#include "group.h"
#include "parser.h"

#define MAX_OUTPUT_LEN 300

#if defined(CONFIG_BACKTRACE_USRSTACK_ARM64) || defined(CONFIG_BACKTRACE_USRSTACK_X86_64)
extern void backtrace_usrstack(void);
#endif

struct kretprobe_instance;

struct skb_output {
	COMMON_RULE		rule;
	struct sk_buff	*skb;
	char	*sym_name;
	char	flags[TCP_FLAG_LEN];
	int		ret_val;
};

extern struct trace_group *all_group;

extern void
post_handler_general(struct kprobe *p, struct pt_regs *regs, unsigned long flags);

extern void
post_handler_udp_tracer(struct kprobe *p, struct pt_regs *regs, unsigned long flags);

extern int
ret_handler_general(struct kretprobe_instance *ri, struct pt_regs *regs);

extern int
entry_handler_general(struct kretprobe_instance *ri, struct pt_regs *regs);

#endif //NETDUMP_HANDLER_H
