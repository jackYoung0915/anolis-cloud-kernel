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

#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/if_ether.h>
#include <linux/icmp.h>
#include <uapi/linux/icmp.h>
#include <linux/skbuff.h>

#include "handler.h"
#include "kprobe.h"
#include "group.h"
#include "parser.h"
#include "help.h"
#include "dump.h"
#include "utils.h"
#include "core.h"

/****************************************************************************************************
 *
 *                              This is the part for filter, by skb or sock
 *
 ****************************************************************************************************/

static int filter_skb(struct sk_buff *skb, struct sock *sk, TRACE_POINT *tp, SKB_OUTPUT *skb_output);

static int filter_sock(struct sock *sk, TRACE_POINT *tp, SKB_OUTPUT *output);

/*
 * This is the general skb info print function.
 *
 * The info in 'output' will be printed by 'log_data' log level.
 */
static void general_print(SKB_OUTPUT *output, TRACE_POINT *tp)
{
        char saddr[IP_ADDR_LEN] = {}, daddr[IP_ADDR_LEN] = {},
                                        output_str[MAX_OUTPUT_LEN] = {};
        COMMON_RULE             *rule = &output->rule;
        struct icmphdr  *icmp;
        struct sk_buff  *skb = output->skb;
        struct net_device *dev = skb ? skb->dev : NULL;
	char *type = "";

        str_append(output_str, "[%d]", dev ? dev->ifindex : 0);
        if (tp->is_ret)
                str_append(output_str, "[%s,ret:%d]:", output->sym_name, output->ret_val);
        else
                str_append(output_str, "[%s]:", output->sym_name);
        if (rule->proto_3 != ETH_P_IP) {
                str_append(output_str, "proto: %s", proto3tostr(rule->proto_3));
                goto begin_print;
        }

        if (i2ip(rule->saddr, saddr) || i2ip(rule->daddr, daddr)) {
                log_err("parse ip addr error!");
                return;
        }
        str_append(output_str, "IP %s>%s", saddr, daddr);

        switch (rule->proto_4) {
        case IPPROTO_TCP:
                str_append(output_str,
                                        " // TCP %d>%d,%s",
                                        ntohs(rule->sport), ntohs(rule->dport), output->flags);
                break;
        case IPPROTO_UDP:
                str_append(output_str,
                                        " // UDP %d>%d",
                                        ntohs(rule->sport), ntohs(rule->dport));
                break;
        case IPPROTO_ICMP:
                if (!output->skb) {
                        str_append(output_str, " // ICMP");
                        break;
                }

                if (!skb_transport_header_was_set(skb))
                        icmp = (struct icmphdr *) (skb_network_header(skb) + sizeof(struct iphdr));
                else
                        icmp = icmp_hdr(skb);

                if (icmp->code == 0 && icmp->type == 8)
                        type = "request";
                if (icmp->code == 0 && icmp->type == 0)
                        type = "response";

                str_append(output_str,
                                   " // ICMP %s %u",
                                   type,
                                   ntohs(icmp->un.echo.sequence));
                break;
        default:
                str_append(output_str,
                                " // %s",
                                proto4tostr(rule->proto_4));
                break;
        }

begin_print:
        log_data("%s\n", output_str);

#if defined(CONFIG_BACKTRACE_USRSTACK_ARM64) || defined(CONFIG_BACKTRACE_USRSTACK_X86_64)
        if (print_ustack)
                backtrace_usrstack();
#endif

        if (output->skb && tp->dump_file)
                try_dump_skb(output->skb, tp);

        if (print_stack)
                dump_stack();
}

/*
 * Print the network information by print the skb.
 *
 * Note that this is most about the kernel function that receive skb,
 * as the package header in skb is not completed in skb send function.
 */
static int filter_skb(struct sk_buff *skb, struct sock *sk,
                                           TRACE_POINT *tp, SKB_OUTPUT *skb_output)
{
        int  proto_3, proto_4, sport, dport;
        struct ethhdr   *eth;
        struct tcphdr   *tcp;
        struct udphdr   *udp;
        struct iphdr    *ip;

        COMMON_RULE *rule    = &skb_output->rule;
        skb_output->sym_name = tp->name;
        rule->s_mask = 0xffffffff;
        rule->d_mask = 0xffffffff;
        eth = eth_hdr(skb);
        if (!sk)
                sk = skb->sk;

        if (!skb_mac_header_was_set(skb)) {
                proto_3 = ntohs(skb->protocol);
                if (proto_3)
                        goto parse_network;

                if (!sk)
                        goto error;

                if (sk->sk_family == PF_INET && skb->network_header) {
                        proto_3 = ETH_P_IP;
                        goto parse_network;
                }

                return filter_sock(sk, tp, skb_output);
        }
        skb_output->skb = skb;
        proto_3 = ntohs(eth->h_proto);

parse_network:
        SET_RULE_FLAGS(rule, proto_3, proto_3);
        if (proto_3 != ETH_P_IP)
                goto do_match;

        ip = ip_hdr(skb);
	if (likely((u8 *)ip >= skb->head &&
			(u8 *)ip + sizeof(struct iphdr) <= skb_tail_pointer(skb))) {
		proto_4 = ip->protocol;
		SET_RULE_FLAGS(rule, proto_4, proto_4);
		SET_RULE_FLAGS(rule, saddr, ip->saddr);
		SET_RULE_FLAGS(rule, daddr, ip->daddr);
	} else {
		proto_4 = 0;
	}

        switch (proto_4) {
        case IPPROTO_TCP:
                tcp = tcp_hdr(skb);
		if (likely((u8 *)tcp >= skb->head &&
				(u8 *)tcp + sizeof(struct tcphdr) <= skb_tail_pointer(skb))) {
			sport = tcp->source;
			dport = tcp->dest;
			flag2str(tcp, skb_output->flags);
		} else {
			sport = 0;
			dport = 0;
		}
                goto flag_port;

        case IPPROTO_UDP:
                udp = udp_hdr(skb);
		if (likely((u8 *)udp >= skb->head &&
				(u8 *)udp + sizeof(struct udphdr) <= skb_tail_pointer(skb))) {
			sport = udp->source;
			dport = udp->dest;
		} else {
			sport = 0;
			dport = 0;
		}
                goto flag_port;
        default:
                break;
        }

do_match:
        if (match_all_rule(rule))
                return 0;
        return -1;

flag_port:
        SET_RULE_FLAGS(rule, sport, sport);
        SET_RULE_FLAGS(rule, dport, dport);
        goto do_match;

error:
        return -1;
}

/*
 * Print the network information by print the sock.
 *
 * Note this is most about the process that send skb, during which
 * skb headers is not ready and we can not get information from it,
 * such ip addr or tcp port.
 */
static int filter_sock(struct sock *sk, TRACE_POINT *tp, SKB_OUTPUT *output)
{
        const struct inet_sock *inet;
        int sport = 0, dport = 0;

        COMMON_RULE *rule = &output->rule;
        rule->s_mask = 0xffffffff;
        rule->d_mask = 0xffffffff;
        output->sym_name = tp->name;

        SET_RULE_FLAGS(rule, proto_4, sk->sk_protocol);

        if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
                goto do_filter;

        inet = inet_sk(sk);

        SET_RULE_FLAGS(rule, proto_3, ETH_P_IP);
        SET_RULE_FLAGS(rule, saddr, inet->inet_saddr);
        SET_RULE_FLAGS(rule, daddr, inet->inet_daddr);

        sport = inet->inet_sport;
        dport = inet->inet_dport;

        SET_RULE_FLAGS(rule, sport, sport);
        SET_RULE_FLAGS(rule, dport, dport);

do_filter:
        if (match_all_rule(rule))
                return 0;
        return -1;
}

/****************************************************************************************************
 *
 *                              This is the part for all kind of handlers.
 *
 ****************************************************************************************************/

/*the function that handle skb and sock.*/
void __post_handler_general(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{

        struct sk_buff  *skb = NULL;
        struct sock             *sk = NULL;
        SKB_OUTPUT skb_output = {};
        TRACE_POINT             *tp = (TRACE_POINT *) p->symbol_name;

        if (tp->sock_index > 0) {
                sk = (struct sock *) kprobe_parm(regs, tp->sock_index);
        }

        if (tp->skb_index > 0) {
                skb = (struct sk_buff *) kprobe_parm(regs, tp->skb_index);
                goto do_print_skb;
        }

        if (tp->pskb_index > 0) {
                skb = *((struct sk_buff **) kprobe_parm(regs, tp->pskb_index));
                goto do_print_skb;
        }

        if (sk && !filter_sock(sk, tp, &skb_output))
                general_print(&skb_output, tp);
        return;

do_print_skb:
        if (skb && !filter_skb(skb, sk, tp, &skb_output))
                general_print(&skb_output, tp);
}

void post_handler_general(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
        /* If being removed, nettrace should stop tracing as soon as possible */
        if (unlikely(READ_ONCE(nt_status) == NT_EXITING))
                return;

        /* If insmoding nettrace is not completed, don't start to parse */
        if (unlikely(READ_ONCE(nt_status) == NT_INIT))
                return;

        __post_handler_general(p, regs, flags);
}

int entry_handler_general(struct kretprobe_instance *ri, struct pt_regs *regs)
{
        struct sk_buff  *skb = NULL;
        struct sock             *sk = NULL;
        TRACE_POINT             *tp = NULL;
        RET_DATA                *data = (RET_DATA *) ri->data;

#ifdef CONFIG_KRETPROBE_ON_RETHOOK
        struct kretprobe *rp = get_kretprobe(ri);
        if (unlikely(!rp))
		return 1;
        tp = (TRACE_POINT *) rp->kp.symbol_name;
#else
        tp = (TRACE_POINT *) ri->rph->rp->kp.symbol_name;
#endif
        if (tp->sock_index > 0) {
                sk = (struct sock *) kprobe_parm(regs, tp->sock_index);
        }

        if (tp->skb_index > 0) {
                skb = (struct sk_buff *) kprobe_parm(regs, tp->skb_index);
        }

        if (tp->pskb_index > 0) {
                skb = *((struct sk_buff **) kprobe_parm(regs, tp->pskb_index));
        }

        data->sk = sk;
        data->skb = skb;

        if (sk == NULL && skb == NULL)
                return 1;
        return 0;
}

int __ret_handler_general(struct kretprobe_instance *ri, struct pt_regs *regs)
{
        struct sk_buff  *skb = NULL;
        struct sock             *sk = NULL;
        SKB_OUTPUT skb_output = {};
        TRACE_POINT             *tp = NULL;
        RET_DATA                *data = (RET_DATA *) ri->data;

#ifdef CONFIG_KRETPROBE_ON_RETHOOK
        struct kretprobe *rp = get_kretprobe(ri);
        if (unlikely(!rp))
		return 1;
        tp = (TRACE_POINT *) rp->kp.symbol_name;
#else
        tp = (TRACE_POINT *) ri->rph->rp->kp.symbol_name;
#endif
        sk = data->sk;
        skb_output.ret_val = KPROBE_RET_PARM;

        if ((skb = data->skb) != NULL)
                goto do_print_skb;

        if (sk && !filter_sock(sk, tp, &skb_output))
                general_print(&skb_output, tp);
        return 0;

do_print_skb:
        if (skb && !filter_skb(skb, sk, tp, &skb_output))
                general_print(&skb_output, tp);
        return 0;
}

int ret_handler_general(struct kretprobe_instance *ri, struct pt_regs *regs)
{
        /* If being removed, nettrace should stop tracing as soon as possible */
        if (unlikely(READ_ONCE(nt_status) == NT_EXITING))
                return 0;

        /* If insmoding nettrace is not completed, don't start to parse */
        if (unlikely(READ_ONCE(nt_status) == NT_INIT))
                return 0;

        return __ret_handler_general(ri, regs);
}

/*the function that handle skb and sock.*/
void post_handler_udp_tracer(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{

        struct sk_buff *skb     = NULL;
        struct sock *sk         = NULL;
        SKB_OUTPUT skb_output = {};

        TRACE_POINT *tp = (TRACE_POINT *) p->symbol_name;

        if (tp->sock_index > 0) {
                sk = (struct sock *) kprobe_parm(regs, tp->sock_index);
        }

        if (tp->skb_index > 0) {
                skb = (struct sk_buff *) kprobe_parm(regs, tp->skb_index);
                goto do_print_skb;
        }

        if (tp->pskb_index > 0) {
                skb = *((struct sk_buff **) kprobe_parm(regs, tp->pskb_index));
                goto do_print_skb;
        }

        if (sk && !filter_sock(sk, tp, &skb_output))
                goto do_print;
        return;

do_print_skb:
        if (skb && !filter_skb(skb, sk, tp, &skb_output))
                goto do_print;
        return;

do_print:
        general_print(&skb_output, tp);

        if (!print_stack && streq(tp->name, "kfree_skb_reason"))
                dump_stack();
}

