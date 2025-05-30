// SPDX-License-Identifier: GPL-2.0
#include <linux/hugetlb.h>
#ifdef CONFIG_X86_64
#include <asm-generic/tlb.h>
#endif
#ifdef CONFIG_ARM64
#include <asm/tlb.h>
#endif
#include <asm/pgalloc.h>
#include <linux/pgtable.h>

#include "internal.h"

bool reclaim_pt_is_enabled(unsigned long start, unsigned long end,
			   struct zap_details *details)
{
	return details && details->reclaim_pt && (end - start >= PMD_SIZE);
}

bool try_get_and_clear_pmd(struct mm_struct *mm, pmd_t *pmd, pmd_t *pmdval)
{
	spinlock_t *pml = pmd_lockptr(mm, pmd);

	if (!spin_trylock(pml))
		return false;

	*pmdval = pmd_read_atomic(pmd);
	pmd_clear(pmd);
	spin_unlock(pml);

	return true;
}

void free_pte(struct mm_struct *mm, unsigned long addr, struct mmu_gather *tlb,
	      pmd_t pmdval)
{
	pte_free_tlb(tlb, pmd_pgtable(pmdval), addr);
	mm_dec_nr_ptes(mm);
}

void try_to_free_pte(struct mm_struct *mm, pmd_t *pmd, unsigned long addr,
		     struct mmu_gather *tlb)
{
	pmd_t pmdval;
	spinlock_t *pml, *ptl = NULL;
	pte_t *start_pte, *pte;
	int i;

	pml = pmd_lock(mm, pmd);
	start_pte = pte_offset_map(pmd, addr);
	if (!start_pte)
		goto out_ptl;

	ptl = pte_lockptr(mm, pmd);

	if (ptl != pml)
		spin_lock_nested(ptl, SINGLE_DEPTH_NESTING);

	pmdval = pmd_read_atomic(pmd);

	/* Check if it is empty PTE page */
	for (i = 0, pte = start_pte; i < PTRS_PER_PTE; i++, pte++) {
		if (!pte_none(ptep_get(pte)))
			goto out_ptl;
	}
	pte_unmap(start_pte);

	pmd_clear(pmd);

	if (ptl != pml)
		spin_unlock(ptl);
	spin_unlock(pml);

	free_pte(mm, addr, tlb, pmdval);

	return;
out_ptl:
	if (start_pte)
		pte_unmap_unlock(start_pte, ptl);
	if (ptl != pml)
		spin_unlock(pml);
}
