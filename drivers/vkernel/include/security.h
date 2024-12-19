/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_SECURITY_H
#define _VKERNEL_SECURITY_H

#include <linux/vkernel.h>

int vk_cap_init(void);
void vk_cap_uninit(void);

int vk_cap_capable(struct vkernel *vk, const struct cred *cred,
		struct user_namespace *targ_ns,
		int cap, unsigned int opts);

#endif
