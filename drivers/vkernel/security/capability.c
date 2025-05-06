// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/prctl.h>
#include <linux/security.h>

#include "security.h"
#include "utils.h"

int (*cap_capget_ptr)(struct task_struct *target, kernel_cap_t *effective,
			   kernel_cap_t *inheritable, kernel_cap_t *permitted);
int (*cap_capset_ptr)(struct cred *new, const struct cred *old,
			   const kernel_cap_t *effective,
			   const kernel_cap_t *inheritable,
			   const kernel_cap_t *permitted);
int (*cap_task_prctl_ptr)(int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5);

int vk_cap_init(void)
{
	cap_capget_ptr = (void *)lookup_name("cap_capget");
	cap_capset_ptr = (void *)lookup_name("cap_capset");
	cap_task_prctl_ptr = (void *)lookup_name("cap_task_prctl");
	if (!cap_capget_ptr || !cap_capset_ptr || !cap_task_prctl_ptr) {
		pr_err("failed to find cap symbols, get: %p, set: %p, prctl: %p\n",
				cap_capget_ptr, cap_capset_ptr, cap_task_prctl_ptr);
		return -1;
	}

	return 0;
}

void vk_cap_uninit(void) {}

int vk_cap_capable(struct vkernel *vk, const struct cred *cred, struct user_namespace *ns,
		int cap, unsigned int opts)
{
	/* Check cred and real_cred to allow fs overried_creds */
	if (current_cred() == current_real_cred() &&
	    !cap_issubset(cred->cap_effective, vk->linux_cap.effective)) {
		pr_debug("vkernel: cap eff %llx escalated? use vk eff %llx instead\n",
				cred->cap_effective.val, vk->linux_cap.effective.val);
		for (;;) {
			if (ns == cred->user_ns)
				return cap_raised(vk->linux_cap.effective, cap) ? 0 : -EPERM;
			if (ns->level <= cred->user_ns->level)
				return -EPERM;
			if ((ns->parent == cred->user_ns) && uid_eq(ns->owner, cred->euid))
				return 0;
			ns = ns->parent;
		}
	}
	return 0;
}

/*
 * Set cap for `current`, and `current` should be vk->init_process
 *
 * Note: this operation will take effect immediately.
 */
int vkernel_set_linux_cap(struct vkernel *vk, struct vkernel_linux_cap *cap)
{
	kernel_cap_t effective, inheritable, permitted;
	struct cred *cred;
	int action;
	int ret;
	int i;

	vk->linux_cap = *cap;

	/* Get current [effective,inheritable,permitted] */
	cap_capget_ptr(vk->init_process, &effective, &inheritable, &permitted);

	/* Drop bset according to linux_cap, which affects the following capset */
	if (cap_raised(effective, CAP_SETPCAP)) {
		for (i = 0; i <= CAP_LAST_CAP; i++) {
			if (!cap_raised(cap->bset, i)) {
				ret = cap_task_prctl_ptr(PR_CAPBSET_DROP, i, 0, 0, 0);
				if (ret)
					return ret;
			}
		}
	}

	/* Set current [effective,inheritable,permitted], ambient is automatically updated */
	cred = prepare_creds();
	if (!cred)
		return -ENOMEM;
	ret = cap_capset_ptr(cred, current_cred(), &cap->effective, &cap->inheritable,
			      &cap->permitted);
	if (ret)
		return ret;
	commit_creds(cred);

	/* Raise or lower abmient according to linux_cap */
	for (i = 0; i < CAP_LAST_CAP; i++) {
		if (cap_raised(cap->ambient, i))
			action = PR_CAP_AMBIENT_RAISE;
		else
			action = PR_CAP_AMBIENT_LOWER;
		ret = cap_task_prctl_ptr(PR_CAP_AMBIENT, action, i, 0, 0);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(vkernel_set_linux_cap);
