// SPDX-License-Identifier: GPL-2.0
/**
 * vkernel core
 *
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 **/

#include <linux/kernel.h>
#include <linux/proc_ns.h>
#include <linux/miscdevice.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/compat.h>
#include <linux/debugfs.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/ipc_namespace.h>
#include <linux/inetdevice.h>
#include <linux/netconf.h>
#include <linux/mman.h>

#include "fs.h"
#include "security.h"
#include "syscall.h"
#include "sysctl.h"
#include "utils.h"

MODULE_AUTHOR("JYH Lab");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("vkernel core module");

/* Worst case buffer size needed for holding an integer. */
#define ITOA_MAX_LEN 12

static DEFINE_MUTEX(vk_lock);
static LIST_HEAD(vk_list);

static DEFINE_MUTEX(custom_lock);
static DEFINE_HASHTABLE(custom_ht, 6);

struct dentry *vkernel_debugfs_dir;
EXPORT_SYMBOL_GPL(vkernel_debugfs_dir);

static const struct file_operations vkernel_chardev_ops;

#define CONFIG_VKERNEL_COMPAT

#ifdef CONFIG_VKERNEL_COMPAT
#define VKERNEL_COMPAT(c)	.compat_ioctl	= (c)
#else
/*
 * For architectures that don't implement a compat infrastructure,
 * adopt a double line of defense:
 * - Prevent a compat task from opening /dev/vkernel
 * - If the open has been done by a 64bit task, and the vkernel fd
 *   passed to a compat task, let the ioctls fail.
 */
static long vkernel_no_compat_ioctl(struct file *file, unsigned int ioctl,
				unsigned long arg)
{
	return -EINVAL;
}

static int vkernel_no_compat_open(struct inode *inode, struct file *file)
{
	return is_compat_task() ? -ENODEV : 0;
}
#define VKERNEL_COMPAT(c)	.compat_ioctl	= vkernel_no_compat_ioctl,	\
			.open			= vkernel_no_compat_open
#endif

#define VKERNEL_EVENT_CREATE_VK 0
#define VKERNEL_EVENT_DESTROY_VK 1

#define VKERNEL_CAP_MASK ((1 << VKERNEL_CAP_ISOLATE_ANON) |\
	(1 << VKERNEL_CAP_ISOLATE_ANON_PIPE) | \
	(1 << VKERNEL_CAP_ISOLATE_RAMFS))

static void vkernel_uevent_notify_change(unsigned int type, struct vkernel *vk);
static DEFINE_MUTEX(event_lock);
static unsigned long long vkernel_createvk_count;
static unsigned long long vkernel_active_vks;


static int default_post_create(struct vkernel *vk)
{
	/* Set default syscall and acl rules */
	vk_install_default_syscalls(&vk->syscall);
	return vkernel_set_default_acl_set(&vk->acl);
}

static struct vkernel_custom_type default_custom = {
	.owner = THIS_MODULE,
	.name = "default",
	.post_create = default_post_create,
	.pre_destroy = NULL,
};

struct vkernel_custom_type *vkernel_find_custom(const char *name)
{
	struct vkernel_custom_type *custom;
	unsigned int key;

	key = full_name_hash(NULL, name, strlen(name));

	hash_for_each_possible(custom_ht, custom, hash, key) {
		if (!strcmp(name, custom->name))
			return custom;
	}

	return NULL;
}
EXPORT_SYMBOL(vkernel_find_custom);

int vkernel_register_custom(struct vkernel_custom_type *custom)
{
	unsigned int key;

	if (!custom->owner) {
		pr_err("custom type %s has no owner\n", custom->name);
		return -EINVAL;
	}

	if (vkernel_find_custom(custom->name)) {
		pr_err("custom type %s already existed\n", custom->name);
		return -EEXIST;
	}

	key = full_name_hash(NULL, custom->name, strlen(custom->name));
	mutex_lock(&custom_lock);
	hash_add(custom_ht, &custom->hash, key);
	mutex_unlock(&custom_lock);

	pr_info("register cutom type %s\n", custom->name);

	return 0;
}
EXPORT_SYMBOL(vkernel_register_custom);

int vkernel_unregister_custom(struct vkernel_custom_type *custom)
{
	pr_info("unregister cutom type %s\n", custom->name);

	mutex_lock(&custom_lock);
	/* It is also ok to remove an unhashed custom */
	hash_del(&custom->hash);
	mutex_unlock(&custom_lock);

	return 0;
}
EXPORT_SYMBOL(vkernel_unregister_custom);


__weak int vkernel_arch_vk_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	return 0;
}

__weak int vkernel_arch_dev_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	return 0;
}

static int vkernel_vk_ioctl_set_def_syscall(struct vkernel *vk, unsigned long arg)
{
	return vkernel_set_default_syscall_rule(&vk->syscall, arg);
}

static int vkernel_vk_ioctl_restrict_syscall(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_syscall_rule_desc desc;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	return vkernel_add_syscall_rule(&vk->syscall, &desc);
}

static int vkernel_vk_ioctl_restrict_file(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_file_desc_set header;
	struct vkernel_file_desc_set *set = NULL;
	unsigned long full_size;
	int r = 0;

	if (copy_from_user(&header, argp, sizeof(header))) {
		r = -EFAULT;
		goto out;
	}
	if (!header.nr_descs) {
		r = -EINVAL;
		goto out;
	}

	full_size = sizeof(header) + sizeof(struct vkernel_file_desc) * header.nr_descs;
	set = kmalloc(full_size, GFP_KERNEL);
	if (!set) {
		r = -ENOMEM;
		goto out_set;
	}
	if (copy_from_user(set, argp, full_size)) {
		r = -EFAULT;
		goto out_set;
	}

	r = vkernel_set_acl_set(&vk->acl, set);

out_set:
	kfree(set);
out:
	return r;
}

static int vkernel_vk_ioctl_restrict_linux_cap(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_linux_cap cap;

	if (copy_from_user(&cap, argp, sizeof(cap)))
		return -EFAULT;

	return vkernel_set_linux_cap(vk, &cap);
}

static int vkernel_vk_ioctl_set_sysctl_fs(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_sysctl_fs_desc desc;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	return vkernel_set_sysctl_fs(&vk->sysctl_fs, &desc);
}

static int vkernel_vk_ioctl_set_sysctl_kernel(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_sysctl_kernel_desc desc;
	struct ipc_namespace *ipc_ns;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	/* Handle namespace fields */
	if (vk->init_process->nsproxy)
		ipc_ns = vk->init_process->nsproxy->ipc_ns;
	if (likely(ipc_ns)) {
		if (desc.msgmax)
			ipc_ns->msg_ctlmax = desc.msgmax;
		if (desc.msgmnb)
			ipc_ns->msg_ctlmnb = desc.msgmnb;
		if (desc.msgmni)
			ipc_ns->msg_ctlmni = desc.msgmni;
#ifdef CONFIG_CHECKPOINT_RESTORE
		if (desc.msg_next_id >= -1)
			ipc_ns->ids[IPC_MSG_IDS].next_id = desc.msg_next_id;
#endif
		if (desc.semmsl > 0)
			ipc_ns->sem_ctls[0] = desc.semmsl;
		if (desc.semmns > 0)
			ipc_ns->sem_ctls[1] = desc.semmns;
		if (desc.semopm > 0)
			ipc_ns->sem_ctls[2] = desc.semopm;
		if (desc.semmni > 0)
			ipc_ns->sem_ctls[3] = desc.semmni;
#ifdef CONFIG_CHECKPOINT_RESTORE
		if (desc.sem_next_id >= -1)
			ipc_ns->ids[IPC_SEM_IDS].next_id = desc.sem_next_id;
#endif
		if (desc.shmall)
			ipc_ns->shm_ctlall = desc.shmall;
		if (desc.shmmax)
			ipc_ns->shm_ctlmax = desc.shmmax;
		if (desc.shmmni)
			ipc_ns->shm_ctlmni = desc.shmmni;
#ifdef CONFIG_CHECKPOINT_RESTORE
		if (desc.shm_next_id)
			ipc_ns->ids[IPC_SHM_IDS].next_id = desc.shm_next_id;
#endif
		if (desc.shm_rmid_forced == 0 || desc.shm_rmid_forced == 1)
			ipc_ns->shm_rmid_forced = desc.shm_rmid_forced;
	}

	return vkernel_set_sysctl_kernel(&vk->sysctl_kernel, &desc);
}

static int vkernel_vk_ioctl_set_sysctl_net(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_sysctl_net_desc desc;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	return vkernel_set_sysctl_net(&vk->sysctl_net, &desc);
}

static int vkernel_vk_ioctl_set_sysctl_vm(struct vkernel *vk, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_sysctl_vm_desc desc;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	if (desc.overcommit_memory == OVERCOMMIT_NEVER)
		vk_sync_overcommit_as(vk);

	return vkernel_set_sysctl_vm(&vk->sysctl_vm, &desc);
}

static int vkernel_vk_ioctl_check_extension(struct vkernel *vk, unsigned long arg)
{
	int r = 0;

	switch (arg) {
	case VKERNEL_CAP_ISOLATE_LOG:
		r = 0;
		break;
	default:
		r = -EOPNOTSUPP;
		break;
	}

	return r;
}

static int vkernel_vk_ioctl_enable_cap(struct vkernel *vk, unsigned long arg)
{
	int r = 0;

	if (arg >= VKERNEL_CAP_NUM)
		return -EINVAL;

	if (vk->caps & (arg << 1))
		return 0;

	switch (arg) {
	case VKERNEL_CAP_ISOLATE_LOG:
		vk->log_ns = vk->pid_ns->ns.inum;
		break;
	default:
		r = -EOPNOTSUPP;
	}

	if (!r)
		vk->caps |= (1 << arg);

	return r;
}

static int stat_show(struct seq_file *m, void *v)
{
	struct vkernel *vk = m->private;

	seq_puts(m, "=== BASIC ===\n");
	seq_printf(m, "Name: %s\n", vk->name);
	seq_printf(m, "Pid ns: %u\n", vk->pid_ns->ns.inum);
	seq_printf(m, "Uts ns: %u\n", vk->uts_ns->ns.inum);
	seq_printf(m, "Init pid: %d\n", vk->init_pid);
	seq_printf(m, "Users count: %d\n", refcount_read(&vk->users_count));
	seq_printf(m, "Active: %d\n", vk->active);

	seq_puts(m, "=== SECURITY ===\n");
	seq_printf(m, "Syscall def act: %d\n", vk->syscall.def_act);
	seq_printf(m, "Syscall do_futex %p\n", vk->syscall.do_futex);
	seq_printf(m, "ACL bits: %d\n", vk->acl.bits);
	seq_printf(m, "ACL active: %d\n", vk->acl.active);
	seq_printf(m, "Cap inheritable: 0x%llx\n", vk->linux_cap.inheritable.val);
	seq_printf(m, "Cap permitted: 0x%llx\n", vk->linux_cap.permitted.val);
	seq_printf(m, "Cap effective: 0x%llx\n", vk->linux_cap.effective.val);
	seq_printf(m, "Cap bset: 0x%llx\n", vk->linux_cap.bset.val);
	seq_printf(m, "Cap ambient: 0x%llx\n", vk->linux_cap.ambient.val);

	seq_puts(m, "EXTENSION CAP\n");
	seq_printf(m, "Isolation caps: 0x%lx\n", vk->caps);
	seq_printf(m, "Log ns: %u\n", vk->log_ns);

	seq_puts(m, "=== SYSCTL ===\n");
	seq_printf(m, "fs.file-max=%lu\n", vk->sysctl_fs.files_stat.max_files);
	seq_printf(m, "fs.nr_open=%u\n", vk->sysctl_fs.nr_open);
	seq_printf(m, "fs.lease-break-time=%d\n", vk->sysctl_fs.lease_break_time);
	seq_printf(m, "fs.leases-enable=%d\n", vk->sysctl_fs.leases_enable);
	seq_printf(m, "fs.mount-max=%u\n", vk->sysctl_fs.mount_max);
	seq_printf(m, "kernel.numa_balancing=%d\n", vk->sysctl_kernel.nb_mode);
	seq_printf(m, "kernel.numa_balancing_promote_rate_limit_MBps=%d\n",
			vk->sysctl_kernel.nb_promote_rate_limit);
	seq_printf(m, "kernel.sched_cfs_bandwidth_slice_us=%u\n",
			vk->sysctl_kernel.sched_cfs_bandwidth_slice);
	seq_printf(m, "kernel.sched_child_runs_first=%u\n",
			vk->sysctl_kernel.sched_child_runs_first);
	seq_printf(m, "kernel.sched_deadline_period_max_us=%u\n",
			vk->sysctl_kernel.sched_dl_period_max);
	seq_printf(m, "kernel.sched_deadline_period_min_us=%u\n",
			vk->sysctl_kernel.sched_dl_period_min);
	seq_printf(m, "kernel.sched_rr_timeslice_ms=%d\n",
			vk->sysctl_kernel.sched_rr_timeslice);
	seq_printf(m, "kernel.sched_rt_period_us=%d\n",
			vk->sysctl_kernel.sched_rt_period);
	seq_printf(m, "kernel.sched_rt_runtime_us=%d\n",
			vk->sysctl_kernel.sched_rt_runtime);
	seq_printf(m, "kernel.threads-max=%d\n", vk->sysctl_kernel.max_threads);
	seq_printf(m, "kernel.keys.gc_delay=%u\n", vk->sysctl_kernel.key_gc_delay);
	seq_printf(m, "kernel.keys.maxbytes=%u\n", vk->sysctl_kernel.key_quota_maxbytes);
	seq_printf(m, "kernel.keys.maxkeys=%u\n", vk->sysctl_kernel.key_quota_maxkeys);
	seq_printf(m, "kernel.keys.persistent_keyring_expiry=%u\n",
			vk->sysctl_kernel.persistent_keyring_expiry);
	seq_printf(m, "kernel.keys.root_maxbytes=%u\n",
			vk->sysctl_kernel.key_quota_root_maxbytes);
	seq_printf(m, "kernel.keys.root_maxkeys=%u\n",
			vk->sysctl_kernel.key_quota_root_maxkeys);
	seq_printf(m, "kernel.pty.max=%d\n", vk->sysctl_kernel.pty_limit);
	seq_printf(m, "kernel.pty.reserve=%d\n", vk->sysctl_kernel.pty_reserve);
	seq_printf(m, "net.nf_conntrack_max=%u\n", vk->sysctl_net.nf_conntrack_max);
	seq_printf(m, "net.core.busy_poll=%u\n", vk->sysctl_net.net_busy_poll);
	seq_printf(m, "net.core.busy_read=%u\n", vk->sysctl_net.net_busy_read);
	seq_printf(m, "net.core.optmem_max=%d\n", vk->sysctl_net.optmem_max);
	seq_printf(m, "net.core.wmem_max=%u\n", vk->sysctl_net.wmem_max);
	seq_printf(m, "net.core.rmem_max=%u\n", vk->sysctl_net.rmem_max);
	seq_printf(m, "net.core.wmem_default=%u\n", vk->sysctl_net.wmem_default);
	seq_printf(m, "net.core.rmem_default=%u\n", vk->sysctl_net.rmem_default);
	seq_printf(m, "vm.max_map_count=%d\n", vk->sysctl_vm.max_map_count);
	seq_printf(m, "vm.mmap_min_addr=0x%lx\n", vk->sysctl_vm.mmap_min_addr);
	seq_printf(m, "vm.dac_mmap_min_addr=0x%lx\n", vk->sysctl_vm.dac_mmap_min_addr);
	seq_printf(m, "vm.overcommit_kbytes=%lu\n", vk->sysctl_vm.overcommit_kbytes);
	seq_printf(m, "vm.overcommit_memory=%d\n", vk->sysctl_vm.overcommit_memory);
	seq_printf(m, "vm.overcommit_ratio=%d\n", vk->sysctl_vm.overcommit_ratio);

	seq_puts(m, "=== OPERATION ===\n");
	seq_printf(m, "Op cap_capable: %p\n", vk->ops.cap_capable);
	seq_printf(m, "Op generic_permission: %p\n", vk->ops.generic_permission);

	seq_puts(m, "=== CUSTOM ===\n");
	seq_printf(m, "Custom type: %s\n", vk->custom->name);
	seq_printf(m, "Custom post_create: %p\n", vk->custom->post_create);
	seq_printf(m, "Custom pre_destroy: %p\n", vk->custom->pre_destroy);

	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;
	int r;

	if (!vkernel_get_vk_safe(vk))
		return -ENOENT;

	r = single_open(file, stat_show, inode->i_private);
	if (r < 0)
		vkernel_put_vk(vk);

	return r;
}

static int stat_release(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;

	vkernel_put_vk(vk);

	return single_release(inode, file);
}

static const struct file_operations vk_stat_fops = {
	.open = stat_open,
	.release = stat_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int sysctl_show(struct seq_file *m, void *v)
{
	struct vkernel *vk = m->private;
	struct ipc_namespace *ipc_ns = NULL;
	struct net *n;

	if (vk->init_process->nsproxy)
		ipc_ns = vk->init_process->nsproxy->ipc_ns;

	n = vk->sysctl_net.net;

	seq_puts(m, "=== fs ===\n");
	seq_printf(m, "fs.file-max=%lu\n", vk->sysctl_fs.files_stat.max_files);
	seq_printf(m, "fs.nr_open=%u\n", vk->sysctl_fs.nr_open);
	seq_printf(m, "fs.lease-break-time=%d\n", vk->sysctl_fs.lease_break_time);
	seq_printf(m, "fs.leases-enable=%d\n", vk->sysctl_fs.leases_enable);
	seq_printf(m, "fs.mount-max=%u\n", vk->sysctl_fs.mount_max);

	seq_puts(m, "=== kernel ===\n");
	if (ipc_ns) {
		seq_printf(m, "kernel.msgmax=%u\n", ipc_ns->msg_ctlmax);
		seq_printf(m, "kernel.msgmnb=%u\n", ipc_ns->msg_ctlmnb);
		seq_printf(m, "kernel.msgmni=%u\n", ipc_ns->msg_ctlmni);
#ifdef CONFIG_CHECKPOINT_RESTORE
		seq_printf(m, "kernel.msg_next_id=%d\n", ipc_ns->ids[IPC_MSG_IDS].next_id);
#endif
		seq_printf(m, "kernel.sem=%d %d %d\n",
				ipc_ns->sem_ctls[0], ipc_ns->sem_ctls[1], ipc_ns->sem_ctls[2]);
#ifdef CONFIG_CHECKPOINT_RESTORE
		seq_printf(m, "kernel.sem_next_id=%d\n", ipc_ns->ids[IPC_SEM_IDS].next_id);
#endif
		seq_printf(m, "kernel.shmall=%lu\n", ipc_ns->shm_ctlall);
		seq_printf(m, "kernel.shmmax=%lu\n", ipc_ns->shm_ctlmax);
		seq_printf(m, "kernel.shmmni=%d\n", ipc_ns->shm_ctlmni);
#ifdef CONFIG_CHECKPOINT_RESTORE
		seq_printf(m, "kernel.shm_next_id=%d\n", ipc_ns->ids[IPC_SHM_IDS].next_id);
#endif
		seq_printf(m, "kernel.shm_rmid_forced=%d\n", ipc_ns->shm_rmid_forced);
	}
	seq_printf(m, "kernel.numa_balancing=%d\n", vk->sysctl_kernel.nb_mode);
	seq_printf(m, "kernel.numa_balancing_promote_rate_limit_MBps=%d\n",
			vk->sysctl_kernel.nb_promote_rate_limit);
	seq_printf(m, "kernel.sched_cfs_bandwidth_slice_us=%u\n",
			vk->sysctl_kernel.sched_cfs_bandwidth_slice);
	seq_printf(m, "kernel.sched_child_runs_first=%u\n",
			vk->sysctl_kernel.sched_child_runs_first);
	seq_printf(m, "kernel.sched_deadline_period_max_us=%u\n",
			vk->sysctl_kernel.sched_dl_period_max);
	seq_printf(m, "kernel.sched_deadline_period_min_us=%u\n",
			vk->sysctl_kernel.sched_dl_period_min);
	seq_printf(m, "kernel.sched_rr_timeslice_ms=%d\n",
			vk->sysctl_kernel.sched_rr_timeslice);
	seq_printf(m, "kernel.sched_rt_period_us=%d\n",
			vk->sysctl_kernel.sched_rt_period);
	seq_printf(m, "kernel.sched_rt_runtime_us=%d\n",
			vk->sysctl_kernel.sched_rt_runtime);
	seq_printf(m, "kernel.threads-max=%d\n", vk->sysctl_kernel.max_threads);
	seq_printf(m, "kernel.keys.gc_delay=%u\n", vk->sysctl_kernel.key_gc_delay);
	seq_printf(m, "kernel.keys.maxbytes=%u\n", vk->sysctl_kernel.key_quota_maxbytes);
	seq_printf(m, "kernel.keys.maxkeys=%u\n", vk->sysctl_kernel.key_quota_maxkeys);
	seq_printf(m, "kernel.keys.persistent_keyring_expiry=%u\n",
			vk->sysctl_kernel.persistent_keyring_expiry);
	seq_printf(m, "kernel.keys.root_maxbytes=%u\n",
			vk->sysctl_kernel.key_quota_root_maxbytes);
	seq_printf(m, "kernel.keys.root_maxkeys=%u\n",
			vk->sysctl_kernel.key_quota_root_maxkeys);
	seq_printf(m, "kernel.pty.max=%d\n", vk->sysctl_kernel.pty_limit);
	seq_printf(m, "kernel.pty.reserve=%d\n", vk->sysctl_kernel.pty_reserve);

	seq_puts(m, "=== net ===\n");
	seq_printf(m, "net.nf_conntrack_max=%u\n", vk->sysctl_net.nf_conntrack_max);
	seq_printf(m, "net.core.busy_poll=%u\n", vk->sysctl_net.net_busy_poll);
	seq_printf(m, "net.core.busy_read=%u\n", vk->sysctl_net.net_busy_read);
	seq_printf(m, "net.core.optmem_max=%d\n", vk->sysctl_net.optmem_max);
	seq_printf(m, "net.core.wmem_max=%u\n", vk->sysctl_net.wmem_max);
	seq_printf(m, "net.core.rmem_max=%u\n", vk->sysctl_net.rmem_max);
	seq_printf(m, "net.core.wmem_default=%u\n", vk->sysctl_net.wmem_default);
	seq_printf(m, "net.core.rmem_default=%u\n", vk->sysctl_net.rmem_default);

	seq_printf(m, "net.core.somaxconn=%d\n", n->core.sysctl_somaxconn);
	seq_printf(m, "net.ipv4.icmp_echo_ignore_broadcasts=%u\n",
			n->ipv4.sysctl_icmp_echo_ignore_broadcasts);
	seq_printf(m, "net.ipv4.ip_local_port_range=%d %d\n",
			n->ipv4.ip_local_ports.range[0], n->ipv4.ip_local_ports.range[1]);
	seq_printf(m, "net.ipv4.tcp_max_tw_buckets=%d\n",
			n->ipv4.tcp_death_row.sysctl_max_tw_buckets);
	seq_printf(m, "net.ipv4.tcp_ecn=%u\n", n->ipv4.sysctl_tcp_ecn);
	seq_printf(m, "net.ipv4.ip_default_ttl=%u\n", n->ipv4.sysctl_ip_default_ttl);
	seq_printf(m, "net.ipv4.ip_no_pmtu_disc=%u\n", n->ipv4.sysctl_ip_no_pmtu_disc);
	seq_printf(m, "net.ipv4.tcp_keepalive_time=%d\n",
			READ_ONCE(n->ipv4.sysctl_tcp_keepalive_time) / HZ);
	seq_printf(m, "net.ipv4.tcp_keepalive_intvl=%d\n",
			READ_ONCE(n->ipv4.sysctl_tcp_keepalive_intvl) / HZ);
	seq_printf(m, "net.ipv4.tcp_keepalive_probes=%u\n",
			n->ipv4.sysctl_tcp_keepalive_probes);
	seq_printf(m, "net.ipv4.tcp_syn_retries=%u\n", n->ipv4.sysctl_tcp_syn_retries);
	seq_printf(m, "net.ipv4.tcp_synack_retries=%u\n", n->ipv4.sysctl_tcp_synack_retries);
	seq_printf(m, "net.ipv4.tcp_syncookies=%u\n", n->ipv4.sysctl_tcp_syncookies);
	seq_printf(m, "net.ipv4.tcp_reordering=%d\n", n->ipv4.sysctl_tcp_reordering);
	seq_printf(m, "net.ipv4.tcp_retries1=%u\n", n->ipv4.sysctl_tcp_retries1);
	seq_printf(m, "net.ipv4.tcp_retries2=%u\n", n->ipv4.sysctl_tcp_retries2);
	seq_printf(m, "net.ipv4.tcp_orphan_retries=%u\n", n->ipv4.sysctl_tcp_orphan_retries);
	seq_printf(m, "net.ipv4.tcp_tw_reuse=%u\n", n->ipv4.sysctl_tcp_tw_reuse);
	seq_printf(m, "net.ipv4.tcp_fin_timeout=%d\n",
			READ_ONCE(n->ipv4.sysctl_tcp_fin_timeout) / HZ);
	seq_printf(m, "net.ipv4.tcp_sack=%u\n", n->ipv4.sysctl_tcp_sack);
	seq_printf(m, "net.ipv4.tcp_window_scaling=%u\n", n->ipv4.sysctl_tcp_window_scaling);
	seq_printf(m, "net.ipv4.tcp_timestamps=%u\n", n->ipv4.sysctl_tcp_timestamps);
	seq_printf(m, "net.ipv4.tcp_thin_linear_timeouts=%u\n",
			n->ipv4.sysctl_tcp_thin_linear_timeouts);
	seq_printf(m, "net.ipv4.tcp_retrans_collapse=%u\n", n->ipv4.sysctl_tcp_retrans_collapse);
	seq_printf(m, "net.ipv4.tcp_fack=%u\n", n->ipv4.sysctl_tcp_fack);
	seq_printf(m, "net.ipv4.tcp_adv_win_scale=%d\n", n->ipv4.sysctl_tcp_adv_win_scale);
	seq_printf(m, "net.ipv4.tcp_dsack=%u\n", n->ipv4.sysctl_tcp_dsack);
	seq_printf(m, "net.ipv4.tcp_nometrics_save=%u\n", n->ipv4.sysctl_tcp_nometrics_save);
	seq_printf(m, "net.ipv4.tcp_moderate_rcvbuf=%u\n", n->ipv4.sysctl_tcp_moderate_rcvbuf);
	seq_printf(m, "net.ipv4.tcp_min_tso_segs=%u\n", n->ipv4.sysctl_tcp_min_tso_segs);
	seq_printf(m, "net.ipv4.tcp_wmem=%d %d %d\n",
			n->ipv4.sysctl_tcp_wmem[0], n->ipv4.sysctl_tcp_wmem[1],
			n->ipv4.sysctl_tcp_wmem[2]);
	seq_printf(m, "net.ipv4.tcp_rmem=%d %d %d\n",
			n->ipv4.sysctl_tcp_rmem[0], n->ipv4.sysctl_tcp_rmem[1],
			n->ipv4.sysctl_tcp_rmem[2]);
	seq_printf(m, "net.ipv4.max_syn_backlog=%d\n", n->ipv4.sysctl_max_syn_backlog);
	seq_printf(m, "net.ipv4.tcp_fastopen=%u\n", n->ipv4.sysctl_tcp_fastopen);
	seq_printf(m, "net.ipv4.tcp_congestion_control=%s\n",
			n->ipv4.tcp_congestion_control->name);

	seq_printf(m, "net.ipv4.conf.all.forwarding=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.all.mc_forwarding=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_MC_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.all.proxy_arp=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_PROXY_ARP - 1]);
	seq_printf(m, "net.ipv4.conf.all.accept_redirects=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ACCEPT_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.all.secure_redirects=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_SECURE_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.all.send_redirects=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_SEND_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.all.shared_media=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_SHARED_MEDIA - 1]);
	seq_printf(m, "net.ipv4.conf.all.rp_filter=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_RP_FILTER - 1]);
	seq_printf(m, "net.ipv4.conf.all.accept_source_route=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ACCEPT_SOURCE_ROUTE - 1]);
	seq_printf(m, "net.ipv4.conf.all.bootp_relay=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_BOOTP_RELAY - 1]);
	seq_printf(m, "net.ipv4.conf.all.log_martians=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_LOG_MARTIANS - 1]);
	seq_printf(m, "net.ipv4.conf.all.tag=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_TAG - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_filter=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARPFILTER - 1]);
	seq_printf(m, "net.ipv4.conf.all.medium_id=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_MEDIUM_ID - 1]);
	seq_printf(m, "net.ipv4.conf.all.disable_xfrm=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_NOXFRM - 1]);
	seq_printf(m, "net.ipv4.conf.all.disable_policy=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_NOPOLICY - 1]);
	seq_printf(m, "net.ipv4.conf.all.force_igmp_version=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_FORCE_IGMP_VERSION - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_announce=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARP_ANNOUNCE - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_ignore=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARP_IGNORE - 1]);
	seq_printf(m, "net.ipv4.conf.all.promote_secondaries=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_PROMOTE_SECONDARIES - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_accept=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARP_ACCEPT - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_notify=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARP_NOTIFY - 1]);
	seq_printf(m, "net.ipv4.conf.all.accept_local=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ACCEPT_LOCAL - 1]);
	seq_printf(m, "net.ipv4.conf.all.src_valid_mark=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_SRC_VMARK - 1]);
	seq_printf(m, "net.ipv4.conf.all.proxy_arp_pvlan=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_PROXY_ARP_PVLAN - 1]);
	seq_printf(m, "net.ipv4.conf.all.route_localnet=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ROUTE_LOCALNET - 1]);
	seq_printf(m, "net.ipv4.conf.all.igmpv2_unsolicited_report_interval=%d\n",
			n->ipv4.devconf_all->data[
				IPV4_DEVCONF_IGMPV2_UNSOLICITED_REPORT_INTERVAL - 1]);
	seq_printf(m, "net.ipv4.conf.all.igmpv3_unsolicited_report_interval=%d\n",
			n->ipv4.devconf_all->data[
				IPV4_DEVCONF_IGMPV3_UNSOLICITED_REPORT_INTERVAL - 1]);
	seq_printf(m, "net.ipv4.conf.all.ignore_routes_with_linkdown=%d\n",
			n->ipv4.devconf_all->data[
				IPV4_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN - 1]);
	seq_printf(m, "net.ipv4.conf.all.drop_unicast_in_l2_multicast=%d\n",
			n->ipv4.devconf_all->data[
				IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST - 1]);
	seq_printf(m, "net.ipv4.conf.all.drop_gratuitous_arp=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_DROP_GRATUITOUS_ARP - 1]);
	seq_printf(m, "net.ipv4.conf.all.bc_forwarding=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_BC_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.all.arp_evict_nocarrier=%d\n",
			n->ipv4.devconf_all->data[IPV4_DEVCONF_ARP_EVICT_NOCARRIER - 1]);

	seq_printf(m, "net.ipv4.conf.default.forwarding=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.default.mc_forwarding=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_MC_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.default.proxy_arp=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_PROXY_ARP - 1]);
	seq_printf(m, "net.ipv4.conf.default.accept_redirects=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ACCEPT_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.default.secure_redirects=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_SECURE_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.default.send_redirects=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_SEND_REDIRECTS - 1]);
	seq_printf(m, "net.ipv4.conf.default.shared_media=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_SHARED_MEDIA - 1]);
	seq_printf(m, "net.ipv4.conf.default.rp_filter=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_RP_FILTER - 1]);
	seq_printf(m, "net.ipv4.conf.default.accept_source_route=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ACCEPT_SOURCE_ROUTE - 1]);
	seq_printf(m, "net.ipv4.conf.default.bootp_relay=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_BOOTP_RELAY - 1]);
	seq_printf(m, "net.ipv4.conf.default.log_martians=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_LOG_MARTIANS - 1]);
	seq_printf(m, "net.ipv4.conf.default.tag=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_TAG - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_filter=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARPFILTER - 1]);
	seq_printf(m, "net.ipv4.conf.default.medium_id=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_MEDIUM_ID - 1]);
	seq_printf(m, "net.ipv4.conf.default.disable_xfrm=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_NOXFRM - 1]);
	seq_printf(m, "net.ipv4.conf.default.disable_policy=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_NOPOLICY - 1]);
	seq_printf(m, "net.ipv4.conf.default.force_igmp_version=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_FORCE_IGMP_VERSION - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_announce=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARP_ANNOUNCE - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_ignore=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARP_IGNORE - 1]);
	seq_printf(m, "net.ipv4.conf.default.promote_secondaries=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_PROMOTE_SECONDARIES - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_accept=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARP_ACCEPT - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_notify=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARP_NOTIFY - 1]);
	seq_printf(m, "net.ipv4.conf.default.accept_local=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ACCEPT_LOCAL - 1]);
	seq_printf(m, "net.ipv4.conf.default.src_valid_mark=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_SRC_VMARK - 1]);
	seq_printf(m, "net.ipv4.conf.default.proxy_arp_pvlan=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_PROXY_ARP_PVLAN - 1]);
	seq_printf(m, "net.ipv4.conf.default.route_localnet=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ROUTE_LOCALNET - 1]);
	seq_printf(m, "net.ipv4.conf.default.igmpv2_unsolicited_report_interval=%d\n",
			n->ipv4.devconf_dflt->data[
				IPV4_DEVCONF_IGMPV2_UNSOLICITED_REPORT_INTERVAL - 1]);
	seq_printf(m, "net.ipv4.conf.default.igmpv3_unsolicited_report_interval=%d\n",
			n->ipv4.devconf_dflt->data[
				IPV4_DEVCONF_IGMPV3_UNSOLICITED_REPORT_INTERVAL - 1]);
	seq_printf(m, "net.ipv4.conf.default.ignore_routes_with_linkdown=%d\n",
			n->ipv4.devconf_dflt->data[
				IPV4_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN - 1]);
	seq_printf(m, "net.ipv4.conf.default.drop_unicast_in_l2_multicast=%d\n",
			n->ipv4.devconf_dflt->data[
				IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST - 1]);
	seq_printf(m, "net.ipv4.conf.default.drop_gratuitous_arp=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_DROP_GRATUITOUS_ARP - 1]);
	seq_printf(m, "net.ipv4.conf.default.bc_forwarding=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_BC_FORWARDING - 1]);
	seq_printf(m, "net.ipv4.conf.default.arp_evict_nocarrier=%d\n",
			n->ipv4.devconf_dflt->data[IPV4_DEVCONF_ARP_EVICT_NOCARRIER - 1]);

	seq_puts(m, "=== vm ===\n");
	seq_printf(m, "vm.max_map_count=%d\n", vk->sysctl_vm.max_map_count);
	seq_printf(m, "vm.mmap_min_addr=0x%lx\n", vk->sysctl_vm.mmap_min_addr);
	seq_printf(m, "vm.dac_mmap_min_addr=0x%lx\n", vk->sysctl_vm.dac_mmap_min_addr);
	seq_printf(m, "vm.overcommit_kbytes=%lu\n", vk->sysctl_vm.overcommit_kbytes);
	seq_printf(m, "vm.overcommit_memory=%d\n", vk->sysctl_vm.overcommit_memory);
	seq_printf(m, "vm.overcommit_ratio=%d\n", vk->sysctl_vm.overcommit_ratio);

	return 0;
}

static int sysctl_open(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;
	int r;

	if (!vkernel_get_vk_safe(vk))
		return -ENOENT;

	r = single_open(file, sysctl_show, inode->i_private);
	if (r < 0)
		vkernel_put_vk(vk);

	return r;
}

static int sysctl_release(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;

	vkernel_put_vk(vk);

	return single_release(inode, file);
}

static ssize_t
sysctl_write(struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	struct inode *inode;
	struct vkernel *vk;
	char buf[256];
	size_t ret;

	inode = file_inode(filp);
	vk = inode->i_private;

	if (cnt > 255)
		cnt = 255;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	pr_debug("sysctl write, vk %s, buf %s\n", vk->name, buf);

	ret = vkernel_set_sysctl_raw(vk, buf);
	if (ret)
		return ret;

	return cnt;
}

static const struct file_operations vk_sysctl_fops = {
	.open = sysctl_open,
	.release = sysctl_release,
	.read = seq_read,
	.write = sysctl_write,
	.llseek = seq_lseek,
};

static void vkernel_destroy_vk_debugfs(struct vkernel *vk)
{
	if (IS_ERR(vk->debugfs_dentry))
		return;

	debugfs_remove_recursive(vk->debugfs_dentry);
}

static int vkernel_create_vk_debugfs(struct vkernel *vk, const char *name)
{
	static DEFINE_MUTEX(vkernel_debugfs_lock);
	struct dentry *dent;

	if (!debugfs_initialized())
		return 0;

	mutex_lock(&vkernel_debugfs_lock);
	dent = debugfs_lookup(name, vkernel_debugfs_dir);
	if (dent) {
		pr_warn_ratelimited("vkernel: debugfs: duplicate directory %s\n", name);
		dput(dent);
		mutex_unlock(&vkernel_debugfs_lock);
		return 0;
	}

	dent = debugfs_create_dir(name, vkernel_debugfs_dir);
	mutex_unlock(&vkernel_debugfs_lock);
	if (IS_ERR(dent))
		return 0;

	vk->debugfs_dentry = dent;

	debugfs_create_file("stat", 0444, dent, vk, &vk_stat_fops);
	debugfs_create_file("sysctl", 0644, dent, vk, &vk_sysctl_fops);

	return 0;
}

void vkernel_destroy_vk(struct vkernel *vk)
{
	pr_info("vkernel: destroy vk %s\n", vk->name);

	vk->active = false;
	vkernel_unregister_vk(vk);

	mutex_lock(&vk_lock);
#ifdef CONFIG_DEBUG_LIST
	list_del(&vk->link);
#else
	if (vk->link.prev)
		list_del(&vk->link);
#endif
	mutex_unlock(&vk_lock);

	if (vk->custom->pre_destroy)
		vk->custom->pre_destroy(vk);
	if (vk->custom->owner != vkernel_chardev_ops.owner)
		module_put(vk->custom->owner);

	vkernel_destroy_vk_debugfs(vk);

	vk_uninit_sysctl_vm(&vk->sysctl_vm);
	vk_uninit_sysctl_net(&vk->sysctl_net);
	vk_uninit_sysctl_kernel(&vk->sysctl_kernel);
	vk_uninit_sysctl_fs(&vk->sysctl_fs);
	vk_uninit_acl(&vk->acl);
	vk_uninit_syscall(&vk->syscall);
	kfree(vk);
	module_put(vkernel_chardev_ops.owner);
}
EXPORT_SYMBOL(vkernel_destroy_vk);

struct vkernel *vkernel_create_vk(struct task_struct *tsk, const char *name,
			   const char *custom)
{
	struct vkernel *vk;
	int r = -ENOMEM;

	vk = kzalloc(sizeof(struct vkernel), GFP_KERNEL);
	if (!vk)
		return ERR_PTR(-ENOMEM);

	__module_get(vkernel_chardev_ops.owner);

	/* Init basic info */
	strscpy(vk->name, name, VKERNEL_NAME_LEN);
	INIT_HLIST_NODE(&vk->hash);
	vk->pid_ns = task_active_pid_ns(tsk);
	vk->uts_ns = tsk->nsproxy->uts_ns;
	vk->init_process = tsk;
	vk->init_pid = tsk->pid;
	refcount_set(&vk->users_count, 1);

	/*
	 * Force subsequent debugfs file creations to fail if the vk directory
	 * is not created (by vkernel_create_vk_debugfs()).
	 */
	vk->debugfs_dentry = ERR_PTR(-ENOENT);

	/* Init syscall */
	r = vk_init_syscall(&vk->syscall);
	if (r)
		goto err_vk;
	/* Init acl */
	r = vk_init_acl(&vk->acl, VKERNEL_ACL_HASH_BITS);
	if (r)
		goto err_syscall;
	/* Init linux cap */
	vk->linux_cap.inheritable = tsk->cred->cap_inheritable;
	vk->linux_cap.permitted = tsk->cred->cap_permitted;
	vk->linux_cap.effective = tsk->cred->cap_effective;
	vk->linux_cap.bset = tsk->cred->cap_bset;
	vk->linux_cap.ambient = tsk->cred->cap_ambient;

	/* Init extension cap */
	vk->caps = (1 << VKERNEL_CAP_ISOLATE_LOG);
	vk->log_ns = vk->pid_ns->ns.inum;

	/* Init sysctl */
	r = vk_init_sysctl_fs(&vk->sysctl_fs);
	if (r)
		goto err_acl;
	r = vk_init_sysctl_kernel(&vk->sysctl_kernel);
	if (r)
		goto err_fs;
	r = vk_init_sysctl_net(&vk->sysctl_net, tsk);
	if (r)
		goto err_kernel;
	r = vk_init_sysctl_vm(&vk->sysctl_vm);
	if (r)
		goto err_net;

	/* Init default operations */
	vk->ops.cap_capable = vk_cap_capable;
	vk->ops.generic_permission = vk_generic_permission;

	r = vkernel_create_vk_debugfs(vk, name);
	if (r)
		goto err_vm;

	/* Custom initializations */
	vk->custom = vkernel_find_custom(custom);
	if (!vk->custom)
		vk->custom = &default_custom;
	if (vk->custom->owner != vkernel_chardev_ops.owner)
		__module_get(vk->custom->owner);
	if (vk->custom->post_create) {
		r = vk->custom->post_create(vk);
		if (r)
			goto err_custom_debugfs;
	}

	mutex_lock(&vk_lock);
	list_add(&vk->link, &vk_list);
	mutex_unlock(&vk_lock);

	/* Register vk into kernel. It is inactive state. */
	vkernel_register_vk(vk);

	pr_info("vkernel: create vk %s, init %d, custom %s (expect %s)",
			vk->name, vk->init_pid, vk->custom->name, custom);

	return vk;

err_custom_debugfs:
	if (vk->custom->owner != vkernel_chardev_ops.owner)
		module_put(vk->custom->owner);

	vkernel_destroy_vk_debugfs(vk);
err_vm:
	vk_uninit_sysctl_vm(&vk->sysctl_vm);
err_net:
	vk_uninit_sysctl_net(&vk->sysctl_net);
err_kernel:
	vk_uninit_sysctl_kernel(&vk->sysctl_kernel);
err_fs:
	vk_uninit_sysctl_fs(&vk->sysctl_fs);
err_acl:
	vk_uninit_acl(&vk->acl);
err_syscall:
	vk_uninit_syscall(&vk->syscall);
err_vk:
	kfree(vk);
	module_put(vkernel_chardev_ops.owner);

	return ERR_PTR(r);
}
EXPORT_SYMBOL(vkernel_create_vk);

void vkernel_get_vk(struct vkernel *vk)
{
	refcount_inc(&vk->users_count);
}
EXPORT_SYMBOL(vkernel_get_vk);

/*
 * Make sure the vk is not during destruction, which is a safe version of
 * vkernel_get_vk().  Return true if vk referenced successfully, false otherwise.
 */
bool vkernel_get_vk_safe(struct vkernel *vk)
{
	return refcount_inc_not_zero(&vk->users_count);
}
EXPORT_SYMBOL(vkernel_get_vk_safe);

void vkernel_put_vk(struct vkernel *vk)
{
	if (refcount_dec_and_test(&vk->users_count))
		vkernel_destroy_vk(vk);
}
EXPORT_SYMBOL(vkernel_put_vk);

/*
 * Used to put a reference that was taken on behalf of an object associated
 * with a user-visible file descriptor, e.g. a vcpu or device, if installation
 * of the new file descriptor fails and the reference cannot be transferred to
 * its final owner.  In such cases, the caller is still actively using @vk and
 * will fail miserably if the refcount unexpectedly hits zero.
 */
void vkernel_put_vk_no_destroy(struct vkernel *vk)
{
	WARN_ON(refcount_dec_and_test(&vk->users_count));
}
EXPORT_SYMBOL(vkernel_put_vk_no_destroy);

static int vkernel_vk_release(struct inode *inode, struct file *filp)
{
	struct vkernel *vk = filp->private_data;

	pr_info("vkernel: release vk fd of %s. Currently, vk is still alive\n", vk->name);

	// vkernel_put_vk(vk);
	return 0;
}

static long vkernel_vk_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct vkernel *vk = filp->private_data;
	int r = 0;

	switch (ioctl) {
	case VKERNEL_SET_DEF_SYSCALL:
		r = vkernel_vk_ioctl_set_def_syscall(vk, arg);
		break;
	case VKERNEL_RESTRICT_SYSCALL:
		r = vkernel_vk_ioctl_restrict_syscall(vk, arg);
		break;
	case VKERNEL_RESTRICT_FILE:
		r = vkernel_vk_ioctl_restrict_file(vk, arg);
		break;
	case VKERNEL_RESTRICT_LINUX_CAP:
		r = vkernel_vk_ioctl_restrict_linux_cap(vk, arg);
		break;
	case VKERNEL_SET_CPU_PREF:
	case VKERNEL_SET_MEMORY_PREF:
		r = -EOPNOTSUPP;
		break;
	case VKERNEL_SET_SYSCTL_FS:
		r = vkernel_vk_ioctl_set_sysctl_fs(vk, arg);
		break;
	case VKERNEL_SET_SYSCTL_KERNEL:
		r = vkernel_vk_ioctl_set_sysctl_kernel(vk, arg);
		break;
	case VKERNEL_SET_SYSCTL_NET:
		r = vkernel_vk_ioctl_set_sysctl_net(vk, arg);
		break;
	case VKERNEL_SET_SYSCTL_VM:
		r = vkernel_vk_ioctl_set_sysctl_vm(vk, arg);
		break;
	case VKERNEL_CHECK_EXTENSION:
		r = vkernel_vk_ioctl_check_extension(vk, arg);
		break;
	case VKERNEL_ENABLE_CAP:
		r = vkernel_vk_ioctl_enable_cap(vk, arg);
		break;
	case VKERNEL_REGISTER:
		pr_warn("vkernel: [deprecated] register vk, init %d id %u ret %d\n",
			vk->init_process->pid, vk->pid_ns->ns.inum, r);
		break;
	case VKERNEL_UNREGISTER:
		pr_warn("vkernel: [deprecated] unregister vk, init %d id %u ret %d\n",
			vk->init_process->pid, vk->pid_ns->ns.inum, r);
		break;
	case VKERNEL_ACTIVATE:
		vk->active = true;
		break;
	case VKERNEL_DEACTIVATE:
		vk->active = false;
		break;
	default:
		r = vkernel_arch_vk_ioctl(filp, ioctl, arg);
	}

	return r;
}

#ifdef CONFIG_VKERNEL_COMPAT
long __weak vkernel_arch_vk_compat_ioctl(struct file *filp, unsigned int ioctl,
					unsigned long arg)
{
	return -ENOTTY;
}

static long vkernel_vk_compat_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	int r;

	r = vkernel_arch_vk_compat_ioctl(filp, ioctl, arg);
	if (r != -ENOTTY)
		return r;

	return vkernel_vk_ioctl(filp, ioctl, arg);
}
#endif

static const struct file_operations vkernel_vk_fops = {
	.release        = vkernel_vk_release,
	.unlocked_ioctl = vkernel_vk_ioctl,
	.llseek		= noop_llseek,
	VKERNEL_COMPAT(vkernel_vk_compat_ioctl),
};

static int vkernel_dev_ioctl_create_vk(unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vkernel_desc desc;
	struct task_struct *tsk;
	struct vkernel *vk;
	struct file *file;
	char fdname[ITOA_MAX_LEN * 2 + 2];
	int r, fd;

	if (copy_from_user(&desc, argp, sizeof(desc)))
		return -EFAULT;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "find_task_by_pid_ns() needs rcu_read_lock() protection");
	tsk = pid_task(find_pid_ns(desc.pid, &init_pid_ns), PIDTYPE_PID);
	if (!tsk) {
		pr_err("cannot find pid %d\n", desc.pid);
		return -EINVAL;
	}

	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		pr_err("cannot get unused fd\n");
		return fd;
	}

	snprintf(fdname, sizeof(fdname), "%d-%d", desc.pid, fd);

	vk = vkernel_create_vk(tsk, fdname, desc.custom);
	if (IS_ERR(vk)) {
		r = PTR_ERR(vk);
		goto put_fd;
	}

	file = anon_inode_getfile("vkernel-vk", &vkernel_vk_fops, vk, O_RDWR);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto put_kernel;
	}

	vkernel_uevent_notify_change(VKERNEL_EVENT_CREATE_VK, vk);

	fd_install(fd, file);
	return fd;

put_kernel:
	vkernel_put_vk(vk);
put_fd:
	put_unused_fd(fd);
	return r;
}

static int vkernel_dev_ioctl_destroy_vk(unsigned long arg)
{
	struct vkernel *vk;
	unsigned int id = (unsigned int)arg;

	pr_info("vkernel: try to destroy vk with id %u\n", id);

	vk = vkernel_find_vk_by_id(id);
	if (!vk)
		return -EINVAL;

	vkernel_put_vk(vk);
	return 0;
}

static long vkernel_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	int r = -EINVAL;

	switch (ioctl) {
	case VKERNEL_GET_API_VERSION:
		if (arg)
			goto out;
		r = VKERNEL_API_VERSION;
		break;
	case VKERNEL_CREATE_VK:
		r = vkernel_dev_ioctl_create_vk(arg);
		break;
	case VKERNEL_DESTROY_VK:
		r = vkernel_dev_ioctl_destroy_vk(arg);
		break;
	case VKERNEL_CHECK_EXTENSION:
		r = vkernel_vk_ioctl_check_extension(NULL, arg);
		break;
	case VKERNEL_TRACE_ENABLE:
	case VKERNEL_TRACE_PAUSE:
	case VKERNEL_TRACE_DISABLE:
		r = -EOPNOTSUPP;
		break;
	default:
		r = vkernel_arch_dev_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

static const struct file_operations vkernel_chardev_ops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = vkernel_dev_ioctl,
	.llseek		= noop_llseek,
	VKERNEL_COMPAT(vkernel_dev_ioctl),
};

static struct miscdevice vkernel_dev = {
	VKERNEL_MINOR,
	"vkernel",
	&vkernel_chardev_ops,
};

static void vkernel_uevent_notify_change(unsigned int type, struct vkernel *vk)
{
	struct kobj_uevent_env *env;
	unsigned long long created, active;

	if (!vkernel_dev.this_device || !vk)
		return;

	mutex_lock(&event_lock);
	if (type == VKERNEL_EVENT_CREATE_VK) {
		vkernel_createvk_count++;
		vkernel_active_vks++;
	} else if (type == VKERNEL_EVENT_DESTROY_VK) {
		vkernel_active_vks--;
	}
	created = vkernel_createvk_count;
	active = vkernel_active_vks;
	mutex_unlock(&event_lock);

	env = kzalloc(sizeof(*env), GFP_KERNEL_ACCOUNT);
	if (!env)
		return;

	add_uevent_var(env, "CREATED=%llu", created);
	add_uevent_var(env, "COUNT=%llu", active);

	if (type == VKERNEL_EVENT_CREATE_VK)
		add_uevent_var(env, "EVENT=create");
	else if (type == VKERNEL_EVENT_DESTROY_VK)
		add_uevent_var(env, "EVENT=destroy");
	add_uevent_var(env, "VKID=%d", vk->pid_ns->ns.inum);

	if (!IS_ERR(vk->debugfs_dentry)) {
		char *tmp, *p = kmalloc(PATH_MAX, GFP_KERNEL_ACCOUNT);

		if (p) {
			tmp = dentry_path_raw(vk->debugfs_dentry, p, PATH_MAX);
			if (!IS_ERR(tmp))
				add_uevent_var(env, "STATS_PATH=%s", tmp);
			kfree(p);
		}
	}
	/* no need for checks, since we are adding at most only 5 keys */
	env->envp[env->envp_idx++] = NULL;
	kobject_uevent_env(&vkernel_dev.this_device->kobj, KOBJ_CHANGE, env->envp);
	kfree(env);
}

static int clear_zombie_vks(void)
{
	struct vkernel *vk;
	struct vkernel *tmp;
	struct task_struct *tsk;
	int count = 0;

	list_for_each_entry_safe(vk, tmp, &vk_list, link) {
		tsk = pid_task(find_pid_ns(vk->init_pid, &init_pid_ns), PIDTYPE_PID);
		if (tsk != vk->init_process) {
			if (refcount_read(&vk->users_count) > 1)
				pr_err("vkernel: BUG! zombie vk %s has other refs, init %d custom %s\n",
					vk->name, vk->init_pid, vk->custom->name);
			vkernel_put_vk(vk);
			count++;
		}
	}

	return count;
}

static int clear_zombie_set(void *data, u64 val)
{
	int count;

	count = clear_zombie_vks();
	pr_info("cleared %d zombie vks\n", count);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(clear_zombie_fops, NULL, clear_zombie_set,
			 "%lld\n");

static void vkernel_init_debug(void)
{
	vkernel_debugfs_dir = debugfs_create_dir("vkernel", NULL);

	debugfs_create_file("clear_zombie", 0200, vkernel_debugfs_dir,
			NULL, &clear_zombie_fops);
}

int vkernel_init(void)
{
	int ret;

	if (vk_kallsyms_init())
		return -1;
	if (vk_cap_init())
		return -1;
	if (vk_syscall_init())
		return -1;
	if (vk_acl_init())
		return -1;

	vkernel_init_debug();

	ret = misc_register(&vkernel_dev);
	if (ret) {
		pr_err("vkernel: misc device register failed\n");
		return ret;
	}

	vkernel_register_custom(&default_custom);
	vkernel_register_custom(&analysis_custom);
	pr_info("vkernel: load vkernel\n");

	return 0;
}
EXPORT_SYMBOL(vkernel_init);

void vkernel_exit(void)
{
	clear_zombie_vks();

	pr_info("vkernel: unlod vkernel\n");
	vkernel_unregister_custom(&analysis_custom);
	vkernel_unregister_custom(&default_custom);

	misc_deregister(&vkernel_dev);

	debugfs_remove_recursive(vkernel_debugfs_dir);

	vk_acl_uninit();
	vk_syscall_uninit();
	vk_cap_uninit();
	vk_kallsyms_uninit();
}
EXPORT_SYMBOL(vkernel_exit);

module_init(vkernel_init);
module_exit(vkernel_exit);
