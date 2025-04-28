// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_common.h"

#ifdef NBL_IOMMU_TRANSLATE
#include <linux/iova.h>
#include <linux/iommu.h>

#define NBL_DEV_TO_DMA_CTRL(dma_dev)	container_of(dma_dev, struct nbl_dma_ctrl, dev)
#define NBL_DMA_IOVA_LIMIT		DMA_BIT_MASK(47)

struct nbl_dma_ctrl {
	struct device dev;
	struct iova_domain iovad;
	struct device_dma_parameters dma_parms;
	u64 dma_mask;
	struct iommu_domain *domain;
	struct device *real_dev;
};

static int nbl_iommu_device_cnt(struct device *dev, void *data)
{
	int *cnt = (int *)data;

	(*cnt)++;

	return 0;
}

static void nbl_dma_release_dev(struct device *dev)
{
	dev_info(dev, "nbl dma device release\n");
}

static int nbl_dma_info_to_prot(enum dma_data_direction dir, unsigned long attrs)
{
	int prot = IOMMU_CACHE;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

static dma_addr_t nbl_dma_alloc_iova(struct iova_domain *iovad, size_t size, struct device *dev)
{
	unsigned long shift, iova_len, iova = 0;
	u64 dma_limit;
#if (KERNEL_VERSION(5, 14, 21) == LINUX_VERSION_CODE)
	struct iova *new_iova = NULL;
#endif

	shift = iova_shift(iovad);
	iova_len = size >> shift;

	if (iova_len < (1 << (IOVA_RANGE_CACHE_MAX_SIZE - 1)))
		iova_len = roundup_pow_of_two(iova_len);

	dma_limit = min_not_zero(NBL_DMA_IOVA_LIMIT, dev->bus_dma_limit);
	/* Try to get PCI devices a SAC address */
	if (dma_limit > DMA_BIT_MASK(32))
#if (KERNEL_VERSION(5, 14, 21) == LINUX_VERSION_CODE)
		new_iova = alloc_iova(iovad, iova_len, DMA_BIT_MASK(32) >> shift, true);
	if (new_iova)
		iova = new_iova->pfn_lo;
#else
		iova = alloc_iova_fast(iovad, iova_len, DMA_BIT_MASK(32) >> shift, false);
#endif

	if (!iova)
#if (KERNEL_VERSION(5, 14, 21) == LINUX_VERSION_CODE)
		new_iova = alloc_iova(iovad, iova_len, dma_limit >> shift, true);
	if (new_iova)
		iova = new_iova->pfn_lo;
#else
		iova = alloc_iova_fast(iovad, iova_len, dma_limit >> shift, true);
#endif

	return (dma_addr_t)iova << shift;
}

static void nbl_dma_free_iova(struct iova_domain *iovad, dma_addr_t iova, size_t size)
{
#if (KERNEL_VERSION(5, 14, 21) == LINUX_VERSION_CODE)
	free_iova(iovad, iova_pfn(iovad, iova));
#else
	free_iova_fast(iovad, iova_pfn(iovad, iova), size >> iova_shift(iovad));
#endif
}

static dma_addr_t nbl_dma_map(struct nbl_dma_ctrl *ctrl, phys_addr_t phys, size_t size, int prot)
{
	struct iommu_domain *domain = ctrl->domain;
	struct iova_domain *iovad = &ctrl->iovad;
	size_t iova_off = iova_offset(iovad, phys);
	dma_addr_t iova;

	size = iova_align(iovad, size + iova_off);
	iova = nbl_dma_alloc_iova(iovad, size, ctrl->real_dev);
	if (!iova)
		return DMA_MAPPING_ERROR;

	if (iommu_map(domain, iova, phys - iova_off, size, prot, GFP_ATOMIC)) {
		nbl_dma_free_iova(iovad, iova, size);
		return DMA_MAPPING_ERROR;
	}

	return iova + iova_off;
}

static void nbl_dma_unmap(struct nbl_dma_ctrl *ctrl, dma_addr_t dma_addr, size_t size)
{
	struct iommu_domain *domain = ctrl->domain;
	struct iova_domain *iovad = &ctrl->iovad;
	size_t iova_off = iova_offset(iovad, dma_addr);
	struct iommu_iotlb_gather iotlb_gather;
	size_t unmapped;

	dma_addr -= iova_off;
	size = iova_align(iovad, size + iova_off);
	iommu_iotlb_gather_init(&iotlb_gather);
	// iotlb_gather.queued = 0;

	unmapped = iommu_unmap_fast(domain, dma_addr, size, &iotlb_gather);
	WARN_ON(unmapped != size);

	iommu_iotlb_sync(domain, &iotlb_gather);
	nbl_dma_free_iova(iovad, dma_addr, size);
}

static void *nbl_dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_addr,
				    gfp_t flag, unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);
	int node = dev_to_node(ctrl->real_dev);
	int ioprot = IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE;
	size_t alloc_size = PAGE_ALIGN(size);
	struct page *page = NULL;
	void *addr;

	page = dma_alloc_contiguous(ctrl->real_dev, alloc_size, flag);
	if (!page)
		page = alloc_pages_node(node, flag, get_order(alloc_size));
	if (!page)
		return NULL;

	addr = page_address(page);
	memset(addr, 0, alloc_size);

	*dma_addr = nbl_dma_map(ctrl, page_to_phys(page), size, ioprot);
	if (*dma_addr == DMA_MAPPING_ERROR) {
		dma_free_contiguous(ctrl->real_dev, page, alloc_size);
		return NULL;
	}

	return addr;
}

static void nbl_dma_free_coherent(struct device *dev, size_t size,
				  void *cpu_addr, dma_addr_t handle, unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);
	struct page *page = NULL;
	size_t alloc_size = PAGE_ALIGN(size);

	nbl_dma_unmap(ctrl, handle, size);

	page = virt_to_page(cpu_addr);
	if (page)
		dma_free_contiguous(ctrl->real_dev, page, alloc_size);
}

static dma_addr_t nbl_dma_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size, enum dma_data_direction dir,
				   unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);
	phys_addr_t phys = page_to_phys(page) + offset;
	int prot = nbl_dma_info_to_prot(dir, attrs);

	/* ignore swiotlb&bounce page */
	return nbl_dma_map(ctrl, phys, size, prot);
}

static void nbl_dma_unmap_page(struct device *dev, dma_addr_t dma_handle, size_t size,
			       enum dma_data_direction dir, unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);

	nbl_dma_unmap(ctrl, dma_handle, size);
}

static void nbl_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			     int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);
	dma_addr_t start, end;
	struct scatterlist *tmp;
	int i;

	start = sg_dma_address(sg);
	for_each_sg(sg_next(sg), tmp, nents - 1, i) {
		if (sg_dma_len(tmp) == 0)
			break;
		sg = tmp;
	}

	end = sg_dma_address(sg) + sg_dma_len(sg);
	nbl_dma_unmap(ctrl, start, end - start);
}

/**
 * Prepare a successfully-mapped scatterlist to give back to the caller.
 *
 * At this point the segments are already laid out by iommu_dma_map_sg() to
 * avoid individually crossing any boundaries, so we merely need to check a
 * segment's start address to avoid concatenating across one.
 */
static int __finalise_sg(struct device *dev, struct scatterlist *sg, int nents,
			 dma_addr_t dma_addr)
{
	struct scatterlist *s, *cur = sg;
	unsigned long seg_mask = dma_get_seg_boundary(dev);
	unsigned int cur_len = 0, max_len = dma_get_max_seg_size(dev);
	int i, count = 0;

	for_each_sg(sg, s, nents, i) {
		/* Restore this segment's original unaligned fields first */
		unsigned int s_iova_off = sg_dma_address(s);
		unsigned int s_length = sg_dma_len(s);
		unsigned int s_iova_len = s->length;

		s->offset += s_iova_off;
		s->length = s_length;
		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;

		/**
		 * Now fill in the real DMA data. If...
		 * - there is a valid output segment to append to
		 * - and this segment starts on an IOVA page boundary
		 * - but doesn't fall at a segment boundary
		 * - and wouldn't make the resulting output segment too long
		 */
		if (cur_len && !s_iova_off && (dma_addr & seg_mask) &&
		    (max_len - cur_len >= s_length)) {
			/* ...then concatenate it with the previous one */
			cur_len += s_length;
		} else {
			/* Otherwise start the next output segment */
			if (i > 0)
				cur = sg_next(cur);
			cur_len = s_length;
			count++;

			sg_dma_address(cur) = dma_addr + s_iova_off;
		}

		sg_dma_len(cur) = cur_len;
		dma_addr += s_iova_len;

		if (s_length + s_iova_off < s_iova_len)
			cur_len = 0;
	}
	return count;
}

/**
 * If mapping failed, then just restore the original list,
 * but making sure the DMA fields are invalidated.
 */
static void __invalidate_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_address(s) != DMA_MAPPING_ERROR)
			s->offset += sg_dma_address(s);
		if (sg_dma_len(s))
			s->length = sg_dma_len(s);
		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;
	}
}

static int nbl_dma_msg_sg(struct device *dev, struct scatterlist *sg,
			  int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);
	struct iova_domain *iovad = &ctrl->iovad;
	struct scatterlist *s, *prev = NULL;
	int prot = nbl_dma_info_to_prot(dir, attrs);
	dma_addr_t iova;
	size_t iova_len = 0;
	unsigned long mask = dma_get_seg_boundary(ctrl->real_dev);
	ssize_t ret;
	int i;

	/**
	 * Work out how much IOVA space we need, and align the segments to
	 * IOVA granules for the IOMMU driver to handle. With some clever
	 * trickery we can modify the list in-place, but reversibly, by
	 * stashing the unaligned parts in the as-yet-unused DMA fields.
	 */
	for_each_sg(sg, s, nents, i) {
		size_t s_iova_off = iova_offset(iovad, s->offset);
		size_t s_length = s->length;
		size_t pad_len = (mask - iova_len + 1) & mask;

		sg_dma_address(s) = s_iova_off;
		sg_dma_len(s) = s_length;
		s->offset -= s_iova_off;
		s_length = iova_align(iovad, s_length + s_iova_off);
		s->length = s_length;

		/**
		 * Due to the alignment of our single IOVA allocation, we can
		 * depend on these assumptions about the segment boundary mask:
		 * - If mask size >= IOVA size, then the IOVA range cannot
		 *   possibly fall across a boundary, so we don't care.
		 * - If mask size < IOVA size, then the IOVA range must start
		 *   exactly on a boundary, therefore we can lay things out
		 *   based purely on segment lengths without needing to know
		 *   the actual addresses beforehand.
		 * - The mask must be a power of 2, so pad_len == 0 if
		 *   iova_len == 0, thus we cannot dereference prev the first
		 *   time through here (i.e. before it has a meaningful value).
		 */
		if (prev && pad_len && pad_len < s_length - 1) {
			prev->length += pad_len;
			iova_len += pad_len;
		}

		iova_len += s_length;
		prev = s;
	}

	iova = nbl_dma_alloc_iova(iovad, iova_len, ctrl->real_dev);
	if (!iova) {
		ret = -ENOMEM;
		goto out_restore_sg;
	}

	ret = iommu_map_sg(ctrl->domain, iova, sg, nents, prot, GFP_ATOMIC);
	if (ret < 0 || ret < iova_len)
		goto out_free_iova;

	return __finalise_sg(ctrl->real_dev, sg, nents, iova);

out_free_iova:
	nbl_dma_free_iova(iovad, iova, iova_len);
out_restore_sg:
	__invalidate_sg(sg, nents);
	if (ret != -ENOMEM)
		return -EINVAL;
	return ret;
}

static unsigned long nbl_dma_get_merge_boundary(struct device *dev)
{
	struct nbl_dma_ctrl *ctrl = NBL_DEV_TO_DMA_CTRL(dev);

	return (1UL << __ffs(ctrl->domain->pgsize_bitmap)) - 1;
}

static const struct dma_map_ops nbl_dma_ops = {
	.alloc = nbl_dma_alloc_coherent,
	.free = nbl_dma_free_coherent,
	.map_page = nbl_dma_map_page,
	.unmap_page = nbl_dma_unmap_page,
	.map_sg = nbl_dma_msg_sg,
	.unmap_sg = nbl_dma_unmap_sg,
	.get_merge_boundary = nbl_dma_get_merge_boundary,
};

int nbl_dma_iommu_change_translate(struct nbl_common_info *common)
{
	struct nbl_dma_ctrl *ctrl;
	struct iommu_group *iommu_group;
	struct bus_type *bus;
	u64 dma_limit;
	unsigned long order;
	int ret = 0, cnt = 0;
	bool iommu_status, remap_status;

	iommu_status = nbl_dma_iommu_status(common->pdev);
	remap_status = nbl_dma_remap_status(common->pdev, &dma_limit);

	if (!iommu_status || remap_status)
		return 0;

	if (!dev_is_dma_coherent(common->dev))
		return 0;

	if (dma_get_mask(common->dev) != DMA_BIT_MASK(64))
		return 0;

	/* check if iommu_group per device */
	bus = (struct bus_type *)common->dev->bus;
	iommu_group = common->dev->iommu_group;
	if (!iommu_group)
		return -EINVAL;

	ret = iommu_group_for_each_dev(iommu_group, &cnt, nbl_iommu_device_cnt);
	if (ret) {
		dev_err(common->dev, "iommu group device cnt failed, ret %d\n", ret);
		return ret;
	}

	if (cnt != 1) {
		dev_err(common->dev, "iommu group device cnt %d\n", cnt);
		return -EINVAL;
	}

	ctrl = devm_kzalloc(common->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->domain = iommu_domain_alloc(bus);
	if (!ctrl->domain) {
		dev_err(common->dev, "iommu new domain alloc failed\n");
		ret = -EIO;
		goto out_free;
	}

	ret = iommu_attach_group(ctrl->domain, iommu_group);
	if (ret) {
		dev_err(common->dev, "iommu attach group failed, ret %d\n", ret);
		goto out_domain;
	}

	/* init new dma dev */
	ctrl->real_dev = common->dev;
	device_initialize(&ctrl->dev);
	ctrl->dev.parent = common->dev;
	ctrl->dev.release = nbl_dma_release_dev;

	ctrl->dma_mask = common->pdev->dma_mask;
	ctrl->dev.dma_mask = &ctrl->dma_mask;
	ctrl->dev.coherent_dma_mask = common->dev->coherent_dma_mask;
	memcpy(&ctrl->dma_parms, &common->pdev->dma_parms, sizeof(ctrl->dma_parms));
	ctrl->dev.dma_parms = &ctrl->dma_parms;
	dev_set_name(&ctrl->dev, pci_name(common->pdev));

	/* set dma mask */
	ctrl->dev.dma_mask = &ctrl->dev.coherent_dma_mask;
	if (dma_set_mask_and_coherent(&ctrl->dev, DMA_BIT_MASK(64))) {
		dev_err(common->dev, "set mask and coherent failed\n");
		goto out_detach;
	}

	set_dma_ops(&ctrl->dev, &nbl_dma_ops);

	ret = iova_cache_get();
	if (ret) {
		dev_err(common->dev, "iova cache get failed\n");
		goto out_detach;
	}

	order = __ffs(ctrl->domain->pgsize_bitmap);
	init_iova_domain(&ctrl->iovad, 1ULL << order, 0);

	common->dma_dev = &ctrl->dev;
	dev_info(common->dev, "init new dma device, order %lu\n", order);

	return ret;

out_detach:
	iommu_detach_group(ctrl->domain, iommu_group);
out_domain:
	iommu_domain_free(ctrl->domain);
out_free:
	devm_kfree(common->dev, ctrl);

	return ret;
}

void nbl_dma_iommu_exit_translate(struct nbl_common_info *common)
{
	struct nbl_dma_ctrl *ctrl;
	struct iommu_group *iommu_group;

	if (common->dma_dev == common->dev)
		return;

	iommu_group = common->dev->iommu_group;
	ctrl = NBL_DEV_TO_DMA_CTRL(common->dma_dev);

	/* todolist: ensure that all memory has been freed */
	put_iova_domain(&ctrl->iovad);
	iova_cache_put();

	iommu_detach_group(ctrl->domain, iommu_group);
	iommu_domain_free(ctrl->domain);
	devm_kfree(common->dev, ctrl);
	common->dma_dev = common->dev;
}

#endif /* end ifdef NBL_USERDEV */
