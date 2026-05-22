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

#include <linux/kernel.h>
#include <linux/string.h>
#include <uapi/linux/tcp.h>
#include "parser.h"
#include "help.h"
#include "utils.h"

LIST_HEAD(rule_list);

typedef struct inet_proto {
	int number;
	char *name;
} INET_PROTO;

const INET_PROTO proto4[] = {
		{.number = 0, .name = "ip"},
		{.number = 1, .name = "icmp"},
		{.number = 2, .name = "igmp"},
		{.number = 4, .name = "ipip"},
		{.number = 6, .name = "tcp"},
		{.number = 8, .name = "egp"},
		{.number = 12, .name = "pup"},
		{.number = 17, .name = "udp"},
		{.number = 22, .name = "idp"},
		{.number = 29, .name = "tp"},
		{.number = 33, .name = "dccp"},
		{.number = 41, .name = "ipv6"},
		{.number = 46, .name = "rsvp"},
		{.number = 47, .name = "gre"},
		{.number = 50, .name = "esp"},
		{.number = 51, .name = "ah"},
		{.number = 92, .name = "mtp"},
		{.number = 94, .name = "beetph"},
		{.number = 98, .name = "encap"},
		{.number = 103, .name = "pim"},
		{.number = 108, .name = "comp"},
		{.number = 132, .name = "sctp"},
		{.number = 136, .name = "udplite"},
		{.number = 137, .name = "mpls"},
		{.number = 255, .name = "raw"}
};
const int proto4_len = sizeof(proto4) / sizeof(INET_PROTO);

const INET_PROTO proto3[] = {
		{.number = 0x0060, .name = "loop"},
		{.number = 0x0200, .name = "pup"},
		{.number = 0x0201, .name = "pupat"},
		{.number = 0x22F0, .name = "tsn"},
		{.number = 0x22EB, .name = "erspan2"},
		{.number = 0x0800, .name = "ip"},
		{.number = 0x0805, .name = "x25"},
		{.number = 0x0806, .name = "arp"},
		{.number = 0x08FF, .name = "bpq"},
		{.number = 0x0a00, .name = "ieeepup"},
		{.number = 0x0a01, .name = "ieeepupat"},
		{.number = 0x4305, .name = "batman"},
		{.number = 0x6000, .name = "dec"},
		{.number = 0x6001, .name = "dna_dl"},
		{.number = 0x6002, .name = "dna_rc"},
		{.number = 0x6003, .name = "dna_rt"},
		{.number = 0x6004, .name = "lat"},
		{.number = 0x6005, .name = "diag"},
		{.number = 0x6006, .name = "cust"},
		{.number = 0x6007, .name = "sca"},
		{.number = 0x6558, .name = "teb"},
		{.number = 0x8035, .name = "rarp"},
		{.number = 0x809B, .name = "atalk"},
		{.number = 0x80F3, .name = "aarp"},
		{.number = 0x8100, .name = "8021q"},
		{.number = 0x88BE, .name = "erspan"},
		{.number = 0x8137, .name = "ipx"},
		{.number = 0x86DD, .name = "ipv6"},
		{.number = 0x8808, .name = "pause"},
		{.number = 0x8809, .name = "slow"},
		{.number = 0x883E, .name = "wccp"},
		{.number = 0x8847, .name = "mpls_uc"},
		{.number = 0x8848, .name = "mpls_mc"},
		{.number = 0x884c, .name = "atmmpoa"},
		{.number = 0x8863, .name = "ppp_disc"},
		{.number = 0x8864, .name = "ppp_ses"},
		{.number = 0x886c, .name = "link_ctl"},
		{.number = 0x8884, .name = "atmfate"},
		{.number = 0x888E, .name = "pae"},
		{.number = 0x88A2, .name = "aoe"},
		{.number = 0x88A8, .name = "8021ad"},
		{.number = 0x88B5, .name = "802_ex1"},
		{.number = 0x88C7, .name = "preauth"},
		{.number = 0x88CA, .name = "tipc"},
		{.number = 0x88CC, .name = "lldp"},
		{.number = 0x88E5, .name = "macsec"},
		{.number = 0x88E7, .name = "8021ah"},
		{.number = 0x88F5, .name = "mvrp"},
		{.number = 0x88F7, .name = "1588"},
		{.number = 0x88F8, .name = "ncsi"},
		{.number = 0x88FB, .name = "prp"},
		{.number = 0x8906, .name = "fcoe"},
		{.number = 0x8915, .name = "iboe"},
		{.number = 0x890D, .name = "tdls"},
		{.number = 0x8914, .name = "fip"},
		{.number = 0x8917, .name = "80221"},
		{.number = 0x892F, .name = "hsr"},
		{.number = 0x894F, .name = "nsh"},
		{.number = 0x9000, .name = "loopback"},
		{.number = 0x9100, .name = "qinq1"},
		{.number = 0x9200, .name = "qinq2"},
		{.number = 0x9300, .name = "qinq3"},
		{.number = 0xDADA, .name = "edsa"},
		{.number = 0xDADB, .name = "dsa_8021q"},
		{.number = 0xED3E, .name = "ife"},
		{.number = 0xFBFB, .name = "af_iucv"},
		{.number = 0x0600, .name = "802_3_min"}
};
const int proto3_len = sizeof(proto3) / sizeof(INET_PROTO);


/****************************************************************************************************
 *
 * 				This is the part for skb bag parse
 *
 ****************************************************************************************************/

/*parse u32 to ip addr string.*/
int i2ip(__be32 ip, char *dest)
{
	u8 *tmp = (u8 *) &ip;

	return sprintf(dest, "%u.%u.%u.%u",
				   *(tmp),
				   *(tmp + 1),
				   *(tmp + 2),
				   *(tmp + 3)) < 0;
}

/*parse ip addr string to u32.*/
int ip2i(char *ip_str, u32 *ip)
{
	int ip_v[4];
	int i = 0;
	u32 ip_tmp = 0;
	int tmp;

	if (sscanf(ip_str, "%d.%d.%d.%d",
			   &ip_v[0],
			   &ip_v[1],
			   &ip_v[2],
			   &ip_v[3]) < 4)
		return -1;

	for (; i < 4; i++) {
		tmp = ip_v[i];
		if (tmp < 0 || tmp > 255)
			return -1;
		ip_tmp += (((u32)tmp) << ((3 - i) * 8));
	}
	*ip = ip_tmp;
	return 0;
}

static
int str2proto(char *proto_str, INET_PROTO proto[], int len)
{
	int i = 0;

	for (; i < len; i++) {
		if (streq(proto[i].name, proto_str))
			return proto[i].number;
	}
	return -1;
}

static
char *proto2str(int proto, INET_PROTO protos[], int len)
{
	int i = 0;

	for (; i < len; i++) {
		if (protos[i].number == proto)
			return protos[i].name;
	}
	return NULL;
}

int str2proto3(char *proto_str)
{
	return str2proto(proto_str, (INET_PROTO *) proto3, proto3_len);
}

int str2proto4(char *proto_str)
{
	return str2proto(proto_str, (INET_PROTO *) proto4, proto4_len);
}

char *proto3tostr(int p)
{
	return proto2str(p, (INET_PROTO *) proto3, proto3_len);
}

char *proto4tostr(int p)
{
	return proto2str(p, (INET_PROTO *) proto4, proto4_len);
}

void flag2str(struct tcphdr *tcp, char *str)
{
	if (strlen(str) + 1 > TCP_FLAG_LEN) {
		log_err("%s: string length(%s) exceeds TCP_FLAG_LEN\n", __func__, str);
		return;
	}

	if (tcp->psh)
		strncat(str, "P", 1);
	if (tcp->rst)
		strncat(str, "R", 1);
	if (tcp->syn)
		strncat(str, "S", 1);
	if (tcp->fin)
		strncat(str, "F", 1);
}

/****************************************************************************************************
 *
 * 				This is the part for ip bag match
 *
 ****************************************************************************************************/

/*
 * 	'remote' is the match rule that generated form ip package, 'local' is the
 * 	match rule that user define.
 *
 *		The return value is 0 if matched, and -1 otherwise.
 */
bool match_rule(COMMON_RULE *remote, COMMON_RULE *local)
{

	if ((local->__flags & FLAG_proto_3) && local->proto_3 != remote->proto_3)
		return false;
	if ((local->__flags & FLAG_proto_4) && local->proto_4 != remote->proto_4)
		return false;

	if (local->__flags & FLAG_port &&
		local->sport != remote->sport &&
		local->sport != remote->dport)
		return false;

	if ((local->__flags & FLAG_dport) && local->dport != remote->dport)
		return false;
	if ((local->__flags & FLAG_sport) && local->sport != remote->sport)
		return false;

	if (local->__flags & FLAG_addr && (
		local->saddr != (remote->saddr & local->s_mask) &&
		local->saddr != (remote->daddr & local->s_mask)))
		return false;

	if ((local->__flags & FLAG_saddr) &&
		local->saddr != (remote->saddr & local->s_mask))
		return false;
	if ((local->__flags & FLAG_daddr) &&
		local->daddr != (remote->daddr & local->d_mask))
		return false;

	return true;
}

bool match_all_rule(COMMON_RULE *remote)
{
	bool is_empty = true;
	COMMON_RULE *rule;

	list_for_each_entry(rule, &rule_list, list) {
		is_empty = false;
		if (match_rule(remote, rule))
			return true;
	}

	if (is_empty)
		return true;
	return false;
}

int add_rule(COMMON_RULE *rule)
{
	if (rule->__flags) {
		list_add_tail(&rule->list, &rule_list);
		return 0;
	}
	return -1;
}

void free_rules(void) {
	COMMON_RULE *rule, *pre;
	list_for_each_entry_safe(rule, pre, &rule_list, list) {
		kfree(rule);
	}
}
