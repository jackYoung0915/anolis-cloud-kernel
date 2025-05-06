/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_FS_H
#define _VKERNEL_FS_H

#include <linux/vkernel.h>

int vk_acl_init(void);
void vk_acl_uninit(void);

int vk_init_acl(struct vkernel_acl *acl, unsigned int bits);
void vk_uninit_acl(struct vkernel_acl *acl);
int vkernel_set_default_acl_set(struct vkernel_acl *acl);

int vk_generic_permission(struct vkernel *vk, struct mnt_idmap *idmap,
			struct inode *inode, int mask);

#endif
