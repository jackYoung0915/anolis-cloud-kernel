#ifndef _LINUX_PGTABLE_SHARE_H_
#define _LINUX_PGTABLE_SHARE_H_

struct vm_area_struct;
struct mm_struct;

#ifdef CONFIG_PAGETABLE_SHARE
struct pgtable_share_struct {
	struct mm_struct *mm;
	refcount_t refcnt;
};

extern void pgtable_share_del_mm(struct vm_area_struct *vm);
extern int pgtable_share_insert_vma(struct mm_struct *mm, struct vm_area_struct *vma);
extern void pgtable_share_create(struct vm_area_struct *vma);
extern struct pgtable_share_struct *vma_get_pgtable_share_data(struct vm_area_struct *vma);
extern void vma_set_pgtable_share_mm(struct vm_area_struct *vma,
				     struct pgtable_share_struct *info);
extern unsigned long pgtable_share_get_unmapped_area(struct file *filp,
						     unsigned long addr,
						     unsigned long len,
						     unsigned long pgoff,
						     unsigned long flags);
extern vm_fault_t pgtable_share_copy_pmd(struct vm_area_struct *orig_vma,
				  struct vm_area_struct *shadow_vma,
				  unsigned long addr);

extern void __pgtable_share_clear_pmd(struct mm_struct *mm, pmd_t *pmdp,
				      unsigned long addr);
extern void pgtable_share_clear_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
				    pmd_t *pmdp, unsigned long addr, unsigned long end);
extern long pgtable_share_dontneed_single_vma(struct vm_area_struct *vma,
					      unsigned long start, unsigned long end);
extern bool page_is_pgtable_shared(struct page *page);
extern bool pgtable_share_find_intersection(struct mm_struct *mm, unsigned long start,
					    unsigned long end);
extern void pgtable_share_adjust_range(struct vm_area_struct *vma,
				       unsigned long *start, unsigned long *end);

static inline bool vma_is_pgtable_shared(const struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_SHARED_PT;
}

static inline bool vma_is_pgtable_shadow(const struct vm_area_struct *vma)
{
	return vma && vma->pgtable_share_data &&
		vma->vm_mm == vma->pgtable_share_data->mm;
}

DECLARE_STATIC_KEY_FALSE(pgtable_share_enabled_key);
static inline bool pgtable_share_enable(void)
{
	return static_branch_unlikely(&pgtable_share_enabled_key);
}
#else
static inline bool vma_is_pgtable_shared(const struct vm_area_struct *vma)
{
	return false;
}

static inline struct pgtable_share_struct *vma_get_pgtable_share_data(struct vm_area_struct *vma)
{
	return NULL;
}

static inline void vma_set_pgtable_share_data(struct vm_area_struct *vma,
					      struct pgtable_share_struct *info)
{
}

static inline void pgtable_share_del_mm(struct vm_area_struct *vma)
{
}

static inline int pgtable_share_insert_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	return 0;
}

static inline void pgtable_share_create(struct vm_area_struct *vma)
{
}

static inline vm_fault_t pgtable_share_copy_pmd(struct vm_area_struct *orig_vma,
				  struct vm_area_struct *shadow_vma,
				  unsigned long addr)
{
	return 0;
}

static inline void __pgtable_share_clear_pmd(struct mm_struct *mm, pmd_t *pmdp,
					     unsigned long addr)
{
}

static inline void pgtable_share_clear_pmd(struct mmu_gather *tlb,
					   struct vm_area_struct *vma,
					   pmd_t *pmdp, unsigned long addr,
					   unsigned long end)
{
}

static inline long pgtable_share_dontneed_single_vma(struct vm_area_struct *vma,
						     unsigned long start, unsigned long end)
{
	return 0;
}

static inline bool page_is_pgtable_shared(struct page *page)
{
	return false;
}

static inline bool pgtable_share_find_intersection(struct mm_struct *mm, unsigned long start,
						    unsigned long end)
{
	return false;
}

static inline void pgtable_share_adjust_range(struct vm_area_struct *vma,
					      unsigned long *start, unsigned long *end)
{
}

static inline bool pgtable_share_enable(void)
{
	return false;
}
#endif
#endif
