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

#include <linux/stacktrace.h>
#include <linux/printk.h>
#include <linux/udp.h>
#include "core.h"
#include "help.h"
#include "group.h"
#include "procfs.h"

enum nettrace_status nt_status = NT_INIT;

#define MAX_DUMP_QUEUE_MEM_DEFAULT	(SK_RMEM_MAX * 200)
int max_dump_queue_mem = MAX_DUMP_QUEUE_MEM_DEFAULT;
#define MAX_DUMP_SKB_CNT_DEFAULT (100 * 1000)
unsigned int max_dump_skb_cnt = MAX_DUMP_SKB_CNT_DEFAULT;
#define MAX_DUMP_FILE_SIZE_DEFAULT (100 * 1024 * 1024)
unsigned int max_dump_file_size = MAX_DUMP_FILE_SIZE_DEFAULT;

MODULE_DESCRIPTION("Network debugging tools    \n \
		insmod nettrace.ko \[probe=\<kernel function list\>\] \n \
		\[output=\<output type\>\] \[flag=\] \n \
		\[dump=\<output dir\>\]            \n \
		\[trace=\<trace scene\>\] \[proto=\] \n \
		\[saddr=\] \[daddr=\] \[addr=\]      \n \
		\[sport=\] \[dport=\] \[port=\]      \n \
		\[stack=\<0 or 1\>\] \[ustack=\<0 or 1\>\] \[mm=\]");
/*module init.*/
KPROBE_INIT {
	int err = -EINVAL;

	init_group();
	err = init_args();
	if (err)
		goto on_err;

	err = trace_register();
	if (err)
		goto on_init_err;

	err = ntrace_proc_init();
	if (err)
		goto on_init_err;

	WRITE_ONCE(nt_status, NT_RUNNING);
	return 0;

on_init_err:
	free_all_group();
on_err:
	return err;
}

/*module exit*/
KPROBE_EXIT {
	WRITE_ONCE(nt_status, NT_EXITING);
	/* Now start to free all tracepoints */
	free_all_group();
	free_rules();
	ntrace_proc_exit();
}
