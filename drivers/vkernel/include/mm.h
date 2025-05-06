/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_MM_H
#define _VKERNEL_MM_H

#include <linux/vkernel.h>

/* Copy from mm/shmem.c */

#define SHMEM_HUGE_NEVER	0
#define SHMEM_HUGE_ALWAYS	1
#define SHMEM_HUGE_WITHIN_SIZE	2
#define SHMEM_HUGE_ADVISE	3

/*
 * Special values.
 * Only can be set via /sys/kernel/mm/transparent_hugepage/shmem_enabled:
 *
 * SHMEM_HUGE_DENY:
 *	disables huge on shm_mnt and all mounts, for emergency use;
 * SHMEM_HUGE_FORCE:
 *	enables huge on shm_mnt and all mounts, w/o needing option, for testing;
 *
 */
#define SHMEM_HUGE_DENY		(-1)
#define SHMEM_HUGE_FORCE	(-2)

int vk_init_memory_pref(struct vkernel_mem_pref *mem);
void vk_uninit_memory_pref(struct vkernel_mem_pref *mem);

#endif
