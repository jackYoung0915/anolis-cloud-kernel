/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * Description: UMMU CORE private interfaces.
 */

#ifndef __UMMU_CORE_PRIV_H__
#define __UMMU_CORE_PRIV_H__

#include <linux/ummu_core.h>
#include <linux/iommu.h>

/* private initialization */

/* private definition */
extern struct ummu_core_device *global_core_device;
extern struct mutex global_device_lock;

/* private interfaces */
void ummu_flush_cached_eid(struct ummu_core_device *core_device);
struct device *ummu_alloc_tdev(struct tdev_attr *attr, u32 *ptid);
int ummu_core_get_resource(struct iommu_sva *sva, struct device *dev,
			   struct resource_args *args);
void ummu_core_put_resource(struct iommu_sva *sva, struct device *dev,
			    struct resource_args *args);
int ummu_core_get_hw_cap(struct device *dev, u32 *hw_cap);
struct mm_struct *ummu_core_get_mm(struct ummu_core_device *ummu_core,
				   u32 tid);
void setup_tdev_dma_ops(struct device *dev, bool coherent);
#endif /* __UMMU_CORE_PRIV_H__ */
