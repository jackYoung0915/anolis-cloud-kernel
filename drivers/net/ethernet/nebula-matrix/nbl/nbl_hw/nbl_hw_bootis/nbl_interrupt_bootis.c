// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_interrupt_bootis.h"
#include "nbl_resource_bootis.h"

static int nbl_res_intr_destroy_msix_map(void *priv, u16 func_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct device *dev, *dma_dev;
	struct nbl_phy_ops *phy_ops;
	struct nbl_interrupt_mgt *intr_mgt;
	struct nbl_common_info *common;
	u16 *interrupts;
	u16 intr_num;
	u16 i;
	int ret = 0;

	if (!res_mgt)
		return -EINVAL;

	phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	dma_dev = NBL_RES_MGT_TO_DMA_DEV(res_mgt);
	common = NBL_RES_MGT_TO_COMMON(res_mgt);

	func_id = nbl_res_vsi_id_to_func_id(res_mgt, common->vsi_id);

	intr_num = intr_mgt->func_intr_res[func_id].num_interrupts;
	interrupts = intr_mgt->func_intr_res[func_id].interrupts;

	WARN_ON(!interrupts);
	for (i = 0; i < intr_num; i++)
		phy_ops->configure_msix_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), func_id, false,
					     interrupts[i], 0, 0, 0, false);
	bitmap_clear(intr_mgt->interrupt_net_bitmap, 0, NBL_BOOTIS_MAX_NET_INTERRUPT);
	bitmap_clear(intr_mgt->interrupt_others_bitmap, 0, NBL_BOOTIS_MAX_OTHER_INTERRUPT);
	kfree(interrupts);
	intr_mgt->func_intr_res[func_id].interrupts = NULL;
	intr_mgt->func_intr_res[func_id].num_interrupts = 0;

	return ret;
}

static int nbl_res_intr_configure_msix_map(void *priv, u16 func_id, u16 num_net_msix,
					   u16 num_others_msix, bool net_msix_mask_en)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct device *dev, *dma_dev;
	struct nbl_phy_ops *phy_ops;
	struct nbl_interrupt_mgt *intr_mgt;
	struct nbl_common_info *common;
	struct nbl_func_interrupt_resource_mng *func_intr_res;
	u16 *interrupts;
	u16 requested;
	u16 intr_index;
	u16 i;
	u32 intr_base;
	u8 bus, devid, function;
	bool msix_mask_en;
	int ret = 0;

	if (!res_mgt)
		return -EINVAL;

	phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	dma_dev = NBL_RES_MGT_TO_DMA_DEV(res_mgt);
	common = NBL_RES_MGT_TO_COMMON(res_mgt);

	func_id = nbl_res_vsi_id_to_func_id(res_mgt, common->vsi_id);
	func_intr_res = &intr_mgt->func_intr_res[func_id];
	intr_base = func_intr_res->msix_base;

	if (func_intr_res->interrupts)
		nbl_res_intr_destroy_msix_map(priv, func_id);

	nbl_res_func_id_to_bdf(res_mgt, func_id, &bus, &devid, &function);

	requested = num_net_msix + num_others_msix;
	interrupts = kcalloc(requested, sizeof(interrupts[0]), GFP_ATOMIC);
	if (!interrupts) {
		pr_err("Allocate function interrupts array failed\n");
		ret = -ENOMEM;
		goto alloc_interrupts_err;
	}

	pr_info("alloc the %u interrupts for func %u.\n", requested, func_id);

	func_intr_res->interrupts = interrupts;
	func_intr_res->num_interrupts = requested;

	for (i = 0; i < num_net_msix; i++) {
		intr_index = find_first_zero_bit(intr_mgt->interrupt_net_bitmap,
						 NBL_BOOTIS_MAX_NET_INTERRUPT);
		if (intr_index == NBL_BOOTIS_MAX_NET_INTERRUPT) {
			pr_err("There is no available interrupt left\n");
			ret = -EAGAIN;
			goto get_interrupt_err;
		}
		interrupts[i] = intr_base + intr_index;
		set_bit(intr_index, intr_mgt->interrupt_net_bitmap);
	}

	for (i = num_net_msix; i < requested; i++) {
		intr_index = find_first_zero_bit(intr_mgt->interrupt_others_bitmap,
						 NBL_BOOTIS_MAX_OTHER_INTERRUPT);
		if (intr_index == NBL_BOOTIS_MAX_OTHER_INTERRUPT) {
			pr_err("There is no available interrupt left\n");
			ret = -EAGAIN;
			goto get_interrupt_err;
		}
		interrupts[i] = intr_base + intr_index + NBL_BOOTIS_MAX_NET_INTERRUPT;
		set_bit(intr_index, intr_mgt->interrupt_others_bitmap);
	}

	for (i = 0; i < requested; i++) {
		if (i < num_net_msix && net_msix_mask_en)
			msix_mask_en = 1;
		else
			msix_mask_en = 0;
		phy_ops->configure_msix_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), func_id, true,
					     interrupts[i], bus, devid, function, msix_mask_en);
	}

	return 0;

get_interrupt_err:
	bitmap_clear(intr_mgt->interrupt_net_bitmap, 0, NBL_BOOTIS_MAX_NET_INTERRUPT);
	bitmap_clear(intr_mgt->interrupt_others_bitmap, 0, NBL_BOOTIS_MAX_OTHER_INTERRUPT);
	kfree(interrupts);
	func_intr_res->num_interrupts = 0;
	func_intr_res->interrupts = NULL;

alloc_interrupts_err:
	return ret;
}

static u8 *nbl_res_get_msix_irq_enable_info(void *priv, u16 global_vector_id, u32 *irq_data)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_func_interrupt_resource_mng *func_intr_res;
	struct nbl_interrupt_mgt *intr_mgt;
	u16 func_id;
	int intr_base, local_vector_id;

	intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	func_id = nbl_res_vsi_id_to_func_id(res_mgt, common->vsi_id);
	func_intr_res = &intr_mgt->func_intr_res[func_id];
	intr_base = func_intr_res->msix_base;
	local_vector_id = global_vector_id - intr_base;

	if (local_vector_id < 0 || local_vector_id >= NBL_BOOTIS_MAX_NET_INTERRUPT) {
		nbl_err(common, NBL_DEBUG_PHY, "invalid param to get msix irq %u.\n",
			global_vector_id);
		return NULL;
	}

	return phy_ops->get_msix_irq_enable_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), local_vector_id,
						 irq_data);
}

static u16 nbl_res_intr_get_global_vector(void *priv, u16 vsi_id, u16 local_vector_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_interrupt_mgt *intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	u16 global_vector;

	global_vector = intr_mgt->func_intr_res[func_id].interrupts[local_vector_id];

	return global_vector;
}

static u16 nbl_res_intr_get_msix_entry_id(void *priv, u16 vsi_id, u16 local_vector_id)
{
	return local_vector_id;
}

static void nbl_res_intr_get_coalesce(void *priv, u16 func_id, u16 vector_id,
				      struct nbl_chan_param_get_coalesce *ec)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_interrupt_mgt *intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	u16 global_vector_id;
	u16 pnum = 0;
	u16 rate = 0;

	func_id = NBL_RES_MGT_TO_COMMON(res_mgt)->function;
	global_vector_id = intr_mgt->func_intr_res[func_id].interrupts[vector_id];
	phy_ops->get_coalesce(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_vector_id, &pnum, &rate);
	NBL_SET_INTR_COALESCE(ec, rate, pnum, rate, pnum);
}

static void nbl_res_intr_set_coalesce(void *priv, u16 func_id, u16 vector_id,
				      u16 num_net_msix, u16 pnum, u16 rate)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_interrupt_mgt *intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	u16 global_vector_id;
	int i;

	func_id = NBL_RES_MGT_TO_COMMON(res_mgt)->function;
	for (i = 0; i < num_net_msix; i++) {
		global_vector_id = intr_mgt->func_intr_res[func_id].interrupts[vector_id + i];
		phy_ops->set_coalesce(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				      global_vector_id, pnum, rate);
	}
}

static int nbl_res_intr_get_mbx_irq_num(void *priv)
{
	return 0;
}

static int nbl_res_intr_get_adminq_irq_num(void *priv)
{
	return 0;
}

static int nbl_res_intr_get_abnormal_irq_num(void *priv)
{
	return 0;
}

/* NBL_INTR_SET_OPS(ops_name, func)
 *
 * Use X Macros to reduce setup and remove codes.
 */
#define NBL_INTR_OPS_TBL								\
do {											\
	NBL_INTR_SET_OPS(configure_msix_map, nbl_res_intr_configure_msix_map);		\
	NBL_INTR_SET_OPS(destroy_msix_map, nbl_res_intr_destroy_msix_map);		\
	NBL_INTR_SET_OPS(get_msix_irq_enable_info, nbl_res_get_msix_irq_enable_info);	\
	NBL_INTR_SET_OPS(get_global_vector, nbl_res_intr_get_global_vector);		\
	NBL_INTR_SET_OPS(get_msix_entry_id, nbl_res_intr_get_msix_entry_id);		\
	NBL_INTR_SET_OPS(get_coalesce, nbl_res_intr_get_coalesce);			\
	NBL_INTR_SET_OPS(set_coalesce, nbl_res_intr_set_coalesce);			\
	NBL_INTR_SET_OPS(get_mbx_irq_num, nbl_res_intr_get_mbx_irq_num);		\
	NBL_INTR_SET_OPS(get_adminq_irq_num, nbl_res_intr_get_adminq_irq_num);		\
	NBL_INTR_SET_OPS(get_abnormal_irq_num, nbl_res_intr_get_abnormal_irq_num);	\
} while (0)

int nbl_intr_setup_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_INTR_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = func; ; } while (0)
	NBL_INTR_OPS_TBL;
#undef  NBL_INTR_SET_OPS

	return 0;
}

void nbl_intr_remove_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_INTR_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = NULL; ; } while (0)
	NBL_INTR_OPS_TBL;
#undef  NBL_INTR_SET_OPS
}

int nbl_res_intr_init_msix_resource(void *priv)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_interrupt_mgt *intr_mgt = NBL_RES_MGT_TO_INTR_MGT(res_mgt);
	u16 func_id = NBL_RES_MGT_TO_COMMON(res_mgt)->function;
	struct nbl_func_interrupt_resource_mng *func_intr_res = &intr_mgt->func_intr_res[func_id];

	phy_ops->get_msix_resource(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), func_id,
				   &func_intr_res->msix_base, &func_intr_res->msix_max);
	if (func_intr_res->msix_max == 0)
		func_intr_res->msix_max = NBL_BOOTIS_MAX_INTERRUPT_PER_PF;

	return 0;
}
