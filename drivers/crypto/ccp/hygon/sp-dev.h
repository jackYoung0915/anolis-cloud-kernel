/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HYGON Secure Processor interface
 *
 * Copyright (C) 2024 Hygon Info Technologies Ltd.
 *
 * Author: Liyang Han <hanliyang@hygon.cn>
 */

#ifndef __CCP_HYGON_SP_DEV_H__
#define __CCP_HYGON_SP_DEV_H__

#include <linux/processor.h>

#ifdef CONFIG_X86_64
static inline bool is_vendor_hygon(void)
{
	return boot_cpu_data.x86_vendor == X86_VENDOR_HYGON;
}
#else
static inline bool is_vendor_hygon(void) { return false; }
#endif

#endif  /* __CCP_HYGON_SP_DEV_H__ */
