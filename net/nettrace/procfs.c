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

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/kprobes.h>
#include <net/snmp.h>

#include "procfs.h"
#include "group.h"
#include "dump.h"

static struct proc_dir_entry *ntrace_proc;

static int kprobe_tp_info_seq_show(struct seq_file *seq, void *v)
{
	TRACE_POINT *tp;
	int i = 0;

	seq_printf(seq, "current registered kernel function:\n");
	for_each_tp(i, tp) {
		if (!tp->kprobe)
			continue;
		seq_printf(seq, "%30s:%p\n", tp->name, tp->kprobe->addr);
	}

	return 0;
}

static int kprobe_tp_info_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, kprobe_tp_info_seq_show, NULL);
}

static const struct proc_ops kprobe_tp_info_ops = {
	.proc_open	= kprobe_tp_info_seq_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int nettrace_statistics_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "The total dump_loss: %u\n", dump_loss_due_to_no_memory +
					dump_skb_over_cnt + dump_skb_over_size);
	seq_printf(seq, "dump_queue_no_memory: %u\n", dump_loss_due_to_no_memory);
	seq_printf(seq, "dump_file_over_cnt: %u\n", dump_skb_over_cnt);
	seq_printf(seq, "dump_file_over_size:%u\n", dump_skb_over_size);

	return 0;
}

static int nettrace_statistics_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, nettrace_statistics_seq_show, NULL);
}

static const struct proc_ops nettrace_statistics_ops = {
	.proc_open      = nettrace_statistics_seq_open,
	.proc_read      = seq_read,
	.proc_lseek     = seq_lseek,
	.proc_release   = seq_release,
};

static int max_dump_skb_cnt_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%d\n", max_dump_skb_cnt);
	return 0;
}

static int max_dump_skb_cnt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, max_dump_skb_cnt_seq_show, NULL);
}

static ssize_t max_dump_skb_cnt_proc_write(struct file *file,
		const char __user *buf, size_t count, loff_t *pos)
{
	char buffer[32];
	int temp_value;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count)) {
		err = -EFAULT;
		goto out;
	}

	err = kstrtoint(strstrip(buffer), 0, &temp_value);
	if (err)
		goto out;
	if (temp_value < 0) {
		err = -EINVAL;
		goto out;
	}

	max_dump_skb_cnt = temp_value;
out:
	return err < 0 ? err : count;
}

static const struct proc_ops max_dump_skb_cnt_ops = {
	.proc_open  = max_dump_skb_cnt_proc_open,
	.proc_read  = seq_read,
	.proc_write = max_dump_skb_cnt_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

static int max_dump_file_size_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%d\n", max_dump_file_size);
	return 0;
}

static int max_dump_file_size_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, max_dump_file_size_seq_show, NULL);
}

static ssize_t max_dump_file_size_proc_write(struct file *file,
		const char __user *buf, size_t count, loff_t *pos)
{
	char buffer[32];
	int temp_value;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count)) {
		err = -EFAULT;
		goto out;
	}

	err = kstrtoint(strstrip(buffer), 0, &temp_value);
	if (err)
		goto out;
	if (temp_value < 0) {
		err = -EINVAL;
		goto out;
	}

	max_dump_file_size = temp_value;
out:
	return err < 0 ? err : count;
}

static const struct proc_ops max_dump_file_size_ops = {
	.proc_open  = max_dump_file_size_proc_open,
	.proc_read  = seq_read,
	.proc_write = max_dump_file_size_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

int __net_init ntrace_proc_init(void)
{

	ntrace_proc = proc_mkdir("ntrace", init_net.proc_net);

	if (ntrace_proc == NULL)
		return -ENOMEM;

	if (!proc_create("kprobe", S_IRUGO, ntrace_proc, &kprobe_tp_info_ops))
		goto err_rmdir_ntrace;
	if (!proc_create("max_dump_skb_cnt", 0644, ntrace_proc, &max_dump_skb_cnt_ops))
		goto err_rmdir_ntrace;
	if (!proc_create("max_dump_file_size", 0644, ntrace_proc, &max_dump_file_size_ops))
		goto err_rmdir_ntrace;
	if (!proc_create("statistics", 0444, ntrace_proc, &nettrace_statistics_ops))
		goto err_rmdir_ntrace;

	return 0;

err_rmdir_ntrace:
	remove_proc_subtree("ntrace", init_net.proc_net);
	return -ENOMEM;
}

void __net_exit ntrace_proc_exit(void)
{
	remove_proc_entry("kprobe", ntrace_proc);
	proc_remove(ntrace_proc);
}
