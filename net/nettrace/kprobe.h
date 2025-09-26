/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Nettrace support.
 *
 * Copyright (C) 2022 ZTE Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KPROBE_COMMON_H
#define KPROBE_COMMON_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/moduleparam.h>

#define DECLARE_HANDLER_POST(func)					\
	static void post_handler_##func(				\
							struct kprobe *p,		\
							struct pt_regs *regs,	\
							unsigned long flags)
#define DECLARE_HANDLER_RET(func)							\
	static int ret_handler##func(							\
							struct kretprobe_instance *ri,	\
							struct pt_regs *regs)
#define DECLARE_HANDLER_ENTRY(func)							\
	static int entry_handler##func(							\
							struct kretprobe_instance *ri,	\
							struct pt_regs *regs)

#define DECLARE_KPROBE(func)					\
	DECLARE_HANDLER_POST(func);					\
	static struct kprobe kprobe_##func = {		\
		.symbol_name = #func,					\
		.post_handler = post_handler_##func};	\
	DECLARE_HANDLER_POST(func)

#define DECLARE_RETKPROBE(func)						\
	DECLARE_HANDLER_ENTRY(func);					\
	DECLARE_HANDLER_RET(func);						\
	static struct kretprobe kretprobe_##func = {	\
		.handler		= ret_handler##func,		\
		.entry_handler	= entry_handler##func,		\
		.kp = {.symbol_name = #func} };				\
	DECLARE_HANDLER_RET(func)

#define REGISTER_KPROBE(func)                                                \
	do {                                                                     \
		if (register_kprobe(&kprobe_##func) < 0) {                           \
			pr_info("register_kprobe failed:" #func "\n");          \
			return 0;                                                        \
		}                                                                    \
		pr_info("Planted kprobe at %p, handler addr %p, name %s\n", \
				kprobe_##func.addr, kprobe_##func.post_handler, #func);      \
	} while (0)

#define UNREGISTER_KPROBE(func)                                              \
	do {                                                                     \
		unregister_kprobe(&kprobe_##func);                                   \
		pr_info("kprobe at %p unregistered\n", kprobe_##func.addr); \
	} while (0)

#define PARAM_STRING(name, def)	\
	char *name = def;			\
	module_param(name, charp, 0)

#define PARAM_STRING_ARRAY(name, cmdname, length) \
	char *name[length];				  \
	int name##_len = 0;				  \
	module_param_array_named(cmdname, name, charp, &name##_len, 0)

#define PARAM_STRING_RW(name, default)	\
	char *name = default;				\
	module_param(name, charp, 0644)

#define PARAM_STRING_NAMED(name, cmdname, default)	\
	char *name = default;							\
	module_param_named(cmdname, name, charp, 0644)

#define PARAM_INT(name, default)	\
	int name = default;				\
	module_param(name, int, 0)

#define PARAM_INT_RW(name, default)	\
	int name = default;				\
	module_param(name, int, 0644)

#define PARAM_INT_NAMED(name, cmdname, default)	\
	int name = default;							\
	module_param_named(cmdname, name, int, 0644)

#define KPROBE_INIT						\
	static int __init kprobe_init(void);\
	module_init(kprobe_init);			\
	static int __init kprobe_init(void)

#define KPROBE_EXIT							\
	static void __exit kprobe_exit(void);	\
	module_exit(kprobe_exit);				\
	static void __exit kprobe_exit(void)

#if defined(__x86_64__)

#define PT_REGS_PARM1(x)	((x)->di)
#define PT_REGS_PARM2(x)	((x)->si)
#define PT_REGS_PARM3(x)	((x)->dx)
#define PT_REGS_PARM4(x)	((x)->cx)
#define PT_REGS_PARM5(x)	((x)->r8)
#define PT_REGS_RET(x)		((x)->sp)
#define PT_REGS_FP(x)		((x)->bp)
#define PT_REGS_RC(x)		((x)->ax)
#define PT_REGS_SP(x)		((x)->sp)

#elif defined(__s390x__)

#define PT_REGS_PARM1(x)	((x)->gprs[2])
#define PT_REGS_PARM2(x)	((x)->gprs[3])
#define PT_REGS_PARM3(x)	((x)->gprs[4])
#define PT_REGS_PARM4(x)	((x)->gprs[5])
#define PT_REGS_PARM5(x)	((x)->gprs[6])
#define PT_REGS_RET(x)		((x)->gprs[14])
#define PT_REGS_FP(x)		((x)->gprs[11]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x)		((x)->gprs[2])
#define PT_REGS_SP(x)		((x)->gprs[15])

#elif defined(__aarch64__)

#define PT_REGS_PARM1(x)	((x)->regs[0])
#define PT_REGS_PARM2(x)	((x)->regs[1])
#define PT_REGS_PARM3(x)	((x)->regs[2])
#define PT_REGS_PARM4(x)	((x)->regs[3])
#define PT_REGS_PARM5(x)	((x)->regs[4])
#define PT_REGS_RET(x)		((x)->regs[30])
#define PT_REGS_FP(x)		((x)->regs[29]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x)		((x)->regs[0])
#define PT_REGS_SP(x)		((x)->sp)

#elif defined(__arm__)

#define PT_REGS_PARM1(x) ((x)->uregs[0])
#define PT_REGS_PARM2(x) ((x)->uregs[1])
#define PT_REGS_PARM3(x) ((x)->uregs[2])
#define PT_REGS_PARM4(x) ((x)->uregs[3])
#define PT_REGS_PARM5(x) ((x)->uregs[4])
#define PT_REGS_RET(x) ((x)->uregs[14])
#define PT_REGS_FP(x) ((x)->uregs[11]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x) ((x)->uregs[0])
#define PT_REGS_SP(x) ((x)->uregs[13])
#define PT_REGS_IP(x) ((x)->uregs[12])

#elif defined(__mips__)

#define PT_REGS_PARM1(x) ((x)->regs[4])
#define PT_REGS_PARM2(x) ((x)->regs[5])
#define PT_REGS_PARM3(x) ((x)->regs[6])
#define PT_REGS_PARM4(x) ((x)->regs[7])
#define PT_REGS_PARM5(x) ((x)->regs[8])
#define PT_REGS_RET(x) ((x)->regs[31])
#define PT_REGS_FP(x) ((x)->regs[30]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x) ((x)->regs[2])
#define PT_REGS_SP(x) ((x)->regs[29])
#define PT_REGS_IP(x) ((x)->cp0_epc)

#elif defined(__powerpc__)

#define PT_REGS_PARM1(x) ((x)->gpr[3])
#define PT_REGS_PARM2(x) ((x)->gpr[4])
#define PT_REGS_PARM3(x) ((x)->gpr[5])
#define PT_REGS_PARM4(x) ((x)->gpr[6])
#define PT_REGS_PARM5(x) ((x)->gpr[7])
#define PT_REGS_RC(x) ((x)->gpr[3])
#define PT_REGS_SP(x) ((x)->sp)
#define PT_REGS_IP(x) ((x)->nip)

#endif

#define KPROBE_PARM(type, name, index) (type name = (type)PT_REGS_PARM##index(regs))
#define KPROBE_RET_PARM regs_return_value(regs)

//Below is the function encapsulation

extern unsigned long kprobe_parm(struct pt_regs *regs, int index);

extern struct kprobe *kprobe_declare(const char *sym, void *handler);

extern struct kretprobe *kretprobe_declare(const char *sym, void *handler, void *entry_handler);

extern int c_register_kprobe(struct kprobe *p);

extern int c_register_kretprobe(struct kretprobe *p);

extern void c_unregister_kprobe(struct kprobe *p);

extern void c_unregister_kretprobe(struct kretprobe *p);

MODULE_LICENSE("GPL");
#endif
