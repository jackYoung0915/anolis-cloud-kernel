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

int vk_init_sysctl_net(struct vkernel_sysctl_net *net, struct task_struct *tsk);
void vk_uninit_sysctl_net(struct vkernel_sysctl_net *net);

extern int (*tcp_set_default_congestion_control_ptr)(struct net *net, const char *name);

int devconf_proc(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type);
int devconf_forward(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type);
int devconf_flush(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type);

int vk_init_sysctl_vm(struct vkernel_sysctl_vm *vm);
void vk_uninit_sysctl_vm(struct vkernel_sysctl_vm *vm);

void vk_sync_overcommit_as(struct vkernel *vk);

#endif
