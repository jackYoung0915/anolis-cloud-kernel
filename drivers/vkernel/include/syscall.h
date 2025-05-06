/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_SYSCALL_H
#define _VKERNEL_SYSCALL_H

#include <linux/vkernel.h>

extern sys_call_vk_t *sys_call_table_ptr;

int vk_syscall_init(void);
void vk_syscall_uninit(void);

long vk_sys_ni_syscall(const struct pt_regs *regs);
long vk_sys_forbid_syscall(const struct pt_regs *regs);
long vk_sys_ni_cond_syscall(const struct pt_regs *regs);
long vk_sys_forbid_cond_syscall(const struct pt_regs *regs);

int vk_init_syscall(struct vkernel_syscall *syscall);
void vk_uninit_syscall(struct vkernel_syscall *syscall);
void vk_install_default_syscalls(struct vkernel_syscall *syscall);

extern struct vkernel_custom_type analysis_custom;

#endif
