/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#ifndef __ASM_X86_VKERNEL_H
#define __ASM_X86_VKERNEL_H

#define sys_call_vk_t	sys_call_ptr_t

DECLARE_PER_CPU(struct task_struct *, current_syscall_task);
DECLARE_PER_CPU(struct vkernel *, current_syscall_vk);

static __always_inline struct task_struct *get_current_syscall_task(void)
{
	return this_cpu_read_stable(current_syscall_task);
}

static __always_inline struct vkernel *get_current_syscall_vk(void)
{
	return this_cpu_read_stable(current_syscall_vk);
}


#endif
