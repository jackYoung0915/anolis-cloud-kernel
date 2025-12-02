/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hygon China Secure Virtualization (CSV)
 *
 * Copyright (C) Hygon Info Technologies Ltd.
 *
 * Author: Jiang Xin <jiangxin@hygon.cn>
 */

#ifndef _ASM_X86_CSV_H
#define _ASM_X86_CSV_H

#include "csv_command.h"

#ifdef CONFIG_HYGON_CSV

#define CSV_MR_ALIGN_BITS		(28)

struct csv_mem {
	uint64_t start;
	uint64_t size;
};

extern struct csv_mem *csv_smr;
extern unsigned int csv_smr_num;
#ifdef CONFIG_SYSFS
extern atomic_long_t csv3_npt_size;
extern atomic_long_t csv3_pri_mem;
extern unsigned long csv3_meta;
extern atomic_long_t *csv3_shared_mem;
#endif	/* CONFIG_SYSFS */

void __init early_csv_reserve_mem(void);
phys_addr_t csv_alloc_from_contiguous(size_t size, nodemask_t *nodes_allowed,
				unsigned int align);
void csv_release_to_contiguous(phys_addr_t pa, size_t size);
uint32_t csv_get_smr_entry_shift(void);

int csv3_issue_request_report(phys_addr_t paddr, size_t size);
int csv3_issue_request_rtmr(void *req_buffer, size_t buffer_size);

#else

static inline int csv3_issue_request_report(phys_addr_t paddr, size_t size) { return -EIO; }
static inline int csv3_issue_request_rtmr(void *req_buffer, size_t buffer_size) { return -ENODEV; }

#endif	/* CONFIG_HYGON_CSV */

#endif
