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

#include <linux/numa.h>

#include "csv_command.h"

enum csv_smr_source {
	USE_CMA,
	USE_HUGETLB,
	NOT_SUPPORTED,
};

#ifdef CONFIG_HYGON_CSV

#define CSV_MR_ALIGN_BITS		(28)

struct csv_mem {
	uint64_t start;
	uint64_t size;
	int nid;
};

extern struct csv_mem *csv_smr;
extern unsigned int csv_smr_num;
extern struct csv_mem *csv_smcr;
extern unsigned int csv_smcr_num;

#ifdef CONFIG_SYSFS
extern atomic_long_t csv3_npt_size;
extern atomic_long_t csv3_pri_mem;
extern unsigned long csv3_meta;
extern atomic_long_t csv3_shared_mem[MAX_NUMNODES];
#endif	/* CONFIG_SYSFS */

void __init early_csv_reserve_mem(void);
phys_addr_t csv_alloc_from_contiguous(size_t size, nodemask_t *nodes_allowed,
				unsigned int align);
void csv_release_to_contiguous(phys_addr_t pa, size_t size);

phys_addr_t csv_alloc_metadata(void);
void csv_free_metadata(u64 hpa);

enum csv_smr_source get_csv_smr_source(void);

uint32_t csv_get_smr_entry_shift(void);

int csv3_issue_request_report(phys_addr_t paddr, size_t size);
int csv3_issue_request_rtmr(void *req_buffer, size_t buffer_size);

#else	/* !CONFIG_HYGON_CSV */

#define csv_smr		NULL
#define csv_smr_num	0U
#define csv_smcr	NULL
#define csv_smcr_num	0U

static inline void __init early_csv_reserve_mem(void) { }

static inline phys_addr_t
csv_alloc_from_contiguous(size_t size, nodemask_t *nodes_allowed,
			  unsigned int align) { return 0; }
static inline void csv_release_to_contiguous(phys_addr_t pa, size_t size) { }

static inline phys_addr_t csv_alloc_metadata(void) { return 0; }
static inline void csv_free_metadata(u64 hpa) { }

static inline enum csv_smr_source get_csv_smr_source(void) { return NOT_SUPPORT; }

static inline uint32_t csv_get_smr_entry_shift(void) { return 0; }

static inline int csv3_issue_request_report(phys_addr_t paddr, size_t size) { return -EIO; }
static inline int csv3_issue_request_rtmr(void *req_buffer, size_t buffer_size) { return -ENODEV; }

#endif	/* CONFIG_HYGON_CSV */

#endif
