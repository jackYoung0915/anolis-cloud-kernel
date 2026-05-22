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

#ifndef IP_PARSER_H
#define IP_PARSER_H

#include <linux/list.h>
#include <linux/tcp.h>

#define IP_ADDR_LEN 16

#define TCP_FLAG_LEN 6

/* flags for network package. */
#define FLAG_proto_3	(1UL << 0)
#define FLAG_proto_4	(1UL << 1)
#define FLAG_sport		(1UL << 2)
#define FLAG_dport		(1UL << 3)
#define FLAG_saddr		(1UL << 4)
#define FLAG_daddr		(1UL << 5)

/* flags for match. */
#define FLAG_addr		(1UL << 6)
#define FLAG_port		(1UL << 7)

typedef
struct common_rule {
	struct list_head list;
	__u16 __flags;
	__u16 proto_3;
	__u8  proto_4;
	__u16 sport;
	__u16 dport;
	__u32 saddr;
	__u32 s_mask;
	__u32 daddr;
	__u32 d_mask;
	__u8  flags;
} COMMON_RULE;

#define SET_RULE_FLAGS(rule, flags, value) \
{\
	(rule)->flags = value;\
	(rule)->__flags |= FLAG_##flags;\
}

extern int i2ip(u32 ip, char *dest);

extern int ip2i(char *ip_str, u32 *ip);

extern int str2proto4(char *proto_str);

extern int str2proto3(char *proto_str);

extern char *proto3tostr(int p);

extern char *proto4tostr(int p);

extern bool match_rule(COMMON_RULE *remote, COMMON_RULE *local);

extern bool match_all_rule(COMMON_RULE *remote);

extern int add_rule(COMMON_RULE *rule);

extern void free_rules(void);

extern void flag2str(struct tcphdr *tcp, char *str);

#endif //IP_PARSER_H
