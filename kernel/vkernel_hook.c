// SPDX-License-Identifier: GPL-2.0
/**
 * Vkernel hook
 *
 * Vkernel polcies are implemented as loadable module(s) and
 * applied by hooks
 *
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 **/

#include <linux/vkernel.h>

static DEFINE_MUTEX(vkernel_lock);
static DEFINE_HASHTABLE(vkernel_ht, 6);

/* id -> vk cache */
static unsigned int id_cache;
static struct vkernel *vk_cache;

DEFINE_PER_CPU(struct task_struct *, current_syscall_task);
EXPORT_PER_CPU_SYMBOL(current_syscall_task);

DEFINE_PER_CPU(struct vkernel *, current_syscall_vk);
EXPORT_PER_CPU_SYMBOL(current_syscall_vk);

struct vkernel *vkernel_find_vk_by_id(unsigned int id)
{
	struct vkernel *vk;

	if (id == id_cache)
		return vk_cache;

	/* TODO: protect with rwlock? */
	hash_for_each_possible(vkernel_ht, vk, hash, id) {
		if (id == vk->pid_ns->ns.inum) {
			id_cache = vk->pid_ns->ns.inum;
			vk_cache = vk;
			return vk;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(vkernel_find_vk_by_id);

struct vkernel *vkernel_find_vk_by_task(struct task_struct *tsk)
{
	struct vkernel *vk;
	struct pid_namespace *ns;

	ns = task_active_pid_ns(tsk);
	if (!ns || ns == &init_pid_ns)
		return NULL;

	vk = vkernel_find_vk_by_id(ns->ns.inum);
	if (vk && vk->active)
		return vk;

	return NULL;
}
EXPORT_SYMBOL(vkernel_find_vk_by_task);

int vkernel_register_vk(struct vkernel *vk)
{
	if (!hlist_unhashed(&vk->hash))
		return -EEXIST;

	mutex_lock(&vkernel_lock);
	hash_add(vkernel_ht, &vk->hash, vk->pid_ns->ns.inum);
	mutex_unlock(&vkernel_lock);
	id_cache = vk->pid_ns->ns.inum;
	vk_cache = vk;

	return 0;
}
EXPORT_SYMBOL(vkernel_register_vk);

int vkernel_unregister_vk(struct vkernel *vk)
{
	if (vk->pid_ns->ns.inum == id_cache) {
		id_cache = 0;
		vk_cache = NULL;
	}
	/* It is also ok to remove an unhashed vk */
	mutex_lock(&vkernel_lock);
	hash_del(&vk->hash);
	mutex_unlock(&vkernel_lock);

	return 0;
}
EXPORT_SYMBOL(vkernel_unregister_vk);
