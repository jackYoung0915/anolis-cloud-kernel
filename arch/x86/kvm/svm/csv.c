// SPDX-License-Identifier: GPL-2.0-only
/*
 * CSV driver for KVM
 *
 * HYGON CSV support
 *
 * Copyright (C) Hygon Info Technologies Ltd.
 */

#include <linux/kvm_host.h>
#include <linux/psp-sev.h>
#include <linux/psp-csv.h>
#include <linux/memory.h>
#include <linux/kvm_types.h>
#include <linux/vmalloc.h>
#include <linux/rbtree.h>
#include <asm/cacheflush.h>
#include <asm/e820/api.h>
#include <asm/csv.h>
#include "kvm_cache_regs.h"
#include "svm.h"
#include "csv.h"
#include "x86.h"

#undef  pr_fmt
#define pr_fmt(fmt) "CSV: " fmt

struct encrypt_data_block {
	struct {
		u64 npages:	12;
		u64 pfn:	52;
	} entry[512];
};

union csv_page_attr {
	struct {
		u64 reserved:	1;
		u64 rw:		1;
		u64 reserved1:	49;
		u64 mmio:	1;
		u64 reserved2:	12;
	};
	u64 val;
};

struct guest_paddr_block {
	struct {
		u64 share:	1;
		u64 reserved:	11;
		u64 gfn:	52;
	} entry[512];
};

struct trans_paddr_block {
	u64	trans_paddr[512];
};

struct vmcb_paddr_block {
	u64	vmcb_paddr[512];
};

enum csv_pg_level {
	CSV_PG_LEVEL_NONE,
	CSV_PG_LEVEL_4K,
	CSV_PG_LEVEL_2M,
	CSV_PG_LEVEL_NUM
};

/*
 * Manage shared page in rbtree, the node within the rbtree
 * is indexed by gfn. @page points to the page mapped by @gfn
 * in NPT.
 */
struct shared_page {
	struct rb_node node;
	gfn_t gfn;
	struct page *page;
};

struct shared_page_mgr {
	struct rb_root root;
	u64 count;
};

struct kvm_csv_info {
	struct kvm_sev_info *sev;

	bool csv_active;	/* CSV enabled guest */

	struct kmem_cache *sp_slab;	/* shared page slab */
	struct shared_page_mgr sp_mgr;	/* shared page manager */
	struct mutex sp_lock;		/* shared page lock */

	struct list_head smr_list; /* List of guest secure memory regions */
	unsigned long nodemask; /* Nodemask where CSV guest's memory resides */
};

struct kvm_svm_csv {
	struct kvm_svm kvm_svm;
	struct kvm_csv_info csv_info;
};

struct secure_memory_region {
	struct list_head list;
	u64 npages;
	u64 hpa;
};

static struct kvm_x86_ops csv_x86_ops;

static bool shared_page_insert(struct shared_page_mgr *mgr, struct shared_page *sp)
{
	struct shared_page *sp_iter;
	struct rb_root *root;
	struct rb_node **new;
	struct rb_node *parent = NULL;

	root = &mgr->root;
	new = &(root->rb_node);

	/* Figure out where to put new node */
	while (*new) {
		sp_iter = rb_entry(*new, struct shared_page, node);
		parent = *new;

		if (sp->gfn < sp_iter->gfn)
			new = &((*new)->rb_left);
		else if (sp->gfn > sp_iter->gfn)
			new = &((*new)->rb_right);
		else
			return false;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&sp->node, parent, new);
	rb_insert_color(&sp->node, root);
	mgr->count++;

	return true;
}

static struct shared_page *shared_page_search(struct shared_page_mgr *mgr, gfn_t gfn)
{
	struct shared_page *sp;
	struct rb_root *root;
	struct rb_node *node;

	root = &mgr->root;
	node = root->rb_node;
	while (node) {
		sp = rb_entry(node, struct shared_page, node);
		if (gfn < sp->gfn)
			node = node->rb_left;
		else if (gfn > sp->gfn)
			node = node->rb_right;
		else
			return sp;
	}

	return NULL;

}

static struct shared_page *shared_page_remove(struct shared_page_mgr *mgr, gfn_t gfn)
{
	struct shared_page *sp;

	sp = shared_page_search(mgr, gfn);
	if (sp) {
		rb_erase(&sp->node, &mgr->root);
		mgr->count--;
	}

	return sp;
}

static inline struct kvm_svm_csv *to_kvm_svm_csv(struct kvm *kvm)
{
	return (struct kvm_svm_csv *)container_of(kvm, struct kvm_svm, kvm);
}

static int to_csv_pg_level(int level)
{
	int ret;

	switch (level) {
	case PG_LEVEL_4K:
		ret = CSV_PG_LEVEL_4K;
		break;
	case PG_LEVEL_2M:
		ret = CSV_PG_LEVEL_2M;
		break;
	default:
		ret = CSV_PG_LEVEL_NONE;
	}

	return ret;
}

static bool csv_guest(struct kvm *kvm)
{
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;

	return sev_es_guest(kvm) && csv->csv_active;
}

static int csv_sync_vmsa(struct vcpu_svm *svm)
{
	struct vmcb_save_area *save = &svm->vmcb->save;

	/* Check some debug related fields before encrypting the VMSA */
	if (svm->vcpu.guest_debug || (save->dr7 & ~DR7_FIXED_1))
		return -EINVAL;

	/* Sync registgers per spec. */
	save->rax = svm->vcpu.arch.regs[VCPU_REGS_RAX];
	save->rdx = svm->vcpu.arch.regs[VCPU_REGS_RDX];
	save->rip = svm->vcpu.arch.regs[VCPU_REGS_RIP];
	save->xcr0 = svm->vcpu.arch.xcr0;
	save->xss  = svm->vcpu.arch.ia32_xss;

	memcpy(svm->vmsa, save, sizeof(*save));
	return 0;
}

static int __csv_issue_cmd(int fd, int id, void *data, int *error)
{
	struct fd f;
	int ret;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	ret = sev_issue_cmd_external_user(f.file, id, data, error);

	fdput(f);
	return ret;
}

static int csv_issue_cmd(struct kvm *kvm, int id, void *data, int *error)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	return __csv_issue_cmd(sev->fd, id, data, error);
}

static inline void csv_init_update_npt(struct csv_data_update_npt *update_npt,
				       gpa_t gpa, u32 error, u32 handle)
{
	memset(update_npt, 0x00, sizeof(*update_npt));

	update_npt->gpa = gpa & PAGE_MASK;
	update_npt->error_code = error;
	update_npt->handle = handle;
}

static int csv_guest_init(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct kvm_csv_init_data params;
	struct kmem_cache *sp_slab;
	char   slab_name[0x40];

	if (unlikely(csv->csv_active))
		return -EINVAL;

	if (unlikely(!sev->es_active))
		return -EINVAL;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	csv->csv_active = true;
	csv->sev = sev;
	csv->nodemask = (unsigned long)params.nodemask;

	INIT_LIST_HEAD(&csv->smr_list);
	mutex_init(&csv->sp_lock);

	memset(slab_name, 0, sizeof(slab_name));
	snprintf(slab_name, sizeof(slab_name), "csv3_%d_sp_slab", sev->asid);
	sp_slab = kmem_cache_create(slab_name, sizeof(struct shared_page), 0, 0, NULL);
	if (!sp_slab)
		return -ENOMEM;

	csv->sp_slab = sp_slab;
	csv->sp_mgr.root = RB_ROOT;

	return 0;
}

static bool csv_is_mmio_pfn(kvm_pfn_t pfn)
{
	return !e820__mapped_raw_any(pfn_to_hpa(pfn),
				     pfn_to_hpa(pfn + 1) - 1,
				     E820_TYPE_RAM);
}

static int csv_set_guest_private_memory(struct kvm *kvm)
{
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *memslot;
	struct secure_memory_region *smr;
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct csv_data_set_guest_private_memory *set_guest_private_memory;
	struct csv_data_memory_region *regions;
	nodemask_t nodemask;
	nodemask_t *nodemask_ptr;

	LIST_HEAD(tmp_list);
	struct list_head *pos, *q;
	u32 i = 0, count = 0, remainder;
	int ret = 0, error;
	u64 size = 0, nr_smr = 0, nr_pages = 0;
	u32 smr_entry_shift;

	unsigned int flags = FOLL_HWPOISON;
	int npages;
	struct page *page;

	if (!csv_guest(kvm))
		return -ENOTTY;

	nodes_clear(nodemask);
	for_each_set_bit(i, &csv->nodemask, BITS_PER_LONG)
		if (i < MAX_NUMNODES)
			node_set(i, nodemask);

	nodemask_ptr = csv->nodemask ? &nodemask : &node_online_map;

	set_guest_private_memory = kzalloc(sizeof(*set_guest_private_memory),
					GFP_KERNEL_ACCOUNT);
	if (!set_guest_private_memory)
		return -ENOMEM;

	regions = kzalloc(PAGE_SIZE, GFP_KERNEL_ACCOUNT);
	if (!regions) {
		kfree(set_guest_private_memory);
		return -ENOMEM;
	}

	/* Get guest secure memory size */
	kvm_for_each_memslot(memslot, slots) {
		npages = get_user_pages_unlocked(memslot->userspace_addr, 1,
						&page, flags);
		if (npages != 1)
			continue;

		nr_pages += memslot->npages;

		put_page(page);
	}

	/*
	 * NPT secure memory size
	 *
	 * PTEs_entries = nr_pages
	 * PDEs_entries = nr_pages / 512
	 * PDPEs_entries = nr_pages / (512 * 512)
	 * PML4Es_entries = nr_pages / (512 * 512 * 512)
	 *
	 * Totals_entries = nr_pages + nr_pages / 512 + nr_pages / (512 * 512) +
	 *		nr_pages / (512 * 512 * 512) <= nr_pages + nr_pages / 256
	 *
	 * Total_NPT_size = (Totals_entries / 512) * PAGE_SIZE = ((nr_pages +
	 *      nr_pages / 256) / 512) * PAGE_SIZE = nr_pages * 8 + nr_pages / 32
	 *      <= nr_pages * 9
	 *
	 */
	smr_entry_shift = csv_get_smr_entry_shift();
	size = ALIGN((nr_pages << PAGE_SHIFT), 1UL << smr_entry_shift) +
		ALIGN(nr_pages * 9, 1UL << smr_entry_shift);
	nr_smr = size >> smr_entry_shift;
	remainder = nr_smr;
	for (i = 0; i < nr_smr; i++) {
		smr = kzalloc(sizeof(*smr), GFP_KERNEL_ACCOUNT);
		if (!smr) {
			ret = -ENOMEM;
			goto e_free_smr;
		}

		smr->hpa = csv_alloc_from_contiguous((1UL << smr_entry_shift),
						nodemask_ptr,
						get_order(1 << smr_entry_shift));
		if (!smr->hpa) {
			kfree(smr);
			ret = -ENOMEM;
			goto e_free_smr;
		}

		smr->npages = ((1UL << smr_entry_shift) >> PAGE_SHIFT);
		list_add_tail(&smr->list, &tmp_list);

		regions[count].size = (1UL << smr_entry_shift);
		regions[count].base_address = smr->hpa;
		count++;

		if (count >= (PAGE_SIZE / sizeof(regions[0])) || (remainder == count)) {
			set_guest_private_memory->nregions = count;
			set_guest_private_memory->handle = sev->handle;
			set_guest_private_memory->regions_paddr = __sme_pa(regions);

			/* set secury memory region for launch enrypt data */
			ret = csv_issue_cmd(kvm, CSV_CMD_SET_GUEST_PRIVATE_MEMORY,
					set_guest_private_memory, &error);
			if (ret)
				goto e_free_smr;

			memset(regions, 0, PAGE_SIZE);
			remainder -= count;
			count = 0;
		}
	}

	list_splice(&tmp_list, &csv->smr_list);

	goto done;

e_free_smr:
	if (!list_empty(&tmp_list)) {
		list_for_each_safe(pos, q, &tmp_list) {
			smr = list_entry(pos, struct secure_memory_region, list);
			if (smr) {
				csv_release_to_contiguous(smr->hpa,
							smr->npages << PAGE_SHIFT);
				list_del(&smr->list);
				kfree(smr);
			}
		}
	}
done:
	kfree(set_guest_private_memory);
	kfree(regions);
	return ret;
}

static int csv_launch_encrypt_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct kvm_csv_launch_encrypt_data params;
	struct csv_data_launch_encrypt_data *encrypt_data = NULL;
	struct encrypt_data_block *blocks = NULL;
	u8 *data = NULL;
	u32 offset;
	u32 num_entries, num_entries_in_block;
	u32 num_blocks, num_blocks_max;
	u32 i, n;
	unsigned long pfn, pfn_sme_mask;
	int ret = 0;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params))) {
		ret = -EFAULT;
		goto exit;
	}

	if ((params.len & ~PAGE_MASK) || !params.len || !params.uaddr) {
		ret = -EINVAL;
		goto exit;
	}

	/* Allocate all the guest memory from CMA */
	ret = csv_set_guest_private_memory(kvm);
	if (ret)
		goto exit;

	num_entries = params.len / PAGE_SIZE;
	num_entries_in_block = ARRAY_SIZE(blocks->entry);
	num_blocks = (num_entries + num_entries_in_block - 1) / num_entries_in_block;
	num_blocks_max = ARRAY_SIZE(encrypt_data->data_blocks);

	if (num_blocks >= num_blocks_max) {
		ret = -EINVAL;
		goto exit;
	}

	data = vzalloc(params.len);
	if (!data) {
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(data, (void __user *)params.uaddr, params.len)) {
		ret = -EFAULT;
		goto data_free;
	}

	blocks = vzalloc(num_blocks * sizeof(*blocks));
	if (!blocks) {
		ret = -ENOMEM;
		goto data_free;
	}

	for (offset = 0, i = 0, n = 0; offset < params.len; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(offset + data);
		pfn_sme_mask = __sme_set(pfn << PAGE_SHIFT) >> PAGE_SHIFT;
		if (offset && ((blocks[n].entry[i].pfn + 1) == pfn_sme_mask))
			blocks[n].entry[i].npages += 1;
		else {
			if (offset) {
				i = (i + 1) % num_entries_in_block;
				n = (i == 0) ? (n + 1) : n;
			}
			blocks[n].entry[i].pfn = pfn_sme_mask;
			blocks[n].entry[i].npages = 1;
		}
	}

	encrypt_data = kzalloc(sizeof(*encrypt_data), GFP_KERNEL);
	if (!encrypt_data) {
		ret = -ENOMEM;
		goto block_free;
	}

	encrypt_data->handle = csv->sev->handle;
	encrypt_data->length = params.len;
	encrypt_data->gpa = params.gpa;
	for (i = 0; i <= n; i++) {
		encrypt_data->data_blocks[i] =
		__sme_set(vmalloc_to_pfn((void *)blocks + i * sizeof(*blocks)) << PAGE_SHIFT);
	}

	clflush_cache_range(data, params.len);
	ret = csv_issue_cmd(kvm, CSV_CMD_LAUNCH_ENCRYPT_DATA,
			    encrypt_data, &argp->error);

	kfree(encrypt_data);
block_free:
	vfree(blocks);
data_free:
	vfree(data);
exit:
	return ret;
}

static int csv_launch_encrypt_vmcb(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct csv_data_launch_encrypt_vmcb *encrypt_vmcb = NULL;
	struct kvm_vcpu *vcpu;
	int ret = 0;
	unsigned long i = 0;

	if (!csv_guest(kvm))
		return -ENOTTY;

	encrypt_vmcb = kzalloc(sizeof(*encrypt_vmcb), GFP_KERNEL);
	if (!encrypt_vmcb) {
		ret = -ENOMEM;
		goto exit;
	}

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vcpu_svm *svm = to_svm(vcpu);

		ret = csv_sync_vmsa(svm);
		if (ret)
			goto e_free;
		clflush_cache_range(svm->vmsa, PAGE_SIZE);
		clflush_cache_range(svm->vmcb, PAGE_SIZE);
		encrypt_vmcb->handle = csv->sev->handle;
		encrypt_vmcb->vcpu_id = i;
		encrypt_vmcb->vmsa_addr = __sme_pa(svm->vmsa);
		encrypt_vmcb->vmsa_len = PAGE_SIZE;
		encrypt_vmcb->shadow_vmcb_addr = __sme_pa(svm->vmcb);
		encrypt_vmcb->shadow_vmcb_len = PAGE_SIZE;
		ret = csv_issue_cmd(kvm, CSV_CMD_LAUNCH_ENCRYPT_VMCB,
				    encrypt_vmcb, &argp->error);
		if (ret)
			goto e_free;

		svm->vmcb_pa = encrypt_vmcb->secure_vmcb_addr;
		svm->vcpu.arch.guest_state_protected = true;
	}

e_free:
	kfree(encrypt_vmcb);
exit:
	return ret;
}

/* Userspace wants to query either header or trans length. */
static int
csv_send_encrypt_data_query_lengths(struct kvm *kvm, struct kvm_sev_cmd *argp,
				    struct kvm_csv_send_encrypt_data *params)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct csv_data_send_encrypt_data data;
	int ret;

	memset(&data, 0, sizeof(data));
	data.handle = sev->handle;
	ret = csv_issue_cmd(kvm, CSV_CMD_SEND_ENCRYPT_DATA, &data, &argp->error);

	params->hdr_len = data.hdr_len;
	params->trans_len = data.trans_len;

	if (copy_to_user((void __user *)(uintptr_t)argp->data, params, sizeof(*params)))
		ret = -EFAULT;

	return ret;
}

#define CSV_SEND_ENCRYPT_DATA_MIGRATE_PAGE  0x00000000
#define CSV_SEND_ENCRYPT_DATA_SET_READONLY  0x00000001
static int csv_send_encrypt_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct csv_data_send_encrypt_data data;
	struct kvm_csv_send_encrypt_data params;
	void *hdr;
	void *trans_data;
	struct trans_paddr_block *trans_block;
	struct guest_paddr_block *guest_block;
	unsigned long pfn;
	u32 offset;
	int ret = 0;
	int i;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	/* userspace wants to query either header or trans length */
	if (!params.trans_len || !params.hdr_len)
		return csv_send_encrypt_data_query_lengths(kvm, argp, &params);

	if (!params.trans_uaddr || !params.guest_addr_data ||
	    !params.guest_addr_len || !params.hdr_uaddr)
		return -EINVAL;

	if (params.guest_addr_len > sizeof(*guest_block))
		return -EINVAL;

	if (params.trans_len > ARRAY_SIZE(trans_block->trans_paddr) * PAGE_SIZE)
		return -EINVAL;

	if ((params.trans_len & PAGE_MASK) == 0 ||
	    (params.trans_len & ~PAGE_MASK) != 0)
		return -EINVAL;

	/* allocate memory for header and transport buffer */
	hdr = kzalloc(params.hdr_len, GFP_KERNEL_ACCOUNT);
	if (!hdr) {
		ret = -ENOMEM;
		goto exit;
	}

	guest_block = kzalloc(sizeof(*guest_block), GFP_KERNEL_ACCOUNT);
	if (!guest_block) {
		ret = -ENOMEM;
		goto e_free_hdr;
	}

	if (copy_from_user(guest_block,
			   (void __user *)(uintptr_t)params.guest_addr_data,
			   params.guest_addr_len)) {
		ret = -EFAULT;
		goto e_free_guest_block;
	}

	trans_block = kzalloc(sizeof(*trans_block), GFP_KERNEL_ACCOUNT);
	if (!trans_block) {
		ret = -ENOMEM;
		goto e_free_guest_block;
	}
	trans_data = vzalloc(params.trans_len);
	if (!trans_data) {
		ret = -ENOMEM;
		goto e_free_trans_block;
	}

	for (offset = 0, i = 0; offset < params.trans_len; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(offset + trans_data);
		trans_block->trans_paddr[i] = __sme_set(pfn_to_hpa(pfn));
		i++;
	}
	memset(&data, 0, sizeof(data));
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_block = __psp_pa(trans_block);
	data.trans_len = params.trans_len;

	data.guest_block = __psp_pa(guest_block);
	data.guest_len = params.guest_addr_len;
	data.handle = sev->handle;

	clflush_cache_range(hdr, params.hdr_len);
	clflush_cache_range(trans_data, params.trans_len);
	clflush_cache_range(trans_block, PAGE_SIZE);
	clflush_cache_range(guest_block, PAGE_SIZE);

	data.flag = CSV_SEND_ENCRYPT_DATA_SET_READONLY;
	ret = csv_issue_cmd(kvm, CSV_CMD_SEND_ENCRYPT_DATA, &data, &argp->error);
	if (ret)
		goto e_free_trans_data;

	kvm_flush_remote_tlbs(kvm);

	data.flag = CSV_SEND_ENCRYPT_DATA_MIGRATE_PAGE;
	ret = csv_issue_cmd(kvm, CSV_CMD_SEND_ENCRYPT_DATA, &data, &argp->error);
	if (ret)
		goto e_free_trans_data;

	ret = -EFAULT;
	/* copy transport buffer to user space */
	if (copy_to_user((void __user *)(uintptr_t)params.trans_uaddr,
			 trans_data, params.trans_len))
		goto e_free_trans_data;

	/* copy guest address block to user space */
	if (copy_to_user((void __user *)(uintptr_t)params.guest_addr_data,
			 guest_block, params.guest_addr_len))
		goto e_free_trans_data;

	/* copy packet header to userspace. */
	if (copy_to_user((void __user *)(uintptr_t)params.hdr_uaddr, hdr,
			 params.hdr_len))
		goto e_free_trans_data;

	ret = 0;
e_free_trans_data:
	vfree(trans_data);
e_free_trans_block:
	kfree(trans_block);
e_free_guest_block:
	kfree(guest_block);
e_free_hdr:
	kfree(hdr);
exit:
	return ret;
}

/* Userspace wants to query either header or trans length. */
static int
csv_send_encrypt_context_query_lengths(struct kvm *kvm, struct kvm_sev_cmd *argp,
				      struct kvm_csv_send_encrypt_context *params)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct csv_data_send_encrypt_context data;
	int ret;

	memset(&data, 0, sizeof(data));
	data.handle = sev->handle;
	ret = csv_issue_cmd(kvm, CSV_CMD_SEND_ENCRYPT_CONTEXT, &data, &argp->error);

	params->hdr_len = data.hdr_len;
	params->trans_len = data.trans_len;

	if (copy_to_user((void __user *)(uintptr_t)argp->data, params, sizeof(*params)))
		ret = -EFAULT;

	return ret;
}

static int csv_send_encrypt_context(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct csv_data_send_encrypt_context data;
	struct kvm_csv_send_encrypt_context params;
	void *hdr;
	void *trans_data;
	struct trans_paddr_block *trans_block;
	unsigned long pfn;
	unsigned long i;
	u32 offset;
	int ret = 0;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	/* userspace wants to query either header or trans length */
	if (!params.trans_len || !params.hdr_len)
		return csv_send_encrypt_context_query_lengths(kvm, argp, &params);

	if (!params.trans_uaddr || !params.hdr_uaddr)
		return -EINVAL;

	if (params.trans_len > ARRAY_SIZE(trans_block->trans_paddr) * PAGE_SIZE)
		return -EINVAL;

	/* allocate memory for header and transport buffer */
	hdr = kzalloc(params.hdr_len, GFP_KERNEL_ACCOUNT);
	if (!hdr) {
		ret = -ENOMEM;
		goto exit;
	}

	trans_block = kzalloc(sizeof(*trans_block), GFP_KERNEL_ACCOUNT);
	if (!trans_block) {
		ret = -ENOMEM;
		goto e_free_hdr;
	}
	trans_data = vzalloc(params.trans_len);
	if (!trans_data) {
		ret = -ENOMEM;
		goto e_free_trans_block;
	}

	for (offset = 0, i = 0; offset < params.trans_len; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(offset + trans_data);
		trans_block->trans_paddr[i] = __sme_set(pfn_to_hpa(pfn));
		i++;
	}

	memset(&data, 0, sizeof(data));
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_block = __psp_pa(trans_block);
	data.trans_len = params.trans_len;
	data.handle = sev->handle;

	/* flush hdr, trans data, trans block, secure VMSAs */
	wbinvd_on_all_cpus();

	ret = csv_issue_cmd(kvm, CSV_CMD_SEND_ENCRYPT_CONTEXT, &data, &argp->error);

	if (ret)
		goto e_free_trans_data;

	/* copy transport buffer to user space */
	if (copy_to_user((void __user *)(uintptr_t)params.trans_uaddr,
			 trans_data, params.trans_len)) {
		ret = -EFAULT;
		goto e_free_trans_data;
	}

	/* copy packet header to userspace. */
	if (copy_to_user((void __user *)(uintptr_t)params.hdr_uaddr, hdr,
			 params.hdr_len)) {
		ret = -EFAULT;
		goto e_free_trans_data;
	}

e_free_trans_data:
	vfree(trans_data);
e_free_trans_block:
	kfree(trans_block);
e_free_hdr:
	kfree(hdr);
exit:
	return ret;
}

static int csv_receive_encrypt_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct csv_data_receive_encrypt_data data;
	struct kvm_csv_receive_encrypt_data params;
	void *hdr;
	void *trans_data;
	struct trans_paddr_block *trans_block;
	struct guest_paddr_block *guest_block;
	unsigned long pfn;
	int i;
	u32 offset;
	int ret = 0;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (unlikely(list_empty(&csv->smr_list))) {
		/* Allocate all the guest memory from CMA */
		ret = csv_set_guest_private_memory(kvm);
		if (ret)
			goto exit;
	}

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	if (!params.hdr_uaddr || !params.hdr_len ||
	    !params.guest_addr_data || !params.guest_addr_len ||
	    !params.trans_uaddr || !params.trans_len)
		return -EINVAL;

	if (params.guest_addr_len > sizeof(*guest_block))
		return -EINVAL;

	if (params.trans_len > ARRAY_SIZE(trans_block->trans_paddr) * PAGE_SIZE)
		return -EINVAL;

	/* allocate memory for header and transport buffer */
	hdr = kzalloc(params.hdr_len, GFP_KERNEL_ACCOUNT);
	if (!hdr) {
		ret = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(hdr,
			  (void __user *)(uintptr_t)params.hdr_uaddr,
			   params.hdr_len)) {
		ret = -EFAULT;
		goto e_free_hdr;
	}

	guest_block = kzalloc(sizeof(*guest_block), GFP_KERNEL_ACCOUNT);
	if (!guest_block) {
		ret = -ENOMEM;
		goto e_free_hdr;
	}

	if (copy_from_user(guest_block,
			  (void __user *)(uintptr_t)params.guest_addr_data,
			   params.guest_addr_len)) {
		ret = -EFAULT;
		goto e_free_guest_block;
	}

	trans_block = kzalloc(sizeof(*trans_block), GFP_KERNEL_ACCOUNT);
	if (!trans_block) {
		ret = -ENOMEM;
		goto e_free_guest_block;
	}
	trans_data = vzalloc(params.trans_len);
	if (!trans_data) {
		ret = -ENOMEM;
		goto e_free_trans_block;
	}

	if (copy_from_user(trans_data,
			  (void __user *)(uintptr_t)params.trans_uaddr,
			  params.trans_len)) {
		ret = -EFAULT;
		goto e_free_trans_data;
	}

	for (offset = 0, i = 0; offset < params.trans_len; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(offset + trans_data);
		trans_block->trans_paddr[i] = __sme_set(pfn_to_hpa(pfn));
		i++;
	}

	memset(&data, 0, sizeof(data));
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_block = __psp_pa(trans_block);
	data.trans_len = params.trans_len;
	data.guest_block = __psp_pa(guest_block);
	data.guest_len = params.guest_addr_len;
	data.handle = sev->handle;

	clflush_cache_range(hdr, params.hdr_len);
	clflush_cache_range(trans_data, params.trans_len);
	clflush_cache_range(trans_block, PAGE_SIZE);
	clflush_cache_range(guest_block, PAGE_SIZE);
	ret = csv_issue_cmd(kvm, CSV_CMD_RECEIVE_ENCRYPT_DATA, &data,
			    &argp->error);

e_free_trans_data:
	vfree(trans_data);
e_free_trans_block:
	kfree(trans_block);
e_free_guest_block:
	kfree(guest_block);
e_free_hdr:
	kfree(hdr);
exit:
	return ret;
}

static int csv_receive_encrypt_context(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct csv_data_receive_encrypt_context data;
	struct kvm_csv_receive_encrypt_context params;
	void *hdr;
	void *trans_data;
	struct trans_paddr_block *trans_block;
	struct vmcb_paddr_block *shadow_vmcb_block;
	struct vmcb_paddr_block *secure_vmcb_block;
	unsigned long pfn;
	u32 offset;
	int ret = 0;
	struct kvm_vcpu *vcpu;
	unsigned long i;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	if (!params.trans_uaddr || !params.trans_len ||
	    !params.hdr_uaddr || !params.hdr_len)
		return -EINVAL;

	if (params.trans_len > ARRAY_SIZE(trans_block->trans_paddr) * PAGE_SIZE)
		return -EINVAL;

	/* allocate memory for header and transport buffer */
	hdr = kzalloc(params.hdr_len, GFP_KERNEL_ACCOUNT);
	if (!hdr) {
		ret = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(hdr,
			  (void __user *)(uintptr_t)params.hdr_uaddr,
			   params.hdr_len)) {
		ret = -EFAULT;
		goto e_free_hdr;
	}

	trans_block = kzalloc(sizeof(*trans_block), GFP_KERNEL_ACCOUNT);
	if (!trans_block) {
		ret = -ENOMEM;
		goto e_free_hdr;
	}
	trans_data = vzalloc(params.trans_len);
	if (!trans_data) {
		ret = -ENOMEM;
		goto e_free_trans_block;
	}

	if (copy_from_user(trans_data,
			  (void __user *)(uintptr_t)params.trans_uaddr,
			  params.trans_len)) {
		ret = -EFAULT;
		goto e_free_trans_data;
	}

	for (offset = 0, i = 0; offset < params.trans_len; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(offset + trans_data);
		trans_block->trans_paddr[i] = __sme_set(pfn_to_hpa(pfn));
		i++;
	}

	secure_vmcb_block = kzalloc(sizeof(*secure_vmcb_block),
				    GFP_KERNEL_ACCOUNT);
	if (!secure_vmcb_block) {
		ret = -ENOMEM;
		goto e_free_trans_data;
	}

	shadow_vmcb_block = kzalloc(sizeof(*shadow_vmcb_block),
				    GFP_KERNEL_ACCOUNT);
	if (!shadow_vmcb_block) {
		ret = -ENOMEM;
		goto e_free_secure_vmcb_block;
	}

	memset(&data, 0, sizeof(data));

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vcpu_svm *svm = to_svm(vcpu);

		if (i >= ARRAY_SIZE(shadow_vmcb_block->vmcb_paddr)) {
			ret = -EINVAL;
			goto e_free_shadow_vmcb_block;
		}
		shadow_vmcb_block->vmcb_paddr[i] = __sme_pa(svm->vmcb);
		data.vmcb_block_len += sizeof(shadow_vmcb_block->vmcb_paddr[0]);
	}

	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_block = __psp_pa(trans_block);
	data.trans_len = params.trans_len;
	data.shadow_vmcb_block = __psp_pa(shadow_vmcb_block);
	data.secure_vmcb_block = __psp_pa(secure_vmcb_block);
	data.handle = sev->handle;

	clflush_cache_range(hdr, params.hdr_len);
	clflush_cache_range(trans_data, params.trans_len);
	clflush_cache_range(trans_block, PAGE_SIZE);
	clflush_cache_range(shadow_vmcb_block, PAGE_SIZE);
	clflush_cache_range(secure_vmcb_block, PAGE_SIZE);

	ret = csv_issue_cmd(kvm, CSV_CMD_RECEIVE_ENCRYPT_CONTEXT, &data,
			    &argp->error);
	if (ret)
		goto e_free_shadow_vmcb_block;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vcpu_svm *svm = to_svm(vcpu);

		if (i >= ARRAY_SIZE(secure_vmcb_block->vmcb_paddr)) {
			ret = -EINVAL;
			goto e_free_shadow_vmcb_block;
		}

		svm->vmcb_pa = secure_vmcb_block->vmcb_paddr[i];
		svm->vcpu.arch.guest_state_protected = true;
	}

e_free_shadow_vmcb_block:
	kfree(shadow_vmcb_block);
e_free_secure_vmcb_block:
	kfree(secure_vmcb_block);
e_free_trans_data:
	vfree(trans_data);
e_free_trans_block:
	kfree(trans_block);
e_free_hdr:
	kfree(hdr);
exit:
	return ret;
}

static void csv_mark_page_dirty(struct kvm_vcpu *vcpu, gva_t gpa,
				unsigned long npages)
{
	gfn_t gfn;
	gfn_t gfn_end;

	gfn = gpa >> PAGE_SHIFT;
	gfn_end = gfn + npages;
	spin_lock(&vcpu->kvm->mmu_lock);
	for (; gfn < gfn_end; gfn++)
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

static int csv_mmio_page_fault(struct kvm_vcpu *vcpu, gva_t gpa, u32 error_code)
{
	int r = 0;
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	union csv_page_attr page_attr = {.mmio = 1};
	union csv_page_attr page_attr_mask = {.mmio = 1};
	struct csv_data_update_npt *update_npt;
	int psp_ret;

	update_npt = kzalloc(sizeof(*update_npt), GFP_KERNEL);
	if (!update_npt) {
		r = -ENOMEM;
		goto exit;
	}

	csv_init_update_npt(update_npt, gpa, error_code,
			    kvm_svm->sev_info.handle);
	update_npt->page_attr = page_attr.val;
	update_npt->page_attr_mask = page_attr_mask.val;
	update_npt->level = CSV_PG_LEVEL_4K;

	r = csv_issue_cmd(vcpu->kvm, CSV_CMD_UPDATE_NPT, update_npt, &psp_ret);

	if (psp_ret != SEV_RET_SUCCESS)
		r = -EFAULT;

	kfree(update_npt);
exit:
	return r;
}

static int __csv_page_fault(struct kvm_vcpu *vcpu, gva_t gpa,
			    u32 error_code, struct kvm_memory_slot *slot,
			    int *psp_ret_ptr, kvm_pfn_t pfn, u32 level)
{
	int r = 0;
	struct csv_data_update_npt *update_npt;
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	int psp_ret = 0;

	update_npt = kzalloc(sizeof(*update_npt), GFP_KERNEL);
	if (!update_npt) {
		r = -ENOMEM;
		goto exit;
	}

	csv_init_update_npt(update_npt, gpa, error_code,
			    kvm_svm->sev_info.handle);

	update_npt->spa = pfn << PAGE_SHIFT;
	update_npt->level = level;

	if (!csv_is_mmio_pfn(pfn))
		update_npt->spa |= sme_me_mask;

	r = csv_issue_cmd(vcpu->kvm, CSV_CMD_UPDATE_NPT, update_npt, &psp_ret);

	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	kvm_flush_remote_tlbs(vcpu->kvm);

	csv_mark_page_dirty(vcpu, update_npt->gpa, update_npt->npages);

	if (psp_ret_ptr)
		*psp_ret_ptr = psp_ret;

	kfree(update_npt);
exit:
	return r;
}

static int csv_pin_shared_memory(struct kvm_vcpu *vcpu,
				 struct kvm_memory_slot *slot, gfn_t gfn,
				 kvm_pfn_t *pfn)
{
	struct page *page;
	u64 hva;
	int npinned;
	kvm_pfn_t tmp_pfn;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct shared_page *sp;
	bool write = !(slot->flags & KVM_MEM_READONLY);
	bool is_dma_pinned;

	tmp_pfn = __gfn_to_pfn_memslot(slot, gfn, false, NULL, write, NULL);
	if (unlikely(is_error_pfn(tmp_pfn)))
		return -ENOMEM;

	if (csv_is_mmio_pfn(tmp_pfn)) {
		*pfn = tmp_pfn;
		return 0;
	}

	is_dma_pinned = page_maybe_dma_pinned(pfn_to_page(tmp_pfn));
	kvm_release_pfn_clean(tmp_pfn);
	if (is_dma_pinned) {
		*pfn = tmp_pfn;
		return 0;
	}

	sp = shared_page_search(&csv->sp_mgr, gfn);
	if (!sp) {
		sp = kmem_cache_zalloc(csv->sp_slab, GFP_KERNEL);
		if (!sp)
			return -ENOMEM;

		hva = __gfn_to_hva_memslot(slot, gfn);
		npinned = pin_user_pages_fast(hva, 1, FOLL_WRITE | FOLL_LONGTERM, &page);
		if (npinned != 1) {
			kmem_cache_free(csv->sp_slab, sp);
			return -ENOMEM;
		}

		sp->page = page;
		sp->gfn = gfn;
		shared_page_insert(&csv->sp_mgr, sp);
	}

	*pfn = page_to_pfn(sp->page);

	return 0;
}

static int csv_mapping_level(struct kvm_vcpu *vcpu, gfn_t gfn, kvm_pfn_t pfn,
			     struct kvm_memory_slot *slot)
{
	unsigned long hva;
	int level;
	pte_t *pte;
	int page_num;
	gfn_t gfn_base;

	if (csv_is_mmio_pfn(pfn)) {
		level = PG_LEVEL_4K;
		goto end;
	}

	if (!PageCompound(pfn_to_page(pfn))) {
		level = PG_LEVEL_4K;
		goto end;
	}

	level = PG_LEVEL_2M;
	page_num = KVM_PAGES_PER_HPAGE(level);
	gfn_base = gfn & ~(page_num - 1);

	/*
	 * 2M aligned guest address in memslot.
	 */
	if ((gfn_base < slot->base_gfn) ||
	    (gfn_base + page_num > slot->base_gfn + slot->npages)) {
		level = PG_LEVEL_4K;
		goto end;
	}

	/*
	 * hva in memslot is 2M aligned.
	 */
	if (__gfn_to_hva_memslot(slot, gfn_base) & ~PMD_PAGE_MASK) {
		level = PG_LEVEL_4K;
		goto end;
	}

	hva = __gfn_to_hva_memslot(slot, gfn);
	pte = lookup_address_in_mm(vcpu->kvm->mm, hva, &level);
	if (unlikely(!pte)) {
		level = PG_LEVEL_4K;
		goto end;
	}

	/*
	 * Firmware supports 2M/4K level.
	 */
	level = level > PG_LEVEL_2M ? PG_LEVEL_2M : level;

end:
	return to_csv_pg_level(level);
}

static int csv_page_fault(struct kvm_vcpu *vcpu, struct kvm_memory_slot *slot,
			  gfn_t gfn, u32 error_code)
{
	int ret = 0;
	int psp_ret = 0;
	int level;
	kvm_pfn_t pfn = KVM_PFN_NOSLOT;
	struct kvm_csv_info *csv = &to_kvm_svm_csv(vcpu->kvm)->csv_info;

	if (error_code & PFERR_PRESENT_MASK)
		level = CSV_PG_LEVEL_4K;
	else {
		mutex_lock(&csv->sp_lock);
		ret = csv_pin_shared_memory(vcpu, slot, gfn, &pfn);
		mutex_unlock(&csv->sp_lock);
		if (ret)
			goto exit;

		level = csv_mapping_level(vcpu, gfn, pfn, slot);
	}

	ret = __csv_page_fault(vcpu, gfn << PAGE_SHIFT, error_code, slot,
			       &psp_ret, pfn, level);

	if (psp_ret != SEV_RET_SUCCESS)
		ret = -EFAULT;
exit:
	return ret;
}

/**
 *  Return negative error code on fail,
 *  or return the number of pages unpinned successfully
 */
static int csv_unpin_shared_memory(struct kvm *kvm, gpa_t gpa, u32 num_pages)
{
	struct kvm_csv_info *csv;
	struct shared_page *sp;
	gfn_t gfn;
	unsigned long i;
	int unpin_cnt = 0;

	csv = &to_kvm_svm_csv(kvm)->csv_info;
	gfn = gpa_to_gfn(gpa);
	mutex_lock(&csv->sp_lock);
	for (i = 0; i < num_pages; i++, gfn++) {
		sp = shared_page_remove(&csv->sp_mgr, gfn);
		if (sp) {
			unpin_user_page(sp->page);
			kmem_cache_free(csv->sp_slab, sp);
			csv->sp_mgr.count--;
			unpin_cnt++;
		}
	}
	mutex_unlock(&csv->sp_lock);

	return unpin_cnt;
}

static void csv_vm_destroy(struct kvm *kvm)
{
	struct kvm_csv_info *csv = &to_kvm_svm_csv(kvm)->csv_info;
	struct kvm_vcpu *vcpu;
	struct list_head *smr_head = &csv->smr_list;
	struct list_head *pos, *q;
	struct secure_memory_region *smr;
	struct shared_page *sp;
	struct rb_node *node;
	unsigned long i = 0;

	if (csv_guest(kvm)) {
		mutex_lock(&csv->sp_lock);
		while ((node = rb_first(&csv->sp_mgr.root))) {
			sp = rb_entry(node, struct shared_page, node);
			rb_erase(&sp->node, &csv->sp_mgr.root);
			unpin_user_page(sp->page);
			kmem_cache_free(csv->sp_slab, sp);
			csv->sp_mgr.count--;
		}
		mutex_unlock(&csv->sp_lock);

		kmem_cache_destroy(csv->sp_slab);
		csv->sp_slab = NULL;

		kvm_for_each_vcpu(i, vcpu, kvm) {
			struct vcpu_svm *svm = to_svm(vcpu);
			svm->vmcb_pa = __sme_pa(svm->vmcb);
		}
	}

	if (likely(csv_x86_ops.vm_destroy))
		csv_x86_ops.vm_destroy(kvm);

	if (!csv_guest(kvm))
		return;

	/* free secure memory region */
	if (!list_empty(smr_head)) {
		list_for_each_safe(pos, q, smr_head) {
			smr = list_entry(pos, struct secure_memory_region, list);
			if (smr) {
				csv_release_to_contiguous(smr->hpa, smr->npages << PAGE_SHIFT);
				list_del(&smr->list);
				kfree(smr);
			}
		}
	}
}

static int csv_handle_page_fault(struct kvm_vcpu *vcpu, gpa_t gpa,
				 u32 error_code)
{
	gfn_t gfn = gpa_to_gfn(gpa);
	struct kvm_memory_slot *slot = gfn_to_memslot(vcpu->kvm, gfn);
	int ret;
	int r = -EIO;

	if (kvm_is_visible_memslot(slot))
		ret = csv_page_fault(vcpu, slot, gfn, error_code);
	else
		ret = csv_mmio_page_fault(vcpu, gpa, error_code);

	if (!ret)
		r = 1;

	return r;
}

static int csv_handle_exit(struct kvm_vcpu *vcpu, fastpath_t exit_fastpath)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 exit_code = svm->vmcb->control.exit_code;
	int ret = -EIO;

	/*
	 * NPF for csv is dedicated.
	 */
	if (csv_guest(vcpu->kvm) && exit_code == SVM_EXIT_NPF) {
		gpa_t gpa = __sme_clr(svm->vmcb->control.exit_info_2);
		u64 error_code = svm->vmcb->control.exit_info_1;

		ret = csv_handle_page_fault(vcpu, gpa, error_code);
	} else {
		if (likely(csv_x86_ops.handle_exit))
			ret = csv_x86_ops.handle_exit(vcpu, exit_fastpath);
	}

	return ret;
}

static void csv_guest_memory_reclaimed(struct kvm *kvm)
{
	if (!csv_guest(kvm)) {
		if (likely(csv_x86_ops.guest_memory_reclaimed))
			csv_x86_ops.guest_memory_reclaimed(kvm);
	}
}

static int csv_handle_memory(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_csv_handle_memory params;
	int r = -EINVAL;

	if (!csv_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, (void __user *)(uintptr_t)argp->data,
			   sizeof(params)))
		return -EFAULT;

	switch (params.opcode) {
	case KVM_CSV_RELEASE_SHARED_MEMORY:
		r = csv_unpin_shared_memory(kvm, params.gpa, params.num_pages);
		break;
	default:
		break;
	}

	return r;
};

static int csv_mem_enc_op(struct kvm *kvm, void __user *argp)
{
	struct kvm_sev_cmd sev_cmd;
	int r = -EINVAL;

	if (!argp)
		return 0;

	if (copy_from_user(&sev_cmd, argp, sizeof(struct kvm_sev_cmd)))
		return -EFAULT;

	mutex_lock(&kvm->lock);

	switch (sev_cmd.id) {
	case KVM_CSV_INIT:
		r = csv_guest_init(kvm, &sev_cmd);
		break;
	case KVM_CSV_LAUNCH_ENCRYPT_DATA:
		r = csv_launch_encrypt_data(kvm, &sev_cmd);
		break;
	case KVM_CSV_LAUNCH_ENCRYPT_VMCB:
		r = csv_launch_encrypt_vmcb(kvm, &sev_cmd);
		break;
	case KVM_CSV_SEND_ENCRYPT_DATA:
		r = csv_send_encrypt_data(kvm, &sev_cmd);
		break;
	case KVM_CSV_SEND_ENCRYPT_CONTEXT:
		r = csv_send_encrypt_context(kvm, &sev_cmd);
		break;
	case KVM_CSV_RECEIVE_ENCRYPT_DATA:
		r = csv_receive_encrypt_data(kvm, &sev_cmd);
		break;
	case KVM_CSV_RECEIVE_ENCRYPT_CONTEXT:
		r = csv_receive_encrypt_context(kvm, &sev_cmd);
		break;
	case KVM_CSV_HANDLE_MEMORY:
		r = csv_handle_memory(kvm, &sev_cmd);
		break;
	default:
		mutex_unlock(&kvm->lock);
		if (likely(csv_x86_ops.mem_enc_op))
			r = csv_x86_ops.mem_enc_op(kvm, argp);
		goto out;
	}

	mutex_unlock(&kvm->lock);

	if (copy_to_user(argp, &sev_cmd, sizeof(struct kvm_sev_cmd)))
		r = -EFAULT;

out:
	return r;
}

#define CSV_BIT		BIT(30)

void __init csv_init(struct kvm_x86_ops *ops)
{
	unsigned int eax, ebx, ecx, edx;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return;

	/* Retrieve CSV CPUID information */
	cpuid(0x8000001f, &eax, &ebx, &ecx, &edx);
	if (eax & CSV_BIT) {
		memcpy(&csv_x86_ops, ops, sizeof(struct kvm_x86_ops));

		ops->mem_enc_op = csv_mem_enc_op;
		ops->vm_destroy = csv_vm_destroy;
		ops->vm_size = sizeof(struct kvm_svm_csv);
		ops->handle_exit = csv_handle_exit;
		ops->guest_memory_reclaimed = csv_guest_memory_reclaimed;
	}
}
