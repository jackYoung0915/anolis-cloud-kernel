// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/slab.h>
#include <linux/kallsyms.h>
#include "kprobe.h"
#include "help.h"
#include "group.h"

static kprobe_opcode_t *query_kallsym_addr;

/* Get the function arg with index of 'index' from 'regs'.
 *
 * This is just another version of the 'KPROBE_PARM'.
 */
unsigned long kprobe_parm(struct pt_regs *regs, int index)
{
	switch (index) {
	case 1:
		return PT_REGS_PARM1(regs);
	case 2:
		return PT_REGS_PARM2(regs);
	case 3:
		return PT_REGS_PARM3(regs);
	case 4:
		return PT_REGS_PARM4(regs);
	case 5:
		return PT_REGS_PARM5(regs);
	default:
		return 0;
	}
}

/* Declare the kprobe with function call.
 */
struct kprobe *kprobe_declare(const char *sym, void *handler)
{
	struct kprobe *p = kcalloc(1, sizeof(struct kprobe), GFP_KERNEL);

	if (!p)
		return NULL;

	p->symbol_name = sym;
	p->post_handler = handler;

	return p;
}

static int skip_entry_handler(struct kretprobe_instance *ri,
							   struct pt_regs *regs)
{
	return 0;
}

/* Declare the kretprobe with function call.
 */
struct kretprobe *kretprobe_declare(const char *sym, void *handler,
									 void *entry_handler)
{
	struct kretprobe *p = kcalloc(1, sizeof(struct kretprobe), GFP_KERNEL);

	if (!p)
		return NULL;

	if (entry_handler == NULL)
		entry_handler = skip_entry_handler;

	p->entry_handler	= entry_handler;
	p->kp.symbol_name	= sym;
	p->handler = handler;

	return p;
}

int filter_syms(void *data, const char *name_buf, unsigned long address)
{
	char *name = data;

	if (strstarts(name_buf, name)) {
		if (strlen(name_buf) > MAX_TP_NAME) {
			log_err("[nettrace: %s] func name is too long: %s\n", __func__, name_buf);
			return -1;
		}
		strscpy(name, name_buf, MAX_TP_NAME);
		query_kallsym_addr = (kprobe_opcode_t *) address;
		return -1;
	}
	return 0;
}

/* query the address of syms in kallsyms.
 */
int query_kallsyms(char *name)
{
	if (strlen(name) + 1 >= MAX_TP_NAME) {
		log_err("[nettrace: %s] func name is too long: %s\n", __func__, name);
		return -1;
	}
	strncat(name, ".", 1);
	return kallsyms_on_each_symbol(filter_syms, name);
}

/* register the kprobe with function call.
 */
int c_register_kprobe(struct kprobe *p)
{
	if (register_kprobe(p) < 0) {
		char name[MAX_TP_NAME] = {};

		strscpy(name, p->symbol_name, sizeof(name));
		if (query_kallsyms(name)) {
			pr_alert("[nettrace] Note: The function we want to trace (%s) has become %s.\n",
					(char *) p->symbol_name, name);
			pr_alert("bacause the kernel compiler had added a suffix in its symbol!\n");
			pr_alert("There may be several versions of %s, please check /proc/kallsyms.\n",
					(char *) p->symbol_name);
			strscpy((char *) p->symbol_name, name, sizeof(name));
			if (register_kprobe(p) < 0)
				goto on_err;
		} else {
			goto on_err;
		}
	}

	log_info("  planted kprobe at %p, name %s\n", p->addr, p->symbol_name);
	return 0;

on_err:
	return -1;
}

/* register the kprobe with function call.
 */
int c_register_kretprobe(struct kretprobe *p)
{
	if (register_kretprobe(p) < 0) {
		char name[MAX_TP_NAME] = {};

		strscpy(name, p->kp.symbol_name, sizeof(name));
		if (query_kallsyms(name)) {
			pr_alert("[nettrace] Note: The function we want to trace (%s) has become %s.\n",
					(char *) p->kp.symbol_name, name);
			pr_alert("bacause the kernel compiler had added a suffix in its symbol!\n");
			pr_alert("There may be several versions of %s, please check /proc/kallsyms\n",
					(char *) p->kp.symbol_name);
			strscpy((char *) p->kp.symbol_name, name, sizeof(name));
			if (register_kretprobe(p) < 0)
				goto on_err;
		} else {
			goto on_err;
		}
	}

	log_info("  planted kretprobe at %p, name %s\n", p->kp.addr, p->kp.symbol_name);
	return 0;

on_err:
	log_err("  register kretprobe failed: %s\n", p->kp.symbol_name);
	return -1;
}

/* unregister the krpobe with function call.
 */
void c_unregister_kprobe(struct kprobe *p)
{
	unregister_kprobe(p);
	log_info("kprobe at %s unregistered\n", p->symbol_name);
}

/* unregister the kretprobe with function call.
 */
void c_unregister_kretprobe(struct kretprobe *p)
{
	unregister_kretprobe(p);
	log_info("kretprobe at %s unregistered\n", p->kp.symbol_name);
}
