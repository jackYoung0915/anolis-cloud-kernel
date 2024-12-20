/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_SYSCTL_H
#define _VKERNEL_SYSCTL_H

#include <linux/vkernel.h>
#include <linux/ipc_namespace.h>

#define IPC_SEM_IDS	0
#define IPC_MSG_IDS	1
#define IPC_SHM_IDS	2

/* defined at kernel/fork.c */
#define MIN_THREADS 20
#define MAX_THREADS FUTEX_TID_MASK

int vk_init_sysctl_fs(struct vkernel_sysctl_fs *fs);
void vk_uninit_sysctl_fs(struct vkernel_sysctl_fs *fs);

int vk_init_sysctl_kernel(struct vkernel_sysctl_kernel *k);
void vk_uninit_sysctl_kernel(struct vkernel_sysctl_kernel *k);

#endif
