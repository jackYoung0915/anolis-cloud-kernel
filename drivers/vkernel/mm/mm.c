// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include "mm.h"

int vk_init_memory_pref(struct vkernel_mem_pref *mem)
{
	mem->default_policy.refcnt =  (atomic_t)ATOMIC_INIT(1);
	mem->default_policy.mode = MPOL_LOCAL;

	mem->shmem_huge = SHMEM_HUGE_NEVER;
	mem->thp_flags =
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS
		(1<<TRANSPARENT_HUGEPAGE_FLAG)|
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_MADVISE
		(1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)|
#endif
		(1<<TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG)|
		(1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG)|
		(1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);

	return 0;
}

void vk_uninit_memory_pref(struct vkernel_mem_pref *mem)
{

}

int vkernel_set_memory_pref(struct vkernel_mem_pref *mem, struct vkernel_mem_desc *desc)
{
	unsigned long *flags = &mem->thp_flags;

	if (desc->numa_mode >= 0 && desc->numa_mode < MPOL_MAX) {
		/* TODO: Setup all fields */
		// mem->default_policy.mode = desc->numa_mode;
		pr_info("set default numa policy is not supported yet\n");
	}

	if (desc->shmem_enabled >= SHMEM_HUGE_FORCE &&
	    desc->shmem_enabled <= SHMEM_HUGE_ADVISE)
		mem->shmem_huge = desc->shmem_enabled;

	if (desc->thp_enabled > -1 &&
	    desc->thp_enabled < TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG, flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, flags);
	if (desc->thp_enabled > TRANSPARENT_HUGEPAGE_UNSUPPORTED)
		set_bit(desc->thp_enabled, flags);
	}

	if (desc->thp_defrag > -1 &&
	    desc->thp_defrag < TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG) {
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_OR_MADV_FLAG, flags);
		clear_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, flags);
		if (desc->thp_defrag > TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)
			set_bit(desc->thp_defrag, flags);
	}

	if (desc->thp_use_zero_page == 0)
		clear_bit(TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG, flags);
	else if (desc->thp_use_zero_page == 1)
		set_bit(TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG, flags);

	return 0;
}
