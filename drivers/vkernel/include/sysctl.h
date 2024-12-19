/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_SYSCTL_H
#define _VKERNEL_SYSCTL_H

#include <linux/vkernel.h>

int vk_init_sysctl_fs(struct vkernel_sysctl_fs *fs);
void vk_uninit_sysctl_fs(struct vkernel_sysctl_fs *fs);

#endif
