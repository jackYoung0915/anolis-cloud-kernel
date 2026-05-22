// SPDX-License-Identifier: GPL-2.0
/*
 * drop_reason_stat.c
 *
 * Linux kernel module for tracking skb drop reasons.
 * Provides statistics and reason grouping/description via /proc/net/drop_reason_stat.
 *
 * Copyright (C) 2025 ZTE Corporation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/tracepoint.h>
#include <linux/version.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <net/dropreason-core.h>
#include <net/net_namespace.h>

#define PROC_NAME "drop_reason_stat"           /* /proc file name */
#define PROC_DIR  "net"                        /* /proc directory */
#define REASON_NAME_LEN 64                     /* Max reason name length */
#define DROP_REASON_BASE SKB_DROP_REASON_NOT_SPECIFIED

/* Counters for each drop reason. */
unsigned long counters[SKB_DROP_REASON_MAX - DROP_REASON_BASE];

/* Drop reason meta information structure.
 * Contains the reason name, a human-readable description, and a group label.
 */
struct drop_reason_info {
	const char *name;   /* Reason short name */
	const char *desc;   /* Detailed description */
	const char *group;  /* Group/category */
};

/* Drop reason meta information table.
 * The order must match SKB_DROP_REASON_XXX enum values.
 */
static const struct drop_reason_info drop_reason_infos[] = {
	{"NOT_SPECIFIED", "Drop reason not specified", "Other"},
	{"NO_SOCKET", "Socket not found", "Input"},
	{"PKT_TOO_SMALL", "Packet size too small", "Input"},
	{"TCP_CSUM", "TCP checksum error", "TCP"},
	{"SOCKET_FILTER", "Dropped by socket filter", "Input"},
	{"UDP_CSUM", "UDP checksum error", "UDP"},
	{"NETFILTER_DROP", "Dropped by netfilter", "Input"},
	{"OTHERHOST", "Packet not for this host", "Input"},
	{"IP_CSUM", "IP checksum error", "IP"},
	{"IP_INHDR", "IP header error", "IP"},
	{"IP_RPFILTER", "IP rpfilter failed", "IP"},
	{"UNICAST_IN_L2_MULTICAST", "L2 multicast, L3 unicast", "Input"},
	{"XFRM_POLICY", "XFRM policy check failed", "IPSec"},
	{"IP_NOPROTO", "No support for IP protocol", "IP"},
	{"SOCKET_RCVBUFF", "Socket receive buffer full", "Input"},
	{"PROTO_MEM", "Protocol memory limit", "Input"},
	{"TCP_MD5NOTFOUND", "TCP-MD5 not found", "TCP"},
	{"TCP_MD5UNEXPECTED", "Unexpected TCP-MD5", "TCP"},
	{"TCP_MD5FAILURE", "TCP-MD5 failure", "TCP"},
	{"SOCKET_BACKLOG", "Socket backlog full", "Input"},
	{"TCP_FLAGS", "TCP flags invalid", "TCP"},
	{"TCP_ZEROWINDOW", "TCP zero window", "TCP"},
	{"TCP_OLD_DATA", "TCP old data", "TCP"},
	{"TCP_OVERWINDOW", "TCP data out of window", "TCP"},
	{"TCP_OFOMERGE", "TCP OFO merge", "TCP"},
	{"TCP_RFC7323_PAWS", "TCP PAWS check", "TCP"},
	{"TCP_OLD_SEQUENCE", "TCP old sequence", "TCP"},
	{"TCP_INVALID_SEQUENCE", "TCP invalid sequence", "TCP"},
	{"TCP_RESET", "TCP invalid RST", "TCP"},
	{"TCP_INVALID_SYN", "TCP invalid SYN", "TCP"},
	{"TCP_CLOSE", "TCP socket in CLOSE", "TCP"},
	{"TCP_FASTOPEN", "TCP fastopen drop", "TCP"},
	{"TCP_OLD_ACK", "TCP old ACK", "TCP"},
	{"TCP_TOO_OLD_ACK", "TCP too old ACK", "TCP"},
	{"TCP_ACK_UNSENT_DATA", "TCP ACK for unsent data", "TCP"},
	{"TCP_OFO_QUEUE_PRUNE", "TCP OFO queue pruned", "TCP"},
	{"TCP_OFO_DROP", "TCP OFO drop", "TCP"},
	{"IP_OUTNOROUTES", "IP route lookup failed", "IP"},
	{"BPF_CGROUP_EGRESS", "BPF cgroup egress drop", "BPF"},
	{"IPV6DISABLED", "IPv6 disabled", "IPv6"},
	{"NEIGH_CREATEFAIL", "Neighbor create failed", "ARP/NDP"},
	{"NEIGH_FAILED", "Neighbor entry failed", "ARP/NDP"},
	{"NEIGH_QUEUEFULL", "Neighbor queue full", "ARP/NDP"},
	{"NEIGH_DEAD", "Neighbor entry dead", "ARP/NDP"},
	{"TC_EGRESS", "TC egress drop", "TC"},
	{"QDISC_DROP", "Qdisc drop", "Qdisc"},
	{"CPU_BACKLOG", "CPU backlog full", "Input"},
	{"XDP", "Dropped by XDP", "XDP"},
	{"TC_INGRESS", "TC ingress drop", "TC"},
	{"UNHANDLED_PROTO", "Unhandled protocol", "Input"},
	{"SKB_CSUM", "SKB checksum error", "SKB"},
	{"SKB_GSO_SEG", "SKB GSO segmentation error", "SKB"},
	{"SKB_UCOPY_FAULT", "SKB user copy fault", "SKB"},
	{"DEV_HDR", "Device header error", "Driver"},
	{"DEV_READY", "Device not ready", "Driver"},
	{"FULL_RING", "Ring buffer full", "Driver"},
	{"NOMEM", "Out of memory", "Memory"},
	{"HDR_TRUNC", "Header truncated", "Input"},
	{"TAP_FILTER", "TAP filter drop", "Driver"},
	{"TAP_TXFILTER", "TAP TX filter drop", "Driver"},
	{"ICMP_CSUM", "ICMP checksum error", "ICMP"},
	{"INVALID_PROTO", "Invalid protocol", "Input"},
	{"IP_INADDRERRORS", "IP address error", "IP"},
	{"IP_INNOROUTES", "IP no route", "IP"},
	{"PKT_TOO_BIG", "Packet too big", "Input"},
	{"DUP_FRAG", "Duplicate fragment", "IP"},
	{"FRAG_REASM_TIMEOUT", "Fragment reassembly timeout", "IP"},
	{"FRAG_TOO_FAR", "Fragment too far", "IP"},
	{"TCP_MINTTL", "TCP min TTL/hoplimit", "TCP"},
	{"IPV6_BAD_EXTHDR", "IPv6 bad extension header", "IPv6"},
	{"IPV6_NDISC_FRAG", "IPv6 NDISC invalid frag", "IPv6"},
	{"IPV6_NDISC_HOP_LIMIT", "IPv6 NDISC hop limit", "IPv6"},
	{"IPV6_NDISC_BAD_CODE", "IPv6 NDISC bad code", "IPv6"},
	{"IPV6_NDISC_BAD_OPTIONS", "IPv6 NDISC bad options", "IPv6"},
	{"IPV6_NDISC_NS_OTHERHOST", "IPv6 NDISC NS for other host", "IPv6"},
	{"QUEUE_PURGE", "Queue purged", "Other"},
};

/* /proc entry pointer */
static struct proc_dir_entry *proc_entry;

static void drop_reason_trace_probe(void *ignore, struct sk_buff *skb,
				    void *location, enum skb_drop_reason reason);
static void probe_tracepoint(struct tracepoint *tp, void *priv);
static struct tracepoint *find_tracepoint(const char *name);
static int attach_tracepoint(const char *name, void *probe, void *data);
static int detach_tracepoint(const char *name, void *probe, void *data);
static int drop_reason_stat_show(struct seq_file *m, void *v);
static int drop_reason_stat_open(struct inode *inode, struct file *file);

/* Tracepoint callback function.
 * This function is called whenever the kfree_skb tracepoint is hit.
 */
static void drop_reason_trace_probe(void *ignore, struct sk_buff *skb,
				    void *location, enum skb_drop_reason reason)
{
	if (reason >= DROP_REASON_BASE && reason < SKB_DROP_REASON_MAX) {
		/* Increment the counter for reason */
		counters[reason - DROP_REASON_BASE]++;
	}
}

/* Tracepoint lookup and registration helpers.
 * These functions help find and register/unregister tracepoint callbacks.
 */
static struct tracepoint *tp_kfree_skb;

/* Probe each tracepoint and set tp_kfree_skb if the name matches */
static void probe_tracepoint(struct tracepoint *tp, void *priv)
{
	if (strcmp(tp->name, (char *)priv) == 0)
		tp_kfree_skb = tp;
}

/* Find a tracepoint by name */
static struct tracepoint *find_tracepoint(const char *name)
{
	tp_kfree_skb = NULL;
	for_each_kernel_tracepoint(probe_tracepoint, (void *)name);
	return tp_kfree_skb;
}

/* Register a tracepoint callback */
static int attach_tracepoint(const char *name, void *probe, void *data)
{
	struct tracepoint *tp = find_tracepoint(name);

	if (!tp)
		return -ENOENT;
	return tracepoint_probe_register(tp, probe, data);
}

/* Unregister a tracepoint callback */
static int detach_tracepoint(const char *name, void *probe, void *data)
{
	struct tracepoint *tp = find_tracepoint(name);

	if (!tp)
		return -ENOENT;
	return tracepoint_probe_unregister(tp, probe, data);
}

/* drop_reason_stat_show - /proc/net/drop_reason_stat show function.*/
static int drop_reason_stat_show(struct seq_file *m, void *v)
{
	/* Print table header for current statistics */
	seq_printf(m, "%-26s %-8s %-10s %-32s", "REASON", "TOTAL", "GROUP", "DESC");
	seq_putc(m, '\n');
	seq_puts(m, "-----------------------------------------------------------------------------------\n");

	/* Print current statistics for all reasons */
	for (int i = 0; i < SKB_DROP_REASON_MAX - DROP_REASON_BASE; i++) {
		seq_printf(m, "%-26s ", drop_reason_infos[i].name);
		seq_printf(m, "%-8lu %-10s %-32s", counters[i], drop_reason_infos[i].group,
			   drop_reason_infos[i].desc);
		seq_putc(m, '\n');
	}

	return 0;
}

/* drop_reason_stat_open - Open handler for /proc/net/drop_reason_stat.*/
static int drop_reason_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, drop_reason_stat_show, NULL);
}

static const struct proc_ops drop_reason_stat_proc_ops = {
	.proc_open = drop_reason_stat_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* drop_reason_stat_init - Module initialization.
 */
static int __init drop_reason_stat_init(void)
{
	if (attach_tracepoint("kfree_skb", drop_reason_trace_probe, NULL))
		return -EINVAL;

	proc_entry = proc_create(PROC_NAME, 0444, init_net.proc_net, &drop_reason_stat_proc_ops);
	if (!proc_entry) {
		detach_tracepoint("kfree_skb", drop_reason_trace_probe, NULL);
		return -ENOMEM;
	}

	pr_info("drop_reason_stat: module loaded\n");
	return 0;
}

/* drop_reason_stat_exit - Module cleanup.
 * Removes proc entry, unregisters tracepoint, frees per-CPU counters and deletes the timer.
 */
static void __exit drop_reason_stat_exit(void)
{
	proc_remove(proc_entry);
	detach_tracepoint("kfree_skb", drop_reason_trace_probe, NULL);
	pr_info("drop_reason_stat: module unloaded\n");
}

module_init(drop_reason_stat_init);
module_exit(drop_reason_stat_exit);

/* MODULE_LICENSE, MODULE_AUTHOR, MODULE_DESCRIPTION:
 *   - Standard kernel macros for module metadata.
 *   - Required for proper kernel module loading and information display.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE");
MODULE_DESCRIPTION("/proc/net/drop_reason_stat: skb drop reason statistics");
