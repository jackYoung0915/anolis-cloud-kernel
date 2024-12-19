// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>
#include <linux/debugfs.h>

#include "syscall.h"
#include "utils.h"

sys_call_vk_t *sys_call_table_ptr;

int (*force_sig_seccomp_ptr)(int syscall, int reason, bool force_coredump);
void (*do_exit_ptr)(long code);

#define NOTIF_SYSCALL_RULE(name) \
{ \
	.nr = __NR_##name, \
	.act = (VKERNEL_SYSCALL_ACT_ERRNO << VKERNEL_SYSCALL_ERRNO_BITS) | ENOSYS, \
} \

static struct vkernel_syscall_rule_desc def_rules[] = {
	NOTIF_SYSCALL_RULE(move_pages),
	NOTIF_SYSCALL_RULE(fsconfig),
	NOTIF_SYSCALL_RULE(kexec_load),
	// NOTIF_SYSCALL_RULE(sysfs),
	NOTIF_SYSCALL_RULE(fsopen),
	NOTIF_SYSCALL_RULE(pkey_mprotect),
	// NOTIF_SYSCALL_RULE(ustat),
	NOTIF_SYSCALL_RULE(pkey_free),
	NOTIF_SYSCALL_RULE(pkey_alloc),
	NOTIF_SYSCALL_RULE(userfaultfd),
	NOTIF_SYSCALL_RULE(migrate_pages),
	NOTIF_SYSCALL_RULE(add_key),
	NOTIF_SYSCALL_RULE(keyctl),
	NOTIF_SYSCALL_RULE(clone3),
	NOTIF_SYSCALL_RULE(kexec_file_load),
	NOTIF_SYSCALL_RULE(swapoff),
	NOTIF_SYSCALL_RULE(fsmount),
	NOTIF_SYSCALL_RULE(open_tree),
	// NOTIF_SYSCALL_RULE(_sysctl),
	NOTIF_SYSCALL_RULE(move_mount),
	NOTIF_SYSCALL_RULE(swapon),
	NOTIF_SYSCALL_RULE(pivot_root),
	NOTIF_SYSCALL_RULE(fspick),
};

static struct kmem_cache *syscall_rule_cache;

int vk_syscall_init(void)
{
	sys_call_table_ptr = (void *)lookup_name("sys_call_table");
	if (!sys_call_table_ptr) {
		pr_err("failed to find sys_call_table\n");
		return -1;
	}

	force_sig_seccomp_ptr = (void *)lookup_name("force_sig_seccomp");
	if (!force_sig_seccomp_ptr) {
		pr_err("failed to find force_sig_seccomp\n");
		return -1;
	}

	do_exit_ptr = (void *)lookup_name("do_exit");
	if (!force_sig_seccomp_ptr) {
		pr_err("failed to find do_exit\n");
		return -1;
	}

	syscall_rule_cache = kmem_cache_create("vkernel_syscall_rule",
			sizeof(struct vkernel_syscall_rule), 0, SLAB_ACCOUNT, NULL);
	if (!syscall_rule_cache) {
		pr_err("failed to create slab for syscall rule\n");
		return -ENOMEM;
	}

	return 0;
}

void vk_syscall_uninit(void)
{
	kmem_cache_destroy(syscall_rule_cache);
}

static inline bool check_cond(int op, unsigned long arg,
		unsigned long oprand1, unsigned long oprand2)
{
	switch (op) {
	case VKERNEL_SYSCALL_CMP_EQ:
		return arg == oprand1;
	case VKERNEL_SYSCALL_CMP_NE:
		return arg != oprand1;
	case VKERNEL_SYSCALL_CMP_LT:
		return arg < oprand1;
	case VKERNEL_SYSCALL_CMP_LE:
		return arg <= oprand1;
	case VKERNEL_SYSCALL_CMP_GT:
		return arg > oprand1;
	case VKERNEL_SYSCALL_CMP_GE:
		return arg >= oprand1;
	case VKRENEL_SYSCALL_CMP_ME:
		return (arg & oprand1) == oprand2;
	}

	return false;
}


static bool check_rule(struct vkernel_syscall_rule *rule, struct pt_regs *regs)
{
	struct vkernel_syscall_cond *cond;
	unsigned long args[6];
	int i;

	/* Corner case */
	if (!rule)
		return true;

	syscall_get_arguments(current, regs, args);
	for (i = 0; i < 6; i++) {
		cond = &rule->conds[i];
		if (cond->op == VKERNEL_SYSCALL_CMP_ED)
			break;
		if (!check_cond(cond->op, args[cond->index], cond->oprand1, cond->oprand2))
			return false;
	}

	return true;
}

asmlinkage long vk_sys_act_cond(const struct pt_regs *regs)
{
	struct vkernel *vk;
	struct vkernel_syscall_rule *rule;
	struct pt_regs *curr_regs;
	int nr;
	unsigned int act;

	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	if (likely(current_vk_task == current))
		vk = current_vk;
	else
		vk = vkernel_find_vk_by_task(current);

	act = vk->syscall.def_act;
	list_for_each_entry(rule, &vk->syscall.rule_chains[nr], link) {
		if (check_rule(rule, curr_regs)) {
			act = rule->act;
			break;
		}
	}

	switch (act >> VKERNEL_SYSCALL_ERRNO_BITS) {
	case VKERNEL_SYSCALL_ACT_TRAP:
		pr_info("vkernel: cond trap for syscall %d\n", nr);
		syscall_rollback(current, curr_regs);
		force_sig_seccomp_ptr(nr, -EPERM, false);
		fallthrough;
	case VKERNEL_SYSCALL_ACT_ERRNO:
		pr_info("vkernel: cond err for syscall %d\n", nr);
		return -(act & VKERNEL_SYSCALL_ERRNO_MASK);

	case VKERNEL_SYSCALL_ACT_USER_NOTIF:
		pr_info("vkernel: cond user notif (nosys) for syscall %d\n", nr);
		return -ENOSYS;

	case VKERNEL_SYSCALL_ACT_TRACE:
		pr_info("vkernel: cond trace (nosys) for syscall %d\n", nr);
		return -ENOSYS;

	case VKERNEL_SYSCALL_ACT_LOG:
		pr_info("vkernel: cond log for syscall %d\n", nr);
		fallthrough;
	case VKERNEL_SYSCALL_ACT_ALLOW:
		return sys_call_table_ptr[nr](regs);

	case VKERNEL_SYSCALL_ACT_KILL_PROCESS:
	case VKERNEL_SYSCALL_ACT_KILL_THREAD:
	default:
		pr_info("vkernel: cond kill process/thread for syscall %d\n", nr);
		if ((act >> VKERNEL_SYSCALL_ERRNO_BITS) != SECCOMP_RET_KILL_THREAD ||
		    (atomic_read(&current->signal->live) == 1)) {
			/* Show the original registers in the dump. */
			syscall_rollback(current, curr_regs);
			/* Trigger a coredump with SIGSYS */
			force_sig_seccomp_ptr(nr, -EPERM, true);
		} else {
			/* Call do_exit since there is missing unified pt_reg api */
			do_exit_ptr(SIGSYS);
		}
		return -1;
	}

	/* We never get here */
	unreachable();

	return -1;
}

asmlinkage long vk_sys_act_invalid(const struct pt_regs *regs)
{
	pr_info("invalid syscall, never get here\n");
	return -ENOSYS;
}

asmlinkage long vk_sys_act_kill_process(const struct pt_regs *regs)
{
	struct pt_regs *curr_regs;
	int nr;

	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	pr_info("vkernel: kill process for syscall %d\n", nr);
	syscall_rollback(current, curr_regs);
	force_sig_seccomp_ptr(nr, -EPERM, true);

	return -1;
}

asmlinkage long vk_sys_act_kill_thread(const struct pt_regs *regs)
{
	struct pt_regs *curr_regs;
	int nr;

	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	pr_info("vkernel: kill thread for syscall %d\n", nr);
	if ((atomic_read(&current->signal->live) == 1)) {
		syscall_rollback(current, current_pt_regs());
		force_sig_seccomp_ptr(nr, -EPERM, true);
	} else {
		/* Call do_exit since there is missing unified pt_reg api */
		do_exit_ptr(SIGSYS);
	}

	return -1;
}

asmlinkage long vk_sys_act_trap(const struct pt_regs *regs)
{
	struct pt_regs *curr_regs;
	int nr;

	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	pr_info("vkernel: trap for syscall %d\n", nr);
	syscall_rollback(current, curr_regs);
	force_sig_seccomp_ptr(nr, -EPERM, false);

	return -1;
}

asmlinkage long vk_sys_act_user_notif(const struct pt_regs *regs)
{
	pr_err("vkernel: user notif for syscall nr %d\n",
			syscall_get_nr(current, current_pt_regs()));
	return -ENOSYS;
}

asmlinkage long vk_sys_act_trace(const struct pt_regs *regs)
{
	pr_err("vkernel: trace for syscall nr %d\n",
			syscall_get_nr(current, current_pt_regs()));
	return -ENOSYS;
}

asmlinkage long vk_sys_act_errno(const struct pt_regs *regs)
{
	struct vkernel *vk;
	struct vkernel_syscall_rule *rule;
	struct pt_regs *curr_regs;
	int nr;
	int errno;

	if (likely(current_vk_task == current))
		vk = current_vk;
	else
		vk = vkernel_find_vk_by_task(current);
	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	if (list_empty(&vk->syscall.rule_chains[nr]))
		errno = vk->syscall.def_act & 0xffff;
	else {
		rule = list_first_entry(&vk->syscall.rule_chains[nr],
				struct vkernel_syscall_rule, link);
		errno = rule->act & VKERNEL_SYSCALL_ERRNO_MASK;
	}

	pr_err("vkernel: err for syscall nr %d errno -%d\n", nr, errno);
	return -errno;
}

asmlinkage long vk_sys_act_log(const struct pt_regs *regs)
{
	int nr;

	nr = syscall_get_nr(current, current_pt_regs());
	pr_info("vkernel: log for syscall %d\n", nr);

	return sys_call_table_ptr[nr](regs);
}

static void clear_syscall_rule_chain(struct list_head *chain)
{
	struct vkernel_syscall_rule *rule;
	struct vkernel_syscall_rule *tmp;

	list_for_each_entry_safe(rule, tmp, chain, link) {
		list_del(&rule->link);
		kmem_cache_free(syscall_rule_cache, rule);
	}
	INIT_LIST_HEAD(chain);
}

int vk_init_syscall(struct vkernel_syscall *syscall)
{
	int i;

	for (i = 0; i < NR_syscalls; i++) {
		syscall->table[i] = sys_call_table_ptr[i];
		INIT_LIST_HEAD(&syscall->rule_chains[i]);
	}
	syscall->def_act = VKERNEL_SYSCALL_ACT_ALLOW << VKERNEL_SYSCALL_ERRNO_BITS;

	return 0;
}

void vk_uninit_syscall(struct vkernel_syscall *syscall)
{
	int i;

	for (i = 0; i < NR_syscalls; i++)
		clear_syscall_rule_chain(&syscall->rule_chains[i]);
}

int vkernel_set_syscall(struct vkernel_syscall *syscall, unsigned int nr,
			sys_call_vk_t call)
{
	if (unlikely(nr >= NR_syscalls))
		return -EINVAL;

	clear_syscall_rule_chain(&syscall->rule_chains[nr]);
	syscall->table[nr] = call;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_syscall);

static sys_call_vk_t uncond_table[] = {
	[VKERNEL_SYSCALL_ACT_INVALID] = vk_sys_act_invalid,
	[VKERNEL_SYSCALL_ACT_KILL_PROCESS] = vk_sys_act_kill_process,
	[VKERNEL_SYSCALL_ACT_KILL_THREAD] = vk_sys_act_kill_thread,
	[VKERNEL_SYSCALL_ACT_TRAP] = vk_sys_act_trap,
	[VKERNEL_SYSCALL_ACT_ERRNO] = vk_sys_act_errno,
	[VKERNEL_SYSCALL_ACT_USER_NOTIF] = vk_sys_act_user_notif,
	[VKERNEL_SYSCALL_ACT_TRACE] = vk_sys_act_trace,
	[VKERNEL_SYSCALL_ACT_LOG] = vk_sys_act_log,
};

/*
 * Call before adding rules
 */
int vkernel_set_default_syscall_rule(struct vkernel_syscall *syscall, u32 act)
{
	unsigned int action;
	int i;

	action = act >> VKERNEL_SYSCALL_ERRNO_BITS;
	if (action == VKERNEL_SYSCALL_ACT_INVALID ||
		action > VKERNEL_SYSCALL_ACT_ALLOW ||
		act == syscall->def_act) {
		pr_err("invalid default rule, act 0x%x, old 0x%x\n", act, syscall->def_act);
		return -EINVAL;
	}

	for (i = 0; i < NR_syscalls; i++) {
		clear_syscall_rule_chain(&syscall->rule_chains[i]);
		if (action < VKERNEL_SYSCALL_ACT_ALLOW)
			syscall->table[i] = uncond_table[action];
		else
			syscall->table[i] = sys_call_table_ptr[i];
	}
	syscall->def_act = act;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_default_syscall_rule);

int vkernel_add_syscall_rule(struct vkernel_syscall *syscall,
			struct vkernel_syscall_rule_desc *desc)
{
	struct vkernel_syscall_rule *rule;
	unsigned int nr;
	unsigned int action;
	int index;

	pr_debug("set syscall rule, nr %u act 0x%x has_cond %d\n",
			desc->nr, desc->act, desc->conds[0].op != VKERNEL_SYSCALL_CMP_ED);

	nr = desc->nr;
	action = (desc->act >> VKERNEL_SYSCALL_ERRNO_BITS);
	if (nr >= NR_syscalls ||
		action == VKERNEL_SYSCALL_ACT_INVALID ||
		action > VKERNEL_SYSCALL_ACT_ALLOW ||
		(desc->act == syscall->def_act && list_empty(&syscall->rule_chains[nr]))) {
		pr_err("invalid rule, nr %u act 0x%x def_act 0x%x\n",
				desc->nr, desc->act, syscall->def_act);
		return -EINVAL;
	}

	/* Update syscall rule chain */
	rule = kmem_cache_alloc(syscall_rule_cache, GFP_KERNEL_ACCOUNT);
	if (!rule) {
		pr_err("failed to alloc syscall rule\n");
		return -ENOMEM;
	}

	rule->act = desc->act;
	for (index = 0; index < 6; index++) {
		rule->conds[index] = desc->conds[index];
		if (desc->conds[index].op == VKERNEL_SYSCALL_CMP_ED)
			break;
	}
	list_add(&rule->link, &syscall->rule_chains[nr]);

	/* Update syscall table */
	if (index > 0)
		syscall->table[nr] = vk_sys_act_cond;
	else if (action < VKERNEL_SYSCALL_ACT_ALLOW)
		syscall->table[nr] = uncond_table[action];
	else
		syscall->table[nr] = sys_call_table_ptr[nr];

	return 0;
}
EXPORT_SYMBOL(vkernel_add_syscall_rule);

void vk_install_default_syscalls(struct vkernel_syscall *syscall)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(def_rules); i++)
		vkernel_add_syscall_rule(syscall, &def_rules[i]);
}
EXPORT_SYMBOL(vk_install_default_syscalls);


struct vkernel_analysis {
	unsigned int syscalls[NR_syscalls + 1];
	unsigned int exec_count;
	unsigned int exec_capacity;
	char *execs[];
};

asmlinkage long vk_sys_act_analysis(const struct pt_regs *regs)
{
	struct vkernel *vk;
	struct vkernel_analysis *data;
	struct vkernel_analysis *newdata;
	char __user *uname;
	char *kname;
	struct pt_regs *curr_regs;
	int nr;

	if (likely(current_vk_task == current))
		vk = current_vk;
	else
		vk = vkernel_find_vk_by_task(current);
	data = (struct vkernel_analysis *)vk->private;
	curr_regs = current_pt_regs();
	nr = syscall_get_nr(current, curr_regs);
	if (data->syscalls[nr] < UINT_MAX)
		data->syscalls[nr]++;
	if (nr == __NR_execve || nr == __NR_execveat) {
		kname = __getname();
		if (unlikely(!kname)) {
			pr_err("failed to alloc name\n");
			return -ENOMEM;
		}
		if (nr == __NR_execve)
			uname = (char __user *)regs_get_kernel_argument(curr_regs, 0);
		else
			uname = (char __user *)regs_get_kernel_argument(curr_regs, 1);
		if (strncpy_from_user(kname, uname, PATH_MAX) < 0) {
			pr_err("failed to copy user filename\n");
			__putname(kname);
			return -EFAULT;
		}
		if (data->exec_count >= data->exec_capacity) {
			newdata = kzalloc(sizeof(*data) +
					sizeof(char *) * (data->exec_capacity << 1), GFP_KERNEL);
			if (!newdata)
				return -ENOMEM;
			memcpy(newdata, data, sizeof(*data) + sizeof(char *) * data->exec_capacity);
			newdata->exec_capacity <<= 1;

			vk->private = newdata;
			/* TODO: fix race window */
			while (refcount_read(&vk->users_count) > 1)
				;
			kfree(data);
			data = newdata;
		}
		data->execs[data->exec_count++] = kname;
	}

	return sys_call_table_ptr[nr](regs);
}

static int analysis_show(struct seq_file *m, void *v)
{
	struct vkernel *vk = m->private;
	struct vkernel_analysis *data = vk->private;
	unsigned int i;
	bool first;

	seq_puts(m, "{\n");
	seq_puts(m, "  \"syscalls\": [");
	first = true;
	for (i = 0; i < NR_syscalls; i++) {
		if (!data->syscalls[i])
			continue;
		if (first) {
			seq_printf(m, "%u", i);
			first = false;
		} else
			seq_printf(m, ", %u", i);
	}
	seq_puts(m, "],\n");
	seq_puts(m, "  \"execs\": [\n");
	first = true;
	for (i = 0; i < data->exec_count; i++) {
		if (unlikely(!data->execs[i])) {
			pr_warn("encounter nil exec path in vkernel_analysis\n");
			continue;
		}
		if (first) {
			seq_printf(m, "    \"%s\"", data->execs[i]);
			first = false;
		} else
			seq_printf(m, ",\n    \"%s\"", data->execs[i]);
	}
	seq_puts(m, "\n  ],\n");
	seq_puts(m, "  \"syscall_details\": [\n");
	first = true;
	for (i = 0; i < NR_syscalls; i++) {
		if (!data->syscalls[i])
			continue;
		if (first) {
			seq_printf(m, "    {\"nr\": %u, \"count\": %u}", i, data->syscalls[i]);
			first = false;
		} else
			seq_printf(m, ",\n    {\"nr\": %u, \"count\": %u}", i, data->syscalls[i]);
	}
	seq_puts(m, "\n  ]\n");
	seq_puts(m, "}\n");

	return 0;
}

static int analysis_open(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;
	int r;

	if (!vkernel_get_vk_safe(vk))
		return -ENOENT;

	r = single_open(file, analysis_show, inode->i_private);
	if (r < 0)
		vkernel_put_vk(vk);

	return r;
}

static int analysis_release(struct inode *inode, struct file *file)
{
	struct vkernel *vk = inode->i_private;

	vkernel_put_vk(vk);

	return single_release(inode, file);
}

static const struct file_operations analysis_fops = {
	.open = analysis_open,
	.release = analysis_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int analysis_post_create(struct vkernel *vk)
{
	struct vkernel_analysis *data;
	struct vkernel_syscall *syscall;
	int i;

	data = kzalloc(sizeof(*data) + sizeof(char *) * 64, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->exec_capacity = 4;
	vk->private = data;

	syscall = &vk->syscall;
	for (i = 0; i < NR_syscalls; i++)
		syscall->table[i] = vk_sys_act_analysis;

	debugfs_create_file("analysis", 0444, vk->debugfs_dentry, vk, &analysis_fops);

	return 0;
}

static void analysis_pre_destroy(struct vkernel *vk)
{
	struct vkernel_analysis *data = (struct vkernel_analysis *)vk->private;

	if (unlikely(!data)) {
		pr_warn("detroy an analysis vk without vkernel_analysis data\n");
		return;
	}

	kfree(data);
	vk->private = NULL;
}

struct vkernel_custom_type analysis_custom = {
	.owner = THIS_MODULE,
	.name = "analysis",
	.post_create = analysis_post_create,
	.pre_destroy = analysis_pre_destroy,
};
