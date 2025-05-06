// SPDX-License-Identifier: GPL-2.0
/*
 * Wrapper of lookup_name
 * Define the wrapper, so other components can include a function not a symbol
 *
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/kprobes.h>
#include <linux/version.h>

#include "utils.h"

/*
 * There are two ways of preventing vicious recursive loops when hooking:
 * - detect recusion using function return address (USE_FENTRY_OFFSET = 0)
 * - avoid recusion by jumping over the ftrace call (USE_FENTRY_OFFSET = 1)
 */
#define USE_FENTRY_OFFSET 0

/*
 * Tail call optimization can interfere with recursion detection based on
 * return address on the stack. Disable it to avoid machine hangups.
 */
#if !USE_FENTRY_OFFSET
#pragma GCC optimize("-fno-optimize-sibling-calls")
#endif

unsigned long vk_lookup_name(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long retval;

	if (register_kprobe(&kp) < 0)
		return 0;

	retval = (unsigned long)kp.addr;
	unregister_kprobe(&kp);

	return retval;
}

static unsigned long (*kallsyms_lookup_name_ptr)(const char *name);

int vk_kallsyms_init(void)
{
	kallsyms_lookup_name_ptr = (void *)vk_lookup_name("kallsyms_lookup_name");
	if (!kallsyms_lookup_name_ptr) {
		pr_err("cannot resolve symbol: kallsyms_lookup_name\n");
		return -ENOENT;
	}

	return 0;
}

void vk_kallsyms_uninit(void) {}

unsigned long lookup_name(const char *name)
{
	return kallsyms_lookup_name_ptr(name);
}
EXPORT_SYMBOL(lookup_name);
