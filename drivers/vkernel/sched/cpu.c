// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include "sched.h"

int vk_init_cpu_pref(struct vkernel_cpu_pref *cpu)
{
	cpu->policy = SCHED_NORMAL;
	cpu->rr_timeslice_us = 0;
	cpu->wakeup_gran_us = 0;

	return 0;
}

void vk_uninit_cpu_pref(struct vkernel_cpu_pref *cpu)
{

}

int vkernel_set_cpu_pref(struct vkernel *vk, struct vkernel_cpu_desc *desc)
{
	if (desc->policy >= 0)
		vk->cpu_pref.policy = desc->policy;

	if (desc->rr_timeslice_us > 0)
		vk->cpu_pref.rr_timeslice_us = desc->rr_timeslice_us;

	if (desc->wakeup_gran_us > 0)
		vk->cpu_pref.wakeup_gran_us = desc->wakeup_gran_us;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_cpu_pref);
