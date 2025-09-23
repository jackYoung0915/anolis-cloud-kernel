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

#ifndef NETDUMP_HELP_H
#define NETDUMP_HELP_H

#include <linux/printk.h>
#include <linux/kernel.h>

#define MAX_LOG_BUF 200

typedef enum {
	OUTPUT_KERNEL,
	OUTPUT_FTRACE,
	OUTPUT_FILE
} output_type;

extern char *output_file_path;
extern output_type op_type;
extern int	if_print_info;

extern char *filter_saddr;
extern char *filter_daddr;
extern char *filter_addr;
extern char *filter_proto;

extern int	filter_port;
extern int	filter_sport;
extern int	filter_dport;

extern char	*filter_trace[];
extern char	*filter_probe[];
extern int	filter_trace_len;
extern int	filter_probe_len;

extern int	print_stack;
extern int	print_ustack;

extern char	*print_output;
extern char	*print_dump;
extern char	*param_flag;
extern int      param_mm;

extern int init_output_file(char *path);

extern void log_base(char *fmt, ...);

extern int init_args(void);

#define log_info(fmt, args...)  {if(if_print_info)\
    log_base(fmt, ##args);}
#define log_data(fmt, args...)  log_base(fmt, ##args)
#define log_err(fmt, args...)  log_base(fmt, ##args)
#define log_debug(fmt, args...)  log_base(fmt, ##args)

static inline void print_leave(void) {
	log_info("you just exited netdump, welcome back~\n");
}

#endif //NETDUMP_HELP_H
