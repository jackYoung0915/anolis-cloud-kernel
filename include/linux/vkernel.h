/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#ifndef _LINUX_VKERNEL_H
#define _LINUX_VKERNEL_H

#include <linux/types.h>
#include <linux/sched.h>
#include <asm/syscall.h>
#include <linux/hashtable.h>
#include <linux/pid_namespace.h>
#include <linux/utsname.h>
#include <linux/capability.h>
#include <linux/percpu_counter.h>
#include <linux/mempolicy.h>
#include <linux/fs.h>

#include <net/busy_poll.h>
#include <net/tcp.h>

#include <asm/vkernel.h>

#define VKERNEL_API_VERSION	1

#define VKERNEL_NAME_LEN 64
#define VKERNEL_PATH_MAX 128
#define VKERNEL_ACL_HASH_BITS 8

#define NOT_FOUND 0x8000
#define IOP_VKERNEL_REG 0x8000
#define IOP_VKERNEL_DIR 0x4000

/* Refer KVM */
#define VKERNELIO 0xAF

/* System/VK IOCTL list */
#define VKERNEL_GET_API_VERSION		_IO(VKERNELIO, 0x00)
#define VKERNEL_CREATE_VK			_IO(VKERNELIO, 0x01)
#define VKERNEL_DESTROY_VK			_IO(VKERNELIO, 0x02)
#define VKERNEL_CHECK_EXTENSION		_IO(VKERNELIO, 0x03)
#define VKERNEL_TRACE_ENABLE		_IO(VKERNELIO, 0x04)
#define VKERNEL_TRACE_PAUSE			_IO(VKERNELIO, 0x05)
#define VKERNEL_TRACE_DISABLE		_IO(VKERNELIO, 0x06)
#define VKERNEL_SET_DEF_SYSCALL		_IO(VKERNELIO, 0x07)
#define VKERNEL_RESTRICT_SYSCALL	_IO(VKERNELIO, 0x08)
#define VKERNEL_RESTRICT_FILE		_IO(VKERNELIO, 0x09)
#define VKERNEL_RESTRICT_LINUX_CAP	_IO(VKERNELIO, 0x0a)
#define VKERNEL_SET_CPU_PREF		_IO(VKERNELIO, 0X0b)
#define VKERNEL_SET_MEMORY_PREF		_IO(VKERNELIO, 0X0c)
#define VKERNEL_SET_SYSCTL_FS		_IO(VKERNELIO, 0X0d)
#define VKERNEL_SET_SYSCTL_KERNEL	_IO(VKERNELIO, 0x0e)
#define VKERNEL_SET_SYSCTL_NET		_IO(VKERNELIO, 0x0f)
#define VKERNEL_SET_SYSCTL_VM		_IO(VKERNELIO, 0x10)
#define VKERNEL_ENABLE_CAP			_IO(VKERNELIO, 0x11)
#define VKERNEL_REGISTER			_IO(VKERNELIO, 0x12)
#define VKERNEL_UNREGISTER			_IO(VKERNELIO, 0x13)
#define VKERNEL_ACTIVATE			_IO(VKERNELIO, 0x14)
#define VKERNEL_DEACTIVATE			_IO(VKERNELIO, 0x15)

/* syscall condition compare operations */
#define VKERNEL_SYSCALL_CMP_ED		0 /* invalid op, means the end of conditions */
#define VKERNEL_SYSCALL_CMP_EQ		1 /* equal, arg == val */
#define VKERNEL_SYSCALL_CMP_NE		2 /* not equal, arg != val */
#define VKERNEL_SYSCALL_CMP_LT		3 /* less than, arg < val */
#define VKERNEL_SYSCALL_CMP_LE		4 /* less than or equal, arg <= val */
#define VKERNEL_SYSCALL_CMP_GT		5 /* greater than, arg > val */
#define VKERNEL_SYSCALL_CMP_GE		6 /* greater than or equal, arg >= val */
#define VKRENEL_SYSCALL_CMP_ME		7 /* masked equal, arg & mask == val, mask is val1 */

/* syscall rule actions */
#define VKERNEL_SYSCALL_ACT_INVALID			0
#define VKERNEL_SYSCALL_ACT_KILL_PROCESS	1
#define VKERNEL_SYSCALL_ACT_KILL_THREAD		2
#define VKERNEL_SYSCALL_ACT_TRAP			3
#define VKERNEL_SYSCALL_ACT_ERRNO			4
#define VKERNEL_SYSCALL_ACT_USER_NOTIF		5
#define VKERNEL_SYSCALL_ACT_TRACE			6
#define VKERNEL_SYSCALL_ACT_LOG				7
#define VKERNEL_SYSCALL_ACT_ALLOW			8

#define VKERNEL_SYSCALL_ACT_BITS		16
#define VKERNEL_SYSCALL_ERRNO_BITS		16
#define VKERNEL_SYSCALL_ERRNO_MASK		((1U << VKERNEL_SYSCALL_ERRNO_BITS) - 1)

#define current_vk_task	get_current_syscall_task()
#define current_vk		get_current_syscall_vk()

struct vkernel_desc {
	char custom[VKERNEL_NAME_LEN];
	int pid;
};

struct vkernel_syscall_cond {
	u16 index; /* argument index 0-5 */
	u16 op; /* compare option */
	unsigned long oprand1; /* compared value */
	unsigned long oprand2; /* optional masked value */
};

struct vkernel_syscall_rule_desc {
	u32 nr; /* syscall nr */
	u32 act; /* action when conditions matched, [31:16]act|[15:0]errno */
	struct vkernel_syscall_cond conds[6]; /* optional conditions */
};

struct vkernel_syscall_rule {
	struct list_head link;
	u32 act;
	struct vkernel_syscall_cond conds[6];
};

struct vkernel_syscall {
	sys_call_vk_t table[NR_syscalls + 1];
	struct list_head rule_chains[NR_syscalls + 1];
	long (*do_futex)(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
			u32 __user *uaddr2, u32 val2, u32 val3);
	u32 def_act; /* default syscall rule action, [31:16]act|[15:0]errno */
};

struct vkernel_file_desc {
	char path[VKERNEL_PATH_MAX];
	u16 mode;
};

struct vkernel_file_desc_set {
	u64 nr_descs;
	struct vkernel_file_desc descs[];
};

struct vkernel_acl_node {
	struct hlist_node hash;
	struct list_head link;
	unsigned long ino;
	struct super_block *sb;
	char path[VKERNEL_PATH_MAX];
	unsigned short mode;
};

struct vkernel_acl {
	struct hlist_head *ht;
	int bits;
	struct list_head nodes;
	bool active;
};

struct vkernel_linux_cap {
	kernel_cap_t inheritable;
	kernel_cap_t permitted;
	kernel_cap_t effective;
	kernel_cap_t bset;
	kernel_cap_t ambient;
};

struct vkernel;

struct vkernel_ops {
	int (*cap_capable)(struct vkernel *vk, const struct cred *cred,
			struct user_namespace *targ_ns,	int cap, unsigned int opts);
	int (*generic_permission)(struct vkernel *vk, struct mnt_idmap *idmap,
			struct inode *inode, int mask);
};

struct vkernel_custom_type {
	struct module *owner;
	struct hlist_node hash;
	char name[VKERNEL_NAME_LEN];
	int (*post_create)(struct vkernel *vk);
	void (*pre_destroy)(struct vkernel *vk);
};

struct vkernel {
	/* basic */
	char name[VKERNEL_NAME_LEN];
	struct hlist_node hash;
	struct list_head link;
	struct pid_namespace *pid_ns;
	struct uts_namespace *uts_ns;
	struct task_struct *init_process;
	int init_pid;
	refcount_t users_count;
	bool active;

	/* security */
	struct vkernel_syscall syscall;
	struct vkernel_acl acl;
	struct vkernel_linux_cap linux_cap;

	/* operation */
	struct vkernel_ops ops;

	/* custom */
	struct vkernel_custom_type *custom;
	void *private;

	/* debug */
	struct dentry *debugfs_dentry;
};

struct vkernel *vkernel_find_vk_by_id(unsigned int id);
struct vkernel *vkernel_find_vk_by_task(struct task_struct *tsk);
int vkernel_register_vk(struct vkernel *vk);
int vkernel_unregister_vk(struct vkernel *vk);

struct vkernel *vkernel_create_vk(struct task_struct *tsk, const char *name,
			const char *custom);
void vkernel_destroy_vk(struct vkernel *vk);

void vkernel_get_vk(struct vkernel *vk);
bool vkernel_get_vk_safe(struct vkernel *vk);
void vkernel_put_vk(struct vkernel *vk);
void vkernel_put_vk_no_destroy(struct vkernel *vk);

int vkernel_set_syscall(struct vkernel_syscall *syscall, unsigned int nr,
			sys_call_vk_t call);
int vkernel_set_default_syscall_rule(struct vkernel_syscall *syscall, u32 act);
int vkernel_add_syscall_rule(struct vkernel_syscall *syscall,
			struct vkernel_syscall_rule_desc *desc);

int vkernel_set_acl(struct vkernel_acl *acl, char *path, unsigned short mode);
int vkernel_clear_acl(struct vkernel_acl *acl, char *path);
int vkernel_set_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set);
int vkernel_clear_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set);

int vkernel_set_linux_cap(struct vkernel *vk, struct vkernel_linux_cap *cap);

struct vkernel_custom_type *vkernel_find_custom(const char *name);
int vkernel_register_custom(struct vkernel_custom_type *custom);
int vkernel_unregister_custom(struct vkernel_custom_type *custom);

#endif
