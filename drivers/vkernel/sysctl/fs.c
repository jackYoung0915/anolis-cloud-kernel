// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/mm.h>
#include <linux/swap.h>

#include "sysctl.h"

int vk_init_sysctl_fs(struct vkernel_sysctl_fs *fs)
{
	unsigned long n;
	unsigned long nr_pages = totalram_pages();
	unsigned long memreserve = (nr_pages - nr_free_pages()) * 3/2;

	memreserve = min(memreserve, nr_pages - 1);
	n = ((nr_pages - memreserve) * (PAGE_SIZE / 1024)) / 10;
	fs->files_stat.max_files = max_t(unsigned long, n, NR_FILE);
	fs->nr_open = 1024 * 1024;
	if (percpu_counter_init(&fs->nr_files, 0, GFP_KERNEL)) {
		pr_err("vkernel: failed to init sysctl_fs nr_files\n");
		return -ENOMEM;
	}

	fs->nr_inodes = alloc_percpu_gfp(unsigned long, GFP_KERNEL);
	if (!fs->nr_inodes) {
		pr_err("vkernel: failed to alloc sysctl_fs nr_inodes\n");
		return -ENOMEM;
	}
	fs->nr_unused = alloc_percpu_gfp(unsigned long, GFP_KERNEL);
	if (!fs->nr_unused) {
		pr_err("vkernel: failed to alloc sysctl_fs nr_unused\n");
	return -ENOMEM;
	}

	fs->leases_enable = 1;
	fs->lease_break_time = 45;

	fs->mount_max = 100000;

	return 0;
}

void vk_uninit_sysctl_fs(struct vkernel_sysctl_fs *fs)
{
	if (fs->nr_inodes)
		free_percpu(fs->nr_inodes);
	if (fs->nr_unused)
		free_percpu(fs->nr_unused);

	percpu_counter_destroy(&fs->nr_files);
}

int vkernel_set_sysctl_fs(struct vkernel_sysctl_fs *fs, struct vkernel_sysctl_fs_desc *desc)
{
	if (desc->file_max)
		fs->files_stat.max_files = desc->file_max;
	if (desc->nr_open)
		fs->nr_open = desc->nr_open;

	if (desc->leases_enable == 0 || desc->leases_enable == 1)
		fs->leases_enable = desc->leases_enable;
	if (desc->lease_break_time > 0)
		fs->lease_break_time = desc->lease_break_time;

	if (desc->mount_max)
		fs->mount_max = desc->mount_max;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_sysctl_fs);
