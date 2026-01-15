// SPDX-License-Identifier: GPL-2.0
#ifndef __PROC_MNT_MASK_H__
#define __PROC_MNT_MASK_H__

#define MNT_MASK_PATH_MAX 256
#define MNT_MASK_BUF_MAX 1024

struct mnt_msk {
	struct list_head list;
	char target[MNT_MASK_PATH_MAX];
	char replace[MNT_MASK_PATH_MAX];
	char mnt_path[MNT_MASK_PATH_MAX];
};

struct mnt_msk *mnt_mask(const char *dev_path, const char *mount_path);

#endif
