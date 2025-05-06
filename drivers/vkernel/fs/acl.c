// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/namei.h>

#include "fs.h"

static char *def_path[] = {
	/* open, access, append, read, exec */
	"/proc/sys/abi",
	"/proc/sys/debug",
	"/proc/sys/dev",
	"/proc/sys/fs",
	"/proc/sys/net",
	"/proc/sys/user",
	"/proc/sys/vm",
	/* open, read, exec */
	"/sys/kernel",
	"/sys/power",
	"/sys/class",
	"/sys/devices",
	"/sys/dev",
	"/sys/hypervisor",
	"/sys/bus",
	"/sys/block",
	"/sys/module",
	"/sys/firmware",
	"/sys/fs/ecryptfs",
	"/sys/fs/pstore",
	"/sys/fs/bpf",
	"/sys/fs/fuse",
	"/sys/fs/ext4",
	/* open */
	"/proc/sysrq-trigger",
	"/sys/kernel/security",
	/* nop */
	"/sys/fs/cgroup",
	"/dev/vkernel",
};

static unsigned short def_mode[] = {
	0x803d, 0x803d, 0x803d, 0x803d, 0x803d, 0x803d, 0x803d, 0x8025,
	0x8025, 0x8025, 0x8025, 0x8025, 0x8025, 0x8025, 0x8025, 0x8025,
	0x8025, 0x8024, 0x8024, 0x8024, 0x8024, 0x8024, 0x8020, 0x8020,
	0x0000, 0x0000,
};

static struct kmem_cache *acl_node_cache;

int vk_acl_init(void)
{
	acl_node_cache = kmem_cache_create("vkernel_acl_node",
			sizeof(struct vkernel_acl_node), 0, SLAB_ACCOUNT, NULL);
	if (!acl_node_cache) {
		pr_err("failed to create slab for acl node\n");
		return -ENOMEM;
	}

	return 0;
}

void vk_acl_uninit(void)
{
	kmem_cache_destroy(acl_node_cache);
}

int vk_init_acl(struct vkernel_acl *acl, unsigned int bits)
{

	acl->ht = kcalloc(
		1UL << bits, sizeof(struct hlist_head), GFP_KERNEL);
	if (!acl->ht)
		return -ENOMEM;

	acl->bits = bits;
	INIT_LIST_HEAD(&acl->nodes);
	acl->active = false;

	return 0;
}

void vk_uninit_acl(struct vkernel_acl *acl)
{
	struct hlist_head *ht = acl->ht;
	struct vkernel_acl_node *node;
	struct vkernel_acl_node *tmp;

	if (!acl->ht || !acl->bits)
		return;

	acl->active = false;
	list_for_each_entry_safe(node, tmp, &acl->nodes, link) {
		if (!hlist_unhashed(&node->hash))
			hlist_del(&node->hash);
		list_del(&node->link);
		kmem_cache_free(acl_node_cache, node);
	}
	INIT_LIST_HEAD(&acl->nodes);

	acl->bits = 0;
	kfree(ht);
}

/* inode hash, copy from inode.c */
static unsigned long inode_hash(struct inode *inode, unsigned long shift)
{
	struct super_block *sb = inode->i_sb;
	unsigned long hashval = inode->i_ino;
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) /
			L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> shift);
	return tmp;
}

static struct vkernel_acl_node *vk_acl_node_get(struct vkernel_acl *acl, struct inode *inode)
{
	struct hlist_head *ht = acl->ht;
	struct vkernel_acl_node *node;
	unsigned long key = inode_hash(inode, acl->bits);

	hlist_for_each_entry(node, &ht[hash_min(key, acl->bits)], hash) {
		if (inode->i_ino == node->ino && inode->i_sb == node->sb)
			return node;
	}

	return NULL;
}

static int vk_acl_node_del(struct vkernel_acl *acl, struct inode *inode)
{
	struct vkernel_acl_node *node;

	node = vk_acl_node_get(acl, inode);
	if (!node)
		return -1;

	hlist_del(&node->hash);
	node->ino = 0;
	node->sb = NULL;

	return 0;
}

static int vk_acl_node_add(struct vkernel_acl *acl, struct inode *inode,
			struct vkernel_acl_node *node)
{
	struct hlist_head *ht = acl->ht;
	unsigned long key = inode_hash(inode, acl->bits);

	/* Remove old rule if exists */
	vk_acl_node_del(acl, inode);

	node->ino = inode->i_ino;
	node->sb = inode->i_sb;
	hlist_add_head(&node->hash, &ht[hash_min(key, acl->bits)]);

	return 0;
}

/*
 * Inode from file->f_inode may be destroyed at following access
 * Using kern_path is also unstable, is there a better way?
 */
static struct inode *kern_path_to_inode(const char *filename)
{
	struct path path;
	struct inode *inode;
	int ret;

	ret = kern_path(filename, LOOKUP_FOLLOW | LOOKUP_OPEN, &path);
	if (ret)
		return NULL;

	inode = path.dentry->d_inode;
	path_put(&path);

	return inode;
}

static int vk_activate_acl(struct vkernel_acl *acl, struct vkernel_acl_node *node)
{
	struct inode *inode;

	inode = kern_path_to_inode(node->path);
	if (!inode) {
		pr_warn("vkernel: cannot set cal, no such file or directory %s\n", node->path);
		return 0;
	}

	if (!vk_acl_node_add(acl, inode, node)) {
		if (S_ISDIR(inode->i_mode))
			inode->i_opflags |= IOP_VKERNEL_DIR;
		else
			inode->i_opflags |= IOP_VKERNEL_REG;
	}

	pr_debug("activate acl, path %s mode 0x%x ino %lu\n", node->path, node->mode, inode->i_ino);

	return 0;
}

int vk_deactivate_acl(struct vkernel_acl *acl, struct vkernel_acl_node *node)
{
	struct inode *inode;

	inode = kern_path_to_inode(node->path);
	if (!inode)
		return -EINVAL;

	if (!vk_acl_node_del(acl, inode)) {
		if (S_ISDIR(inode->i_mode))
			inode->i_opflags &= ~IOP_VKERNEL_DIR;
		else
			inode->i_opflags &= ~IOP_VKERNEL_REG;
	}

	return 0;
}

static void vk_activate_acl_all(struct vkernel_acl *acl)
{
	static DEFINE_MUTEX(vk_activate_lock);
	struct vkernel_acl_node *node;

	/* Failure on trylock means someone is doing this job */
	if (!mutex_trylock(&vk_activate_lock))
		return;

	acl->active = true;
	list_for_each_entry(node, &acl->nodes, link) {
		if (hlist_unhashed(&node->hash))
			vk_activate_acl(acl, node);
	}

	mutex_unlock(&vk_activate_lock);
}

static int vk_permission(struct vkernel *vk, struct inode *inode, int mask)
{
	struct vkernel_acl_node *node;

	node = vk_acl_node_get(&vk->acl, inode);
	if (node) {
		if ((mask & ~(node->mode) & (MAY_READ | MAY_WRITE | MAY_EXEC)) != 0) {
			pr_err("vkernel: permision denied, pid %d mask 0x%x vmode 0x%x path %s\n",
					current->pid, mask, node->mode, node->path);
			return -EACCES;
		}
	}

	return 0;
}

/*
 * Note: some filesystems or inodes may define their own permission hook.
 * In such cases, vkernel permission check will be skipped.
 */
int vk_generic_permission(struct vkernel *vk, struct mnt_idmap *idmap,
			struct inode *inode, int mask)
{
	int ret = 0;

	/* Activate acl at first check */
	if (unlikely(!vk->acl.active))
		vk_activate_acl_all(&vk->acl);

	if (inode->i_opflags & (IOP_VKERNEL_REG|IOP_VKERNEL_DIR))
		ret = vk_permission(vk, inode, mask);

	return ret;
}

int vkernel_set_acl(struct vkernel_acl *acl, char *path, unsigned short mode)
{
	struct vkernel_acl_node *node;

	pr_debug("set acl, path %s mode 0x%x\n", path, mode);
	node = kmem_cache_alloc(acl_node_cache, GFP_KERNEL_ACCOUNT);
	if (!node) {
		pr_err("failed to alloc acl node\n");
		return -ENOMEM;
	}
	INIT_HLIST_NODE(&node->hash);
	node->ino = 0;
	node->sb = NULL;
	memcpy(node->path, path, VKERNEL_PATH_MAX);
	node->mode = mode;
	list_add_tail(&node->link, &acl->nodes);

	if (acl->active)
		return vk_activate_acl(acl, node);

	return 0;
}
EXPORT_SYMBOL(vkernel_set_acl);

int vkernel_clear_acl(struct vkernel_acl *acl, char *path)
{
	struct vkernel_acl_node *node;
	bool found = false;

	list_for_each_entry(node, &acl->nodes, link) {
		if (!strncmp(node->path, path, VKERNEL_PATH_MAX)) {
			found = true;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	if (!hlist_unhashed(&node->hash))
		vk_deactivate_acl(acl, node);

	list_del(&node->link);
	kmem_cache_free(acl_node_cache, node);

	return 0;
}
EXPORT_SYMBOL(vkernel_clear_acl);

int vkernel_set_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set)
{
	u64 i;
	int r;

	for (i = 0; i < set->nr_descs; i++) {
		r = vkernel_set_acl(acl, set->descs[i].path, set->descs[i].mode);
		if (r)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL(vkernel_set_acl_set);

int vkernel_clear_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set)
{
	u64 i;
	int r;

	for (i = 0; i < set->nr_descs; i++) {
		r = vkernel_clear_acl(acl, set->descs[i].path);
		if (r)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL(vkernel_clear_acl_set);

int vkernel_set_default_acl_set(struct vkernel_acl *acl)
{
	u64 i;
	int r;

	for (i = 0; i < ARRAY_SIZE(def_path); i++) {
		r = vkernel_set_acl(acl, def_path[i], def_mode[i]);
		if (r)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL(vkernel_set_default_acl_set);
