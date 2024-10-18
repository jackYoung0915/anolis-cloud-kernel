// SPDX-License-Identifier: GPL-2.0-only
/*
 * Share page table entries when possible to reduce the amount of extra
 * memory consumed by page tables
 *
 * Copyright (C) 2022 Oracle Corp. All rights reserved.
 * Authors:	Khalid Aziz <khalid.aziz@oracle.com>
 *		Matthew Wilcox <willy@infradead.org>
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <asm/pgalloc.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/pgtable_share.h>
#include <linux/hugetlb.h>
#include <linux/mmdebug.h>
#include <uapi/linux/falloc.h>
#include <asm/tlb.h>

static bool vma_is_suitable_pgtable_share(struct vm_area_struct *vma)
{
	if (!vma)
		return false;

	if (!vma->vm_file || !vma->vm_file->f_mapping)
		return false;

	/* hugetlb is not supported temporarily */
	if (is_vm_hugetlb_page(vma))
		return false;

	/* keep in PMD size alignment */
	if (((vma->vm_start | vma->vm_end) & (PMD_SIZE - 1)))
		return false;

	return true;
}

inline struct pgtable_share_struct *vma_get_pgtable_share_data(struct vm_area_struct *vma)
{
	struct pgtable_share_struct *info;

	info = vma->pgtable_share_data;

	return info;
}

inline void vma_set_pgtable_share_data(struct vm_area_struct *vma,
				struct pgtable_share_struct *info)
{
	WRITE_ONCE(vma->pgtable_share_data, info);
}

/*
 * Create a new mm struct that will hold the shared PTEs. Pointer to
 * this new mm is stored in the data structure ptshare_data which also
 * includes a refcount for any current references to PTEs in this new
 * mm. This refcount is used to determine when the mm struct for shared
 * PTEs can be deleted.
 */
static int pgtable_share_new_mm(struct vm_area_struct *vma)
{
	struct mm_struct *new_mm;
	struct pgtable_share_struct *info = NULL;
	int retval = 0;
	unsigned long start = vma->vm_start;
	unsigned long len = vma->vm_end - vma->vm_start;

	new_mm = mm_alloc();
	if (!new_mm) {
		retval = -ENOMEM;
		goto err_free;
	}
	new_mm->mmap_base = start;
	new_mm->task_size = len;
	if (!new_mm->task_size)
		new_mm->task_size--;
	new_mm->owner = NULL;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		retval = -ENOMEM;
		goto err_free;
	}
	info->mm = new_mm;
	refcount_set(&info->refcnt, 1);
	vma_set_pgtable_share_data(vma, info);

	return retval;

err_free:
	if (new_mm)
		mmput(new_mm);
	kfree(info);
	return retval;
}

/*
 * insert vma into mm holding shared page tables
 */
int pgtable_share_insert_vma(struct mm_struct *host_mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *new_vma;
	int err = 0;

	new_vma = vm_area_dup(vma);
	if (!new_vma)
		return -ENOMEM;

	/*
	 * This new vma belongs to host mm, so clear the VM_SHARED_PT
	 * flag on this so we know this is the host vma when we clean
	 * up page tables. Do not use THP for page table shared regions
	 */
	new_vma->vm_flags &= ~VM_SHARED_PT;
	new_vma->vm_flags |= VM_NOHUGEPAGE;
	new_vma->vm_mm = host_mm;
	new_vma->vm_page_prot = vm_get_page_prot(new_vma->vm_flags);

	err = insert_vm_struct(host_mm, new_vma);
	if (err) {
		vm_area_free(new_vma);
		return -ENOMEM;
	}
	get_file(vma->vm_file);

	return err;
}

static pmd_t *pgtable_share_create_pmd(struct mm_struct *mm, unsigned long addr,
				       bool alloc_pte)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		goto out;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		goto out;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		goto out;

	if (!alloc_pte)
		return pmd;

	if (!pmd_none(*pmd) && !pmd_bad(*pmd))
		return pmd;

	if (!pte_alloc(mm, pmd))
		return pmd;
out:
	return NULL;
}

vm_fault_t pgtable_share_copy_pmd(struct vm_area_struct *orig_vma,
				  struct vm_area_struct *shadow_vma,
				  unsigned long addr)
{
	struct mm_struct *orig_mm = orig_vma->vm_mm;
	struct mm_struct *shadow_mm = shadow_vma->vm_mm;
	pmd_t *pmd, *shadow_pmd;
	spinlock_t *ptl;

	pmd = pgtable_share_create_pmd(orig_mm, addr, false);
	if (!pmd)
		goto out;

	shadow_pmd = pgtable_share_create_pmd(shadow_mm, addr, true);
	if (!shadow_pmd)
		goto out;

	ptl = pmd_lock(orig_mm, pmd);
	if (!pmd_none(*pmd)) {
		if (!pmd_same(*pmd, *shadow_pmd)) {
			unsigned long pmd_aligned = (addr & PMD_MASK) >> PAGE_SHIFT;

			/*
			 * It's almost impossible to run here, but for
			 * security, print some warning messages and
			 * set original pmd.
			 */
			pr_warn("the original pmd has different value with shadow pmd");

			pmd_clear(pmd);
			flush_tlb_range(orig_vma, pmd_aligned, pmd_aligned + PMD_SIZE);
			set_pmd_at(orig_mm, addr, pmd, *shadow_pmd);
			spin_unlock(ptl);
			return VM_FAULT_NOPAGE;
		}
	} else {
		pmd_populate(orig_mm, pmd, pmd_pgtable(*shadow_pmd));
		get_page(pmd_page(*shadow_pmd));
		add_mm_counter(orig_mm, MM_SHMEMPAGES, PMD_SIZE >> PAGE_SHIFT);
	}
	spin_unlock(ptl);

	return 0;
out:
	return VM_FAULT_OOM;
}

/*
 * Free the mm struct and page table data, and the shadow vma
 * is also freed in mmput()->exit_mmap()->unmap_vmas().
 */
static inline void free_pgtable_share_mm(struct pgtable_share_struct *info)
{
	mmput(info->mm);
	kfree(info);
}

/* mm_lock (write lock) of vma->vm_mm must be hold in caller */
void pgtable_share_create(struct vm_area_struct *vma)
{
	struct pgtable_share_struct *info;
	int ret;

	if (!vma_is_suitable_pgtable_share(vma))
		return;

	info = vma_get_pgtable_share_data(vma);
	VM_BUG_ON_VMA(info, vma);

	ret = pgtable_share_new_mm(vma);
	if (ret < 0)
		return;

	info = vma->pgtable_share_data;
	/* Duplicate and insert shadow vma into shadow mm */
	ret = pgtable_share_insert_vma(info->mm, vma);
	if (ret < 0) {
		free_pgtable_share_mm(info);
		vma->pgtable_share_data = NULL;
		return;
	}

	vma->vm_flags |= VM_SHARED_PT;
}

/*
 * This function is called when a reference to the shared PTEs in mm
 * struct is dropped. It updates refcount and checks to see if last
 * reference to the mm struct holding shared PTEs has been dropped. If
 * so, it cleans up the mm struct and associated data structures
 */
void pgtable_share_del_mm(struct vm_area_struct *vma)
{
	struct pgtable_share_struct *info;
	struct file *file = vma->vm_file;

	if (!file || (!file->f_mapping))
		return;
	info = vma_get_pgtable_share_data(vma);
	WARN_ON(!info);
	if (!info)
		return;

	vma_set_pgtable_share_data(vma, NULL);
	if (refcount_dec_and_test(&info->refcnt))
		free_pgtable_share_mm(info);
}

/*
 * This function directly references __thp_get_unmapped_area()
 * function.
 */
static unsigned long __pgtable_share_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len,
		loff_t off, unsigned long flags, unsigned long size)
{
	loff_t off_end = off + len;
	loff_t off_align = round_up(off, size);
	unsigned long len_pad, ret;

	if (off_end <= off_align || (off_end - off_align) < size)
		return 0;

	len_pad = len + size;
	if (len_pad < len || (off + len_pad) < off)
		return 0;

	ret = current->mm->get_unmapped_area(filp, addr, len_pad,
					      off >> PAGE_SHIFT, flags);

	/*
	 * The failure might be due to length padding. The caller will retry
	 * without the padding.
	 */
	if (IS_ERR_VALUE(ret))
		return 0;

	/*
	 * Do not try to align to THP boundary if allocation at the address
	 * hint succeeds.
	 */
	if (ret == addr)
		return addr;

	ret += (off - ret) & (size - 1);
	return ret;
}

unsigned long pgtable_share_get_unmapped_area(struct file *filp, unsigned long addr,
					      unsigned long len, unsigned long pgoff,
					      unsigned long flags)
{
	unsigned long ret;
	loff_t off = (loff_t)pgoff << PAGE_SHIFT;

	BUG_ON(!(flags & MAP_SHARED_PT));

	ret = __pgtable_share_get_unmapped_area(filp, addr, len, off, flags, PMD_SIZE);
	return (ret == 0) ? -ENOMEM : ret;
}

void __pgtable_share_clear_pmd(struct mm_struct *mm, pmd_t *pmdp,
			       unsigned long addr)
{
	put_page(pmd_pgtable(*pmdp));
	pmd_clear(pmdp);
	add_mm_counter(mm, MM_SHMEMPAGES, PMD_SIZE >> PAGE_SHIFT);
}

void pgtable_share_clear_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
			     pmd_t *pmdp, unsigned long addr, unsigned long end)
{
	/* pgtable share vmas always align with PMD size */
	spinlock_t *ptl;

	ptl = pmd_lock(vma->vm_mm, pmdp);
	/*
	 * Make sure page fault will happen when accessing
	 * this area.
	 */
	if (!pmd_none_or_clear_bad(pmdp))
		__pgtable_share_clear_pmd(vma->vm_mm, pmdp, addr);
	spin_unlock(ptl);

	tlb_change_page_size(tlb, PAGE_SIZE);
	tlb_flush_pte_range(tlb, addr, end - addr);
	tlb_flush_mmu_tlbonly(tlb);
}

/**
 * pgtable_share_dontneed_single_vma - free page cache and flush
 * TLB for all corresponding tasks.
 * @vma: aboriginal vma which pgtable is sharable.
 */
long pgtable_share_dontneed_single_vma(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end)
{
	struct pgtable_share_struct *info;
	struct address_space *mapping;
	struct file *file;
	int error;
	loff_t offset;

	file = vma->vm_file;
	if ((!file) || (!file->f_mapping))
		return 0;

	/* Check pgtable share data */
	info = vma_get_pgtable_share_data(vma);
	if (!info) {
		pr_warn("the pgtable share data has been released!");
		return -EINVAL;
	}

	/*
	 * Each shared vma has private file mapping, nice
	 * to back early if no pages attached.
	 */
	mapping = file->f_mapping;
	if (unlikely(!mapping->nrpages))
		return 0;

	offset = (loff_t)(start - vma->vm_start)
			+ ((loff_t)vma->vm_pgoff << PAGE_SHIFT);

	/*
	 * zap_page_range_single() will be called during
	 * vfs_fallocate(). It will flush each related tlb.
	 * This refers to MADV_REMOVE.
	 */
	get_file(file);
	mmap_read_unlock(vma->vm_mm);
	error = vfs_fallocate(file, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			      offset, end - start);
	fput(file);
	mmap_read_lock(vma->vm_mm);

	return 0;
}

bool page_is_pgtable_shared(struct page *page)
{
	struct vm_area_struct *vma;
	struct address_space *mapping;
	pgoff_t pgoff;

	page = compound_head(page);
	if (!page_mapped(page))
		return false;

	mapping = page_mapping(page);
	if (!mapping)
		return false;

	pgoff = page_to_index(page);

	i_mmap_lock_read(mapping);
	vma = vma_interval_tree_iter_first(&mapping->i_mmap, pgoff, pgoff);
	while (vma) {
		if (vma_is_pgtable_shared(vma)) {
			i_mmap_unlock_read(mapping);
			return true;
		}

		vma = vma_interval_tree_iter_next(vma, pgoff, pgoff);
	}
	i_mmap_unlock_read(mapping);

	return false;
}

/**
 * pgtable_share_find_intersection - check whether a range area overlap
 * pgtable shared vmas exists.
 */
bool pgtable_share_find_intersection(struct mm_struct *mm, unsigned long start,
				     unsigned long end)
{
	struct vm_area_struct *vma;
	bool ret = false;

	vma = find_vma(mm, start);
	if (!vma)
		return false;

	/* if it doesn't overlap, we have nothing.. */
	if (vma->vm_start >= end)
		return false;

	for ( ; vma && vma->vm_start < end; vma = vma->vm_next) {
		if (vma_is_pgtable_shared(vma)) {
			ret = true;
			break;
		}
	}

	return ret;
}

/*
 * When start,end range within vma which has shared pte. If so, adjust
 * start and end to cover an entire shared pte mapping.
 */
void pgtable_share_adjust_range(struct vm_area_struct *vma, unsigned long *start,
				unsigned long *end)
{
	unsigned long v_start = ALIGN(vma->vm_start, PMD_SIZE),
		v_end = ALIGN_DOWN(vma->vm_end, PMD_SIZE);

	if (!vma_is_pgtable_shared(vma) || !(v_end > v_start) ||
	    (*end <= v_start) || (*start >= v_end))
		return;

	/* Extend the range to be PMD aligned. */
	if (*start > v_start)
		*start = ALIGN_DOWN(*start, PMD_SIZE);

	if (*end < v_end)
		*end = ALIGN(*end, PMD_SIZE);
}

DEFINE_STATIC_KEY_FALSE(pgtable_share_enabled_key);
static int __init setup_pgtable_share(char *s)
{
	if (!strcmp(s, "1"))
		static_branch_enable(&pgtable_share_enabled_key);
	else if (!strcmp(s, "0"))
		static_branch_disable(&pgtable_share_enabled_key);
	return 1;
}
__setup("pgtable_share=", setup_pgtable_share);

#ifdef CONFIG_SYSFS
static ssize_t pgtable_share_enabled_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", !!static_branch_unlikely(&pgtable_share_enabled_key));
}
static ssize_t pgtable_share_enabled_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	static DEFINE_MUTEX(mutex);
	ssize_t ret = count;

	mutex_lock(&mutex);

	if (!strncmp(buf, "1", 1))
		static_branch_enable(&pgtable_share_enabled_key);
	else if (!strncmp(buf, "0", 1))
		static_branch_disable(&pgtable_share_enabled_key);
	else
		ret = -EINVAL;

	mutex_unlock(&mutex);
	return ret;
}
static struct kobj_attribute pgtable_share_enabled_attr =
	__ATTR(enabled, 0644, pgtable_share_enabled_show,
	       pgtable_share_enabled_store);

static struct attribute *pgtable_share_attrs[] = {
	&pgtable_share_enabled_attr.attr,
	NULL,
};

static struct attribute_group pgtable_share_attr_group = {
	.attrs = pgtable_share_attrs,
};

static int __init pgtable_share_init_sysfs(void)
{
	int err;
	struct kobject *pgtable_share_kobj;

	pgtable_share_kobj = kobject_create_and_add("pgtable_share", mm_kobj);
	if (!pgtable_share_kobj) {
		pr_err("failed to create pgtable_share kobject\n");
		return -ENOMEM;
	}
	err = sysfs_create_group(pgtable_share_kobj, &pgtable_share_attr_group);
	if (err) {
		pr_err("failed to register pgtable_share group\n");
		goto delete_obj;
	}

	return 0;

delete_obj:
	kobject_put(pgtable_share_kobj);
	return err;
}
#endif /* CONFIG_SYSFS */

static int __init pgtable_share_init(void)
{
	int ret = -EINVAL;

#ifdef CONFIG_SYSFS
	ret = pgtable_share_init_sysfs();
#endif

	return ret;
}
module_init(pgtable_share_init);
