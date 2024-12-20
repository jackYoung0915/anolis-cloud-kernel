// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/futex.h>
#include <linux/sched/sysctl.h>
#include <linux/tty.h>

#include "sysctl.h"

int vk_init_sysctl_kernel(struct vkernel_sysctl_kernel *k)
{
	u64 threads;
	unsigned long nr_pages = totalram_pages();

	k->nb_mode = NUMA_BALANCING_DISABLED;
	k->nb_promote_rate_limit = 65536;

	k->sched_cfs_bandwidth_slice = 5000UL;
	k->sched_child_runs_first = 0;

	k->sched_dl_period_max = 1 << 22; /* ~4 seconds */
	k->sched_dl_period_min = 100;     /* 100 us */

	k->sched_rr_timeslice = (MSEC_PER_SEC / HZ) * RR_TIMESLICE;
	k->sched_rt_period = 1000000;
	k->sched_rt_runtime = 950000;

	/*
	 * The number of threads shall be limited such that the thread
	 * structures may only consume a small part of the available memory.
	 */
	if (fls64(nr_pages) + fls64(PAGE_SIZE) > 64)
		threads = MAX_THREADS;
	else
		threads = div64_u64((u64) nr_pages * (u64) PAGE_SIZE,
				    (u64) THREAD_SIZE * 8UL);
	if (threads > MAX_THREADS)
		threads = MAX_THREADS;
	k->nr_threads = 0;
	k->max_threads = clamp_t(u64, threads, MIN_THREADS, MAX_THREADS);

	k->key_gc_delay = 5 * 60;
	k->persistent_keyring_expiry = 3 * 24 * 3600; /* Expire after 3 days of non-use */
	k->key_quota_root_maxbytes = 25000000;
	k->key_quota_root_maxkeys = 1000000;
	k->key_quota_maxbytes = 20000;
	k->key_quota_maxkeys = 200;

	k->pty_limit = NR_UNIX98_PTY_DEFAULT;
	k->pty_reserve = NR_UNIX98_PTY_RESERVE;
	k->pty_count = (atomic_t)ATOMIC_INIT(0);

	return 0;
}

void vk_uninit_sysctl_kernel(struct vkernel_sysctl_kernel *k)
{

}

int vkernel_set_sysctl_kernel(struct vkernel_sysctl_kernel *k,
			struct vkernel_sysctl_kernel_desc *desc)
{
	if (desc->numa_balancing >= 0)
		k->nb_mode = desc->numa_balancing;
	if (desc->numa_balancing_promote_rate_limit > 0)
		k->nb_promote_rate_limit = desc->numa_balancing_promote_rate_limit;

	if (desc->sched_cfs_bandwidth_slice)
		k->sched_cfs_bandwidth_slice = desc->sched_cfs_bandwidth_slice;
	if (desc->sched_child_runs_first == 0 ||  desc->sched_child_runs_first == 1)
		k->sched_child_runs_first = desc->sched_child_runs_first;

	if (desc->sched_dl_period_max)
		k->sched_dl_period_max = desc->sched_dl_period_max;
	if (desc->sched_dl_period_min)
		k->sched_dl_period_min = desc->sched_dl_period_min;

	if (desc->sched_rr_timeslice > 0)
		k->sched_rr_timeslice = desc->sched_rr_timeslice;
	if (desc->sched_rt_period > 0)
		k->sched_rt_period = desc->sched_rt_period;
	if (desc->sched_rt_runtime > 0)
		k->sched_rt_runtime = desc->sched_rt_runtime;

	if (desc->max_threads > 0)
		k->max_threads = clamp_t(u64, desc->max_threads, MIN_THREADS, MAX_THREADS);

	if (desc->key_gc_delay)
		k->key_gc_delay = desc->key_gc_delay;
	if (desc->key_persistent_keyring_expiry)
		k->persistent_keyring_expiry = desc->key_persistent_keyring_expiry;
	if (desc->key_quota_root_maxbytes)
		k->key_quota_root_maxbytes = desc->key_quota_root_maxbytes;
	if (desc->key_quota_root_maxkeys)
		k->key_quota_root_maxkeys = desc->key_quota_root_maxkeys;
	if (desc->key_quota_maxbytes)
		k->key_quota_maxbytes = desc->key_quota_maxbytes;
	if (desc->key_quota_maxkeys)
		k->key_quota_maxkeys = desc->key_quota_maxkeys;

	if (desc->pty_limit > 0)
		k->pty_limit = desc->pty_limit;
	if (desc->pty_reserve > 0)
		k->pty_reserve = desc->pty_reserve;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_sysctl_kernel);
