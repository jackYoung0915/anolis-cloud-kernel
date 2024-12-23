/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_SCHED_H
#define _VKERNEL_SCHED_H

#include <linux/vkernel.h>

int vk_init_cpu_pref(struct vkernel_cpu_pref *cpu);
void vk_uninit_cpu_pref(struct vkernel_cpu_pref *cpu);

#endif
