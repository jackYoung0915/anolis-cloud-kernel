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

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include "group.h"
#include "utils.h"
#include "kprobe.h"
#include "help.h"
#include "handler.h"
#include "dump.h"
#include "mm.h"

/*
 * The kernel function that we interest in. Normally, they can be divided into
 * four part: ethernet II, IP layer, udp layer, tcp layer and some common
 * function, such as kfree_skb().
 */
struct trace_point all_tp[]   = {

#define SKB_TP(i, g, n) {.skb_index = i, .groups = g, .name = n}
#define PSKB_TP(i, g, n) {.pskb_index = i, .groups = g, .name = n}
#define SK_TP(i, g, n) {.sock_index = i, .groups = g, .name = n}
#define SS_TP(skb, sk, g, n) \
	{.skb_index = skb, .sock_index = sk, .groups = g, .name = n}

	/* net link layout trace points. */
	PSKB_TP(1, "link_input", "__netif_receive_skb_core"),
	SKB_TP(2, "link_input", "napi_gro_receive"),
	SKB_TP(1, "link_input", "netif_receive_skb_internal"),
	SKB_TP(1, "link_input", "__netif_receive_skb"),
	SKB_TP(1, "link_input", "netif_rx"),
	SKB_TP(1, "link_input", "enqueue_to_backlog"),
	SKB_TP(1, "link_output", "__dev_queue_xmit"),
	SKB_TP(1, "link_output", "dev_hard_start_xmit"),
	SKB_TP(1, "link_output", "dev_queue_xmit_accel"),
	SKB_TP(2, "link_output", "dev_forward_skb"),
	SKB_TP(1, "link_output", "skb_do_redirect"),
	//tc_classify is discarded in Linux 4.19 and later version.
	//{.skb_index = 1, .groups = "link_input", .name = "tc_classify", .is_ret = true},

	/* ip layout trace points. */
	SKB_TP(1, "ip_input", "ip_rcv"),
	SKB_TP(3, "ip_input", "ip_rcv_finish"),
	SKB_TP(1, "ip_input", "ip_route_input_noref"),
	{.skb_index = 1, .groups = "ip_input", .name = "fib_validate_source", .is_ret = true},
	SKB_TP(2, "ip_input", "ip_rcv_finish_core"),
	SKB_TP(1, "ip_input", "ip_local_deliver"),
	SKB_TP(3, "ip_input", "ip_local_deliver_finish"),
	SKB_TP(1, "ip_input", "ip_forward"),
	SKB_TP(3, "ip_input", "ip_forward_finish"),
	SKB_TP(2, "ip_input", "ip_send_skb"),
	SS_TP(3, 2, "ip_output", "__ip_local_out"),
	SKB_TP(3, "ip_output", "ip_output"),
	SKB_TP(3, "ip_output", "ip_finish_output"),
	SKB_TP(3, "ip_output", "ip_finish_output2"),

	/* udp layout trace points. */
	SKB_TP(1, "udp_input", "__udp4_lib_rcv"),
	SKB_TP(2, "udp_input", "udp_queue_rcv_skb"),
	SKB_TP(2, "udp_input", "__udp_enqueue_schedule_skb"),
	SK_TP(1, "udp_input", "__skb_recv_udp"),
	SK_TP(1, "udp_input", "udp_recvmsg"),

	/* tcp layout trace points. */
	SKB_TP(1, "tcp_input", "tcp_v4_rcv"),
	SS_TP(2, 1, "tcp_input", "tcp_v4_do_rcv"),
	SS_TP(2, 1, "tcp_input", "tcp_rcv_established"),
	SS_TP(2, 1, "tcp_input", "tcp_rcv_state_process"),
	SS_TP(2, 1, "tcp_input", "tcp_data_queue"),
	SS_TP(2, 1, "tcp_input",  "tcp_queue_rcv"),
	SK_TP(1, "tcp_input", "tcp_recvmsg"),

	SK_TP(1, "tcp_output", "tcp_sendmsg"),
	SK_TP(1, "tcp_output", "tcp_push"),
	SK_TP(1, "tcp_output", "tcp_write_xmit"),
	SK_TP(1, "tcp_output", "tcp_set_state"),
	SS_TP(2, 1, "tcp_output", "__tcp_transmit_skb"),


	/* common skb trace points.  */
	SKB_TP(1, "error", "kfree_skb_reason"),
	SKB_TP(1, "normal", "consume_skb"),

	/* Compile when VNet MACVLAN=y */
	/* macvlan drop stat. */
	SKB_TP(1, "macvlan", "macvlan_start_xmit"),
	SKB_TP(1, "macvlan", "macvlan_handle_frame"),

#undef SKB_TP
#undef SK_TP
#undef SS_TP
};
const int	all_tp_len = sizeof(all_tp) / sizeof(struct trace_point);
TRACE_GROUP	*all_group;

static TRACE_GROUP *query_group(char *name, TRACE_GROUP *tp);

/****************************************************************************************************
 *
 * 				This is the part for trace point and group query.
 *
 ****************************************************************************************************/

static TRACE_GROUP
*parent_group(TRACE_GROUP *tg, TRACE_GROUP *parent)
{
	TRACE_GROUP *tmp, *tmp2;
	list_for_each_entry(tmp, &parent->groups, list) {
		if (tmp == tg)
			return parent;
		if ((tmp2 = parent_group(tg, tmp)) != NULL)
			return tmp2;
	}
	return NULL;
}

static inline
TRACE_GROUP *parent_group_all(TRACE_GROUP *tg)
{
	return parent_group(tg, all_group);
}

static TRACE_POINT *query_tp(char *name)
{
	int i = 0;
	for (; i < all_tp_len; i++) {
		if (streq(name, all_tp[i].name))
			return &all_tp[i];
	}
	return NULL;
}

static void *query_handler(TRACE_GROUP *tg)
{
	if (tg == NULL)
		return all_group->handler;

	if (tg->handler)
		return tg->handler;

	return query_handler(parent_group_all(tg));
}

static void *query_ret_handler(TRACE_GROUP *tg)
{
	if (tg == NULL)
		return all_group->ret_handler;

	if (tg->ret_handler)
		return tg->ret_handler;

	return query_ret_handler(parent_group_all(tg));
}

static TRACE_GROUP
*query_group(char *name, TRACE_GROUP *tp)
{
	TRACE_GROUP *pos, *tmp;

	if (streq(tp->name, name))
		return tp;

	list_for_each_entry(pos, &tp->groups, list) {
		if ((tmp = query_group(name, pos)) != NULL)
			return tmp;
	}
	return NULL;
}

/****************************************************************************************************
 *
 * 				This is the part for trace point register.
 *
 ****************************************************************************************************/

static int
trace_point_register(TRACE_POINT *tp, TRACE_GROUP *tg)
{
	char          path[MAX_FILE_NAME] = {};
	struct kprobe *p;
	struct file   *dump_file;

	if (tp->kprobe)
		return 0;

	if (tp->skb_index <= 0 && tp->sock_index <= 0 && tp->pskb_index <= 0) {
		log_err("kprobe %s has no index!\n", tp->name);
		goto out_err;
	}

	if (tp->is_ret) {
		p = (struct kprobe *) kretprobe_declare(tp->name,
												query_ret_handler(tg),
												entry_handler_general);
		if (!p)
			goto out_err;
		((struct kretprobe *)p)->data_size = sizeof(RET_DATA);
	}
	else
		p = kprobe_declare(tp->name, query_handler(tg));

	if (!p) {
		log_err("kprobe declare failed: %s\n", tp->name);
		goto out_err;
	}

	/* NOTE:
	 * When register kprobe, the given SYMBOL NAME may be not found in kallsyms
	 * for example, suppose we want to trace the kernel func:
	 *
	 * "__netif_receive_skb_core",
	 *
	 * HOWEVER, that symbol name might be changed by compilers into
	 *
	 * "__netif_receive_skb_core.constprop.0"
	 *
	 * So, we finally trace __netif_receive_skb_core.constprop.0 and use it as
	 * tp->name instead of __netif_receive_skb_core!
	 */
	if ((tp->is_ret && !c_register_kretprobe((struct kretprobe *) p)) ||
		(!tp->is_ret && !c_register_kprobe(p)))
		tp->kprobe = p;
	else {
		if (strcmp(tp->name, "macvlan_start_xmit") == 0 ||
			strcmp(tp->name, "macvlan_handle_frame") == 0) {
			log_err("  Please confirm if the macvlan module is inserted, no %s\n", tp->name);
		} else {
			log_err("kprobe register failed: %s\n", tp->name);
		}
		goto out_free_err;
	}

	if (print_dump) {
		snprintf(path, sizeof(path), "%s/%s.pcap", print_dump, tp->name);
		dump_file = file_create(path);
		if (!dump_file) {
			log_err("failed to create dump file: %s\n", path);
			goto out_err;
		}
		init_pcap(dump_file);
		init_dump_work(tp);
		init_dump_mm(tp);
		/* Add write barrier to avoid the compiler instruction recombination.
		 * We must guarantee that tp->dump_file is init after init_dump_work.
		 */
		wmb();
		tp->dump_file = dump_file;
	}

	return 0;
out_free_err:
	kfree(p);
out_err:
	return -1;
}

static void trace_reg_group(TRACE_GROUP *tg)
{
	TRACE_GROUP *tmp_tg;
	TP_LIST     *tpl;

	log_info("begin register group: %s\n", tg->name);

	list_for_each_entry(tpl, &tg->traces, list) trace_point_register(tpl->tp, tg);

	list_for_each_entry(tmp_tg, &tg->groups, list) trace_reg_group(tmp_tg);

	log_info("end register group: %s\n", tg->name);
}

/*register the kprobe that defined in 'global_kprobe_list'.*/
int trace_register(void)
{
	int t = 0;

	for (; t < filter_trace_len; t++) {
		char        *ft = filter_trace[t];
		TRACE_GROUP *tg = query_group(ft, all_group);
		if (!tg) {
			log_err("trace: %s not founded!\n", ft);
			return -EINVAL;
		}
		trace_reg_group(tg);
	}

	for (t = 0; t < filter_probe_len; t++) {
		char        *name = filter_probe[t];
		TRACE_POINT *tp   = query_tp(name);
		if (!tp) {
			log_err("probe: %s not founded!\n", name);
			return -EINVAL;
		}
		trace_point_register(tp, all_group);
	}

	return 0;
}

/****************************************************************************************************
 *
 * 				This is the part for trace group functions.
 *
 * 				In fact, trace group is organized in form of tree.
 *
 ****************************************************************************************************/

/*
 * add trace point to group.
 */
static int add2group(TRACE_POINT *tp, char *groups)
{
	char        *group;
	TP_LIST     *tpl;
	TRACE_GROUP *g;

	while ((group = strsep(&groups, ",")) != NULL) {
		g = query_group(group, all_group);
		if (!g) {
			log_err("group: %s not exits!", group);
			continue;
		}
		tpl = kmalloc(sizeof(TP_LIST), GFP_KERNEL);
		if (!tpl)
			return -ENOMEM;
		memset(tpl, 1, sizeof(TP_LIST));

		tpl->tp = tp;
		INIT_LIST_HEAD(&tpl->list);
		list_add_tail(&tpl->list, &g->traces);
	}

	return 0;
}

static void init_tp(void)
{
	TRACE_POINT *tp;
	int         i = 0;

	for_each_tp(i, tp) add2group(tp, tp->groups);
}

static TRACE_GROUP
*add_trace_group(char *name, char *desc, enum trace_group_level lev,
				 TRACE_GROUP *parent)
{
	TRACE_GROUP *tmp_tp = kmalloc(sizeof(TRACE_GROUP), GFP_KERNEL);

	if (!tmp_tp)
		return NULL;
	memset(tmp_tp, 0, sizeof(TRACE_GROUP));

	INIT_LIST_HEAD(&tmp_tp->list);
	INIT_LIST_HEAD(&tmp_tp->groups);
	INIT_LIST_HEAD(&tmp_tp->traces);

	strncpy(tmp_tp->name, name, MAX_TP_NAME - 1);
	strncpy(tmp_tp->desc, desc, MAX_TP_DESC -1);
	tmp_tp->level = lev;

	if (parent != NULL)
		list_add_tail(&tmp_tp->list, &parent->groups);
	else
		all_group = tmp_tp;

	return tmp_tp;
}

/* This function has been discarded */
/*
static
void copy_tg(TRACE_GROUP *tg, TRACE_GROUP *child)
{
	TRACE_GROUP *tmp_tg = NULL;
	TP_LIST     *tpl    = NULL;

	if (!tg || !child)
		return;

	list_for_each_entry(tpl, &child->traces, list) add2group(tpl->tp, tg->name);

	list_for_each_entry(tmp_tg, &child->groups, list) copy_tg(tg, tmp_tg);
}
*/

#define ADD_TRACE_GROUP(name, lev, parent, desc) \
    name = add_trace_group(#name, desc, lev, parent);
#define ADD_ANNOY_TRACE_GROUP(name, lev, parent, desc) \
    add_trace_group(#name, desc, lev, parent);

void init_group(void)
{
	/* define all trace groups. */
	TRACE_GROUP *all, *link, *ip, *tcp, *udp, *error, *macvlan;

	ADD_TRACE_GROUP(all,		BASIC,	NULL, "the root trace")
	ADD_TRACE_GROUP(link,		BASIC,	all,  "the link layer.")
	ADD_TRACE_GROUP(ip,			BASIC,	all,  "the ip layer");
	ADD_TRACE_GROUP(tcp,		BASIC,	all,  "the tcp layer");
	ADD_TRACE_GROUP(udp,		BASIC,	all,  "the udp layer");
	ADD_TRACE_GROUP(error,		BASIC,	all,  "the scene that free error package.");
	ADD_TRACE_GROUP(macvlan,	BASIC,	all,  "macvlan receive and send skb.");

	/* for kw scan. */
	if (!all)
		return;
	all->handler = post_handler_general;
	all->ret_handler = ret_handler_general;

	/* general network protocol stack. */
	ADD_ANNOY_TRACE_GROUP(link_input,	BASIC, link, "the link layer that receive package");
	ADD_ANNOY_TRACE_GROUP(link_output,	BASIC, link, "the link layer that send package");

	ADD_ANNOY_TRACE_GROUP(ip_input,		BASIC, ip, 	 "the ip layer that receive package");
	ADD_ANNOY_TRACE_GROUP(ip_output,	BASIC, ip, 	 "the ip layer that send package");

	ADD_ANNOY_TRACE_GROUP(tcp_input,	BASIC, tcp,	 "the tcp layer that receive package");
	ADD_ANNOY_TRACE_GROUP(tcp_output,	BASIC, tcp,	 "the tcp layer that send package");

	ADD_ANNOY_TRACE_GROUP(udp_input,	BASIC, udp,	 "the udp layer that receive package");

	ADD_ANNOY_TRACE_GROUP(normal,		BASIC, all,	 "the scene that free normal package.");

	init_tp();
}

static void free_group(TRACE_GROUP *tg)
{
	TP_LIST     *tp_pos, *tp_next;
	TRACE_GROUP *pos, *next;
	TRACE_POINT *tp;

	list_for_each_entry_safe(pos, next, &tg->groups, list) free_group(pos);

	list_for_each_entry_safe(tp_pos, tp_next, &tg->traces, list) {
		tp = tp_pos->tp;
		kfree(tp_pos);

		if (tp->kprobe) {
			if (tp->is_ret)
				c_unregister_kretprobe((struct kretprobe *) tp->kprobe);
			else
				c_unregister_kprobe(tp->kprobe);
			kfree(tp->kprobe);
			tp->kprobe = NULL;
		}

		/* Dump skb work must be done before tp is removed */
		if (print_dump && tp->dump_work.func)
			flush_work(&tp->dump_work);

		if (tp->dump_file) {
			file_close(tp->dump_file);
			tp->dump_file = NULL;
			release_dump_mm(tp);
		}
	}
	kfree(tg);
}

void free_all_group(void)
{
	free_group(all_group);
}
