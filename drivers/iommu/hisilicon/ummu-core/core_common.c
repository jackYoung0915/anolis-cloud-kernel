// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., All rights reserved.
 * Description: common built-in symbols.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/dma-map-ops.h>

#include "../sva.h"
#include "ummu_core_priv.h"

LIST_HEAD(core_device_list);
EXPORT_SYMBOL_NS_GPL(core_device_list, UMMU_CORE_INTERNAL);

struct ummu_core_device *global_core_device;
EXPORT_SYMBOL_NS_GPL(global_core_device, UMMU_CORE_INTERNAL);

DEFINE_MUTEX(global_device_lock);
EXPORT_SYMBOL_NS_GPL(global_device_lock, UMMU_CORE_INTERNAL);

void setup_tdev_dma_ops(struct device *dev, bool coherent)
{
	arch_setup_dma_ops(dev, coherent);
}
EXPORT_SYMBOL_NS_GPL(setup_tdev_dma_ops, UMMU_CORE_INTERNAL);

int ummu_dev_enable_feat(struct device *dev, enum iommu_dev_features feat)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);

	if (!master) {
		pr_err("get invalid dev!\n");
		return -ENODEV;
	}

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		return -EOPNOTSUPP;
	case IOMMU_DEV_FEAT_SVA:
	case IOMMU_DEV_FEAT_KSVA:
		return ummu_master_enable_sva(master, feat);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(ummu_dev_enable_feat);

int ummu_dev_disable_feat(struct device *dev, enum iommu_dev_features feat)
{
	struct ummu_master *master =
		(struct ummu_master *)dev_iommu_priv_get(dev);

	if (!master) {
		pr_err("get invalid dev!\n");
		return -ENODEV;
	}

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		return -EOPNOTSUPP;
	case IOMMU_DEV_FEAT_SVA:
	case IOMMU_DEV_FEAT_KSVA:
		return ummu_master_disable_sva(master, feat);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(ummu_dev_disable_feat);
