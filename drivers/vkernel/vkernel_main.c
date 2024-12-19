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

	/* Init default operations */
	vk->ops.cap_capable = vk_cap_capable;
	vk->ops.generic_permission = vk_generic_permission;

	r = vkernel_create_vk_debugfs(vk, name);
	if (r)
		goto err_acl;

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
	case VKERNEL_SET_SYSCTL_FS:
	case VKERNEL_SET_SYSCTL_KERNEL:
	case VKERNEL_SET_SYSCTL_NET:
	case VKERNEL_SET_SYSCTL_VM:
	case VKERNEL_CHECK_EXTENSION:
	case VKERNEL_ENABLE_CAP:
		r = -EOPNOTSUPP;
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
		r = -EOPNOTSUPP;
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
