// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/slab.h>
#include "help.h"

#include "utils.h"
#include "kprobe.h"
#include "parser.h"
#include "group.h"

/*param for package filter*/
MODULE_PARM_DESC(saddr, "filter source ip address,e.g. insmod nettrace.ko saddr=172.16.6.62");
PARAM_STRING_NAMED(filter_saddr, saddr, NULL);
MODULE_PARM_DESC(daddr, "filter destination ip address,e.g. insmod nettrace.ko daddr=172.16.6.74");
PARAM_STRING_NAMED(filter_daddr, daddr, NULL);
MODULE_PARM_DESC(addr, "filter source or destination ip address,e.g. insmod nettrace.ko addr=192.168.2.11");
PARAM_STRING_NAMED(filter_addr, addr, NULL);
MODULE_PARM_DESC(proto, "filter 3 layer or 4 layer net protocol,e.g. insmod nettrace.ko proto=arp");
PARAM_STRING_NAMED(filter_proto, proto, NULL);
MODULE_PARM_DESC(port, "filter source port or destination port, e.g. insmod nettrace.ko port=1234");
PARAM_INT_NAMED(filter_port, port, -1);
MODULE_PARM_DESC(sport, "filter source port,e.g. insmod nettrace.ko port=1234");
PARAM_INT_NAMED(filter_sport, sport, -1);
MODULE_PARM_DESC(dport, "filter destination port,e.g. insmod nettrace.ko port=1234");
PARAM_INT_NAMED(filter_dport, dport, -1);

MODULE_PARM_DESC(trace, "a trace is a serial of kernel tracing for special scene, e.g. insmod nettrace.ko trace=link");
PARAM_STRING_ARRAY(filter_trace, trace, 10);
MODULE_PARM_DESC(probe, "this is a list of kernel function where you want to dump network");
PARAM_STRING_ARRAY(filter_probe, probe, 10);

MODULE_PARM_DESC(stack, "print the kernel function call stack,e.g. insmod nettrace.ko stack=1");
PARAM_INT_NAMED(print_stack, stack, 0);
MODULE_PARM_DESC(ustack, "print the user space call stack,e.g. insmod nettrace.ko ustack=1");
PARAM_INT_NAMED(print_ustack, ustack, 0);

MODULE_PARM_DESC(output, "three kind of output supported: ftrace, kernel and file, e.g. insmod nettrace.ko output=ftrace");
PARAM_STRING_NAMED(print_output, output, NULL);
MODULE_PARM_DESC(dump, "the directory where you want to put pcap file in. e.g. insmod nettrace.ko trace=error dump=./output");
PARAM_STRING_NAMED(print_dump, dump, NULL);

MODULE_PARM_DESC(flag, "addition flags supported values v: print addition info, e.g. insmod nettrace.ko flag=v");
PARAM_STRING_NAMED(param_flag, flag, NULL);
MODULE_PARM_DESC(mm, "the number of pages reserved for each TP during the initialization phase");
PARAM_INT_NAMED(param_mm, mm, 100);

enum output_type op_type;
int if_print_info;

struct print_entry {
	struct list_head list;
	char *msg;
};

static struct work_struct print_work;
static LIST_HEAD(print_list);
static spinlock_t print_lock;

static struct file *output_file;
static char output_index[64];

static void print_process(struct work_struct *work)
{
	struct print_entry *file, *next;
	unsigned long flag;
	LIST_HEAD(head);

	spin_lock_irqsave(&print_lock, flag);
	list_splice_init(&print_list, &head);
	spin_unlock_irqrestore(&print_lock, flag);

	list_for_each_entry_safe(file, next, &head, list) {
		file_append(output_file, file->msg, (unsigned int) strlen(file->msg));

		kfree(file->msg);
		kfree(file);
	}
}

int init_output_file(char *path)
{
	output_file = file_create(path);

	if (output_file == NULL)
		return -1;

	INIT_WORK(&print_work, print_process);
	spin_lock_init(&print_lock);

	return 0;
}

static void print_enqueue(char *fmt, va_list ap)
{
	char buf[MAX_LOG_BUF];
	char *print_buf;
	struct print_entry *print_file;
	unsigned long flag;

	vsnprintf(buf, sizeof(buf), fmt, ap);
	print_buf = kcalloc(1, strlen(buf) + 1, GFP_KERNEL);
	if (!print_buf)
		return;

	strscpy(print_buf, buf, strlen(buf) + 1);

	print_file = kcalloc(1, sizeof(struct print_entry), GFP_KERNEL);
	if (!print_file) {
		kfree(print_buf);
		return;
	}

	print_file->msg = print_buf;

	spin_lock_irqsave(&print_lock, flag);
	list_add_tail(&print_file->list, &print_list);
	spin_unlock_irqrestore(&print_lock, flag);

	schedule_work(&print_work);
}

void log_base(char *fmt, ...)
{

	va_list argptr;

	va_start(argptr, fmt);

	switch (op_type) {
	default:
		vprintk(fmt, argptr);
		break;
	case OUTPUT_FTRACE:
		vprintk(fmt, argptr);
		break;
	case OUTPUT_FILE:
		print_enqueue(fmt, argptr);
		break;
	}

	va_end(argptr);
}

static void print_help(void)
{
	log_data("============================Netdump========================\n");
	log_data("\n");
	log_data("Welcome to use netdump! This is a tool based on kprobe for\n");
	log_data("network bag grab in kernel.\n");
	log_data("\n");
	log_data("Basic usage:\n");
	log_data("\n");
	log_data("    insmod netdump.ko [probe=<kernel function list>]\n");
	log_data("    [output=<output type>] [flag=]\n");
	log_data("    [dump=<output dir>]\n");
	log_data("    [trace=<trace scene>] [proto=]\n");
	log_data("    [saddr=] [daddr=] [addr=]\n");
	log_data("    [sport=] [dport=] [port=]\n");
	log_data("    [stack=<0 or 1>] [ustack=<0 or 1>]\n");
	log_data("\n");
	log_data("trace: a trace is a serial of kernel tracing for special scene.\n");
	log_data("    We support various of trace, such ip, tcp, macvlan, etc.\n");
	log_data("    Trace can have children trace, use 'trace=?' to see all supported\n");
	log_data("    trace.\n");
	log_data("\n");
	log_data("probe: this is a list of kernel function where you want\n");
	log_data("    to dump network package info. Use 'probe=?' to see all\n");
	log_data("    supported kernel functions.\n");
	log_data("\n");
	log_data("dump: the directory where you want to put pcap file in. Once this\n");
	log_data("    option is set, all package filtered will be saved.\n");
	log_data("\n");
	log_data("output: three kind of output supported: ftrace, kernel and file.\n");
	log_data("    When comes up with file, it should be a file path, such as /ntrace.log.\n");
	log_data("\n");
	log_data("flag: addition flags. Supported values:\n");
	log_data("    v: print addition info.\n");
	log_data("\n");
	log_data("mm: the number of pages reserved for each TP during the initialization phase,\n");
	log_data("    which is used for temporarily caching messages.\n");
	log_data("    [mm=100] means that each TP initialization will reserve 2 * 100 pages,\n");
	log_data("    100 for storing SKBs, and 100 for storing data.");
	log_data("\n");

	log_data("stack: print the kernel function call stack.\n");
	log_data("ustack: print the user spack call stack.\n");
	log_data("\n");
	log_data("-----------------------package filter-------------------\n");
	log_data("saddr: source ip addr\n");
	log_data("daddr: dest ip addr\n");
	log_data("addr: source or dest ip addr\n");
	log_data("\n");
	log_data("sport: source udp or tcp port\n");
	log_data("dport: dest udp or tcp port\n");
	log_data("port: source or dest udp or tcp port\n");
	log_data("proto: the network protocol\n");

	log_data("\n");
	log_data("\n");
	log_data("============================Netdump========================\n");
}

static void print_group(struct trace_group *tg)
{
	struct trace_group	*tmp_tg;
	char	tab[] = "    ";
	ulong	cur_len = strlen(output_index);

	log_data("%s%s: %s\n", output_index, tg->name, tg->desc);
	if (cur_len + sizeof(tab) > sizeof(output_index))
		return;
	strscpy(output_index + cur_len, tab, strlen(tab) + 1);
	list_for_each_entry(tmp_tg, &tg->groups, list) {
		print_group(tmp_tg);
	}
	output_index[cur_len] = '\0';
}

static void print_trace(void)
{
	memset(output_index, 0, sizeof(output_index));
	print_group(all_group);
}

static void print_probe(void)
{
	struct trace_point *tp;
	int i = 0;

	log_data("all supported kprobe:\n\n");
	for_each_tp(i, tp) {
		log_data("\t%s\n", tp->name);
	}
}

static int init_rule(void)
{
	COMMON_RULE *rule;
	u32 addr_i	= 0;
	int proto3	= 0;
	int proto4	= 0;

	rule = kcalloc(1, sizeof(COMMON_RULE), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	INIT_LIST_HEAD(&rule->list);
	rule->s_mask = rule->d_mask = 0xffffffff;

	if (filter_addr) {
		if (ip2i(filter_addr, &addr_i)) {
			log_err("ip addr format error!\n");
			goto error;
		}
		addr_i = htonl(addr_i);
		rule->saddr = addr_i;
		rule->__flags |= FLAG_addr;
	}

	if (filter_saddr) {
		if (ip2i(filter_saddr, &addr_i)) {
			log_err("ip addr format error!\n");
			goto error;
		}
		addr_i = htonl(addr_i);
		SET_RULE_FLAGS(rule, saddr, addr_i);
	}

	if (filter_daddr) {
		if (ip2i(filter_daddr, &addr_i)) {
			log_err("ip addr format error!\n");
			goto error;
		}
		addr_i = htonl(addr_i);
		SET_RULE_FLAGS(rule, daddr, addr_i);
	}

	if (filter_sport > 0xffff || filter_dport > 0xffff || filter_port > 0xffff) {
		log_err("port range error!\n");
		goto error;
	}

	if (filter_port > 0) {
		filter_port = (int) htons((u16) filter_port);
		rule->sport = filter_port;
		rule->__flags |= FLAG_port;
	}

	if (filter_sport > 0) {
		filter_sport = (int) htons((u16) filter_sport);
		SET_RULE_FLAGS(rule, sport, filter_sport);
	}

	if (filter_dport > 0) {
		filter_dport = (int) htons((u16) filter_dport);
		SET_RULE_FLAGS(rule, dport, filter_dport);
	}

	if (filter_proto) {
		proto3 = str2proto3(filter_proto);
		proto4 = str2proto4(filter_proto);
		if (proto3 >= 0) {
			SET_RULE_FLAGS(rule, proto_3, proto3);
		} else if (proto4 >= 0) {
			SET_RULE_FLAGS(rule, proto_4, proto4);
		} else {
			log_err("proto not found!\n");
			goto error;
		}
	}

	if (add_rule(rule))
		kfree(rule);

	return 0;
error:
	kfree(rule);
	return -EINVAL;
}

int init_args(void)
{
	int err = -EINVAL;

	if (print_output != NULL) {
		if (streq(print_output, "ftrace"))
			op_type = OUTPUT_FTRACE;
		else if (streq(print_output, "kernel"))
			op_type = OUTPUT_KERNEL;
		else if (!init_output_file(print_output))
			op_type = OUTPUT_FILE;
		else {
			log_err("output type error!\n");
			goto error;
		}
	}

	if (print_dump != NULL) {
		err = access_path(print_dump);
		if (err) {
			log_err("[nettrace] access_path err:%d\n", err);
			log_err("[nettrace] failed to access the dump output directory: %s\n",
					print_dump);
			goto error;
		}
	}

	if (filter_trace_len == 0 && filter_probe_len == 0) {
		pr_alert("Don't worry. execute 'dmesg' and see usage of nettrace.ko\n");
		print_help();
		goto error;
	}

	if (filter_trace_len == 1 && streq(filter_trace[0], "?")) {
		pr_alert("Execute 'dmesg' and see available parameters of 'trace='\n");
		print_trace();
		goto error;
	}

	if (filter_probe_len == 1 && streq(filter_probe[0], "?")) {
		pr_alert("Execute 'dmesg' and see available parameters of 'probe='\n");
		print_probe();
		goto error;
	}

	if (param_flag) {
		char *tmp_flag;

		while ((tmp_flag = strsep(&param_flag, ",")) != NULL) {
			switch (*tmp_flag) {
			case 'v':
				if_print_info = 1;
				break;
			default:
				log_err("flags:%c not supported\n", *tmp_flag);
				goto error;
			}
		}
	}

	if (init_rule())
		goto error;

	return 0;
error:
	return err;
}
