// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_queue_bootis.h"

static int nbl_res_queue_setup_queue_info(struct nbl_resource_mgt *res_mgt, u16 func_id,
					  u16 num_queues)
{
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	u16 *txrx_queues, *queues_context;
	u16 queue_index, vsi_id;
	int ret = 0;

	nbl_info(common, NBL_DEBUG_QUEUE,
		 "Setup qinfo, func_id:%d, num_queues:%d", func_id, num_queues);

	vsi_id = nbl_res_func_id_to_vsi_id(res_mgt, func_id, NBL_VSI_DATA);
	switch (vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		queue_info->qid_map_index = NBL_BOOTIS_ECPU_PF2_NOTIFY_INDEX;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		queue_info->qid_map_index = NBL_BOOTIS_ECPU_PF3_NOTIFY_INDEX;
		break;
	default:
		nbl_err(common, NBL_DEBUG_QUEUE, "ilegal vsi_id id:%u.\n", vsi_id);
		return -ENODEV;
	}

	queue_info->rss_ret_base = vsi_id;

	txrx_queues = kcalloc(num_queues, sizeof(txrx_queues[0]), GFP_ATOMIC);
	if (!txrx_queues) {
		nbl_err(common, NBL_DEBUG_QUEUE, "Allocate function txrx_queues array failed\n");
		ret = -ENOMEM;
		goto alloc_txrx_queues_fail;
	}

	queues_context = kcalloc(num_queues * 2, sizeof(txrx_queues[0]), GFP_ATOMIC);
	if (!queues_context) {
		nbl_err(common, NBL_DEBUG_QUEUE, "Allocate function queues_context array failed\n");
		ret = -ENOMEM;
		goto alloc_queue_contex_fail;
	}

	queue_info->num_txrx_queues = num_queues;
	queue_info->txrx_queues = txrx_queues;
	queue_info->queues_context = queues_context;

	for (queue_index = 0; queue_index < num_queues; queue_index++)
		txrx_queues[queue_index] = NBL_RES_BASE_QID(res_mgt) + queue_index;

	return 0;

alloc_queue_contex_fail:
	kfree(txrx_queues);
alloc_txrx_queues_fail:
	return ret;
}

static void nbl_res_queue_remove_queue_info(struct nbl_resource_mgt *res_mgt, u16 func_id)
{
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];

	kfree(queue_info->txrx_queues);
	kfree(queue_info->queues_context);
	queue_info->txrx_queues = NULL;
	queue_info->queues_context = NULL;

	queue_info->num_txrx_queues = 0;
}

static int nbl_res_queue_setup_qid_map_table_leonis(struct nbl_resource_mgt *res_mgt, u16 func_id,
						    u64 notify_addr)
{
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_ecpu_qid_map_param qid_map = {0};

	/* Get base location */
	queue_info->notify_addr = notify_addr;

	qid_map.valid = 1;
	qid_map.table_id = queue_info->qid_map_index;
	qid_map.max_qid = NBL_BOOTIS_MAX_QID;
	qid_map.base_qid = 0;
	qid_map.device_type = 0;
	qid_map.notify_addr = notify_addr;

	phy_ops->set_qid_map_table(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), &qid_map,
				   queue_mgt->qid_map_select);

	return 0;
}

static void nbl_res_queue_remove_qid_map_table_leonis(struct nbl_resource_mgt *res_mgt, u16 func_id)
{
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_ecpu_qid_map_param qid_map = {0};

	qid_map.valid = 0;
	qid_map.table_id = queue_info->qid_map_index;

	phy_ops->set_qid_map_table(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), &qid_map,
				   queue_mgt->qid_map_select);
}

static void nbl_res_queue_setup_queue_cfg(struct nbl_queue_mgt *queue_mgt,
					  struct nbl_queue_cfg_param *cfg_param,
					  struct nbl_txrx_queue_param *queue_param,
					  bool is_tx, u16 func_id)
{
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];

	cfg_param->desc = queue_param->dma;
	cfg_param->size = queue_param->desc_num;
	cfg_param->global_vector = queue_param->global_vector_id;
	cfg_param->global_queue_id = queue_info->txrx_queues[queue_param->local_queue_id];

	cfg_param->avail = 0;
	cfg_param->used = 0;
	cfg_param->extend_header = 1;
	cfg_param->split = 0;
	cfg_param->last_avail_idx = 0;

	cfg_param->intr_en = queue_param->intr_en;
	cfg_param->intr_mask = queue_param->intr_mask;

	cfg_param->tx = is_tx;
	cfg_param->rxcsum = 0;
	cfg_param->half_offload_en = queue_param->half_offload_en;
}

static void nbl_res_queue_setup_hw_dq(struct nbl_resource_mgt *res_mgt,
				      struct nbl_queue_cfg_param *queue_cfg, u16 func_id)
{
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_vnet_queue_info_param param = {0};
	u16 global_queue_id = queue_cfg->global_queue_id;
	u16 ecpu_local_id = NBL_BOOTIS_ECPU_LOCAL_QUEUE(global_queue_id);
	u8 bus, dev, func;

	nbl_res_func_id_to_bdf(res_mgt, func_id, &bus, &dev, &func);
	queue_info->split = queue_cfg->split;
	queue_info->queue_size = queue_cfg->size;

	param.function_id = func;
	param.device_id = dev;
	param.bus_id = bus;
	param.valid = 1;

	if (queue_cfg->intr_en) {
		param.msix_idx = queue_cfg->global_vector;
		param.msix_idx_valid = 1;
	}

	if (queue_cfg->tx) {
		phy_ops->set_vnet_queue_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), &param,
					     NBL_PAIR_ID_GET_TX(ecpu_local_id));
		phy_ops->reset_dvn_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue_id);

		phy_ops->cfg_tx_queue(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				      queue_cfg, global_queue_id);

	} else {
		phy_ops->set_vnet_queue_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), &param,
					     NBL_PAIR_ID_GET_RX(ecpu_local_id));
		phy_ops->reset_uvn_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue_id);

		phy_ops->cfg_rx_queue(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), queue_cfg,
				      global_queue_id);
	}
}

static void nbl_res_queue_remove_all_hw_dq(struct nbl_resource_mgt *res_mgt, u16 func_id)
{
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 global_queue, count = queue_info->num_txrx_queues;
	u16 local_queue;
	int i;

	for (i = 0; i < count; i++) {
		global_queue = queue_info->txrx_queues[i];

		phy_ops->disable_dvn(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue);
	}

	for (i = 0; i < count; i++) {
		global_queue = queue_info->txrx_queues[i];

		phy_ops->disable_uvn(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue);
	}

	for (i = 0; i < count; i++) {
		global_queue = queue_info->txrx_queues[i];
		phy_ops->reset_uvn_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue);
		phy_ops->reset_dvn_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), global_queue);
	}

	for (i = 0; i < count; i++) {
		global_queue = queue_info->txrx_queues[i];
		local_queue = NBL_BOOTIS_ECPU_LOCAL_QUEUE(global_queue);
		phy_ops->clear_vnet_queue_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					       NBL_PAIR_ID_GET_RX(local_queue));
		phy_ops->clear_vnet_queue_info(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					       NBL_PAIR_ID_GET_TX(local_queue));
	}
}

static int nbl_res_queue_cfg_rss(struct nbl_resource_mgt *res_mgt,
				 struct nbl_phy_ops *phy_ops,
				 u16 vsi)
{
	struct nbl_rss_alg_param rss_alg_param = {0};

	rss_alg_param.hash_field_type_v4 = NBL_RSS_HASH_FIELD_TYPE_TCPUDP_OVER_IP;
	rss_alg_param.hash_field_type_v6 = NBL_RSS_HASH_FIELD_TYPE_TCPUDP_OVER_IP;
	rss_alg_param.hash_alg_type = NBL_RSS_HASH_FUNC_TOEPLITZ;

	phy_ops->cfg_rss_alg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), vsi, &rss_alg_param);

	return 0;
}

static int nbl_res_queue_alloc_txrx_queues(void *priv, u16 vsi_id, u16 queue_num)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u64 notify_addr = 0;
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	int ret = 0;

	ret = nbl_res_queue_setup_queue_info(res_mgt, func_id, queue_num);
	if (ret)
		goto setup_queue_info_fail;

	ret = nbl_res_queue_setup_qid_map_table_leonis(res_mgt, func_id, notify_addr);
	if (ret)
		goto setup_qid_map_fail;

	return 0;

setup_qid_map_fail:
	nbl_res_queue_remove_queue_info(res_mgt, func_id);
setup_queue_info_fail:
	return ret;
}

static void nbl_res_queue_free_txrx_queues(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);

	nbl_res_queue_remove_qid_map_table_leonis(res_mgt, func_id);
	nbl_res_queue_remove_queue_info(res_mgt, func_id);
}

static int nbl_res_queue_setup_queue(void *priv, struct nbl_txrx_queue_param *param, bool is_tx)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_queue_cfg_param cfg_param = {0};
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, param->vsi_id);

	nbl_res_queue_setup_queue_cfg(NBL_RES_MGT_TO_QUEUE_MGT(res_mgt),
				      &cfg_param, param, is_tx, func_id);

	nbl_res_queue_setup_hw_dq(res_mgt, &cfg_param, func_id);
	return 0;
}

static void nbl_res_queue_remove_all_queues(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);

	nbl_res_queue_remove_all_hw_dq(res_mgt, func_id);
}

static u16 nbl_res_queue_func_id_to_group_id(void *p, u16 func_id)
{
	u16 group_id = 0;

	switch (func_id) {
	case NBL_BOOTIS_ECPU_ETH0_FUNCTION:
		group_id = NBL_BOOTIS_ECPU_ETH0_GROUP_ID;
		break;
	case NBL_BOOTIS_ECPU_ETH1_FUNCTION:
		group_id = NBL_BOOTIS_ECPU_ETH1_GROUP_ID;
		break;
	default:
		pr_err("ilegal function:%u.\n", func_id);
		break;
	}
	return group_id;
}

static int nbl_res_queue_cfg_dsch(void *priv, u16 vsi_id, bool vld)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u16 group_id = nbl_res_queue_func_id_to_group_id(res_mgt, func_id);
	int i, ret;

	for (i = 0; i < queue_info->num_txrx_queues; i++) {
		phy_ops->cfg_q2tc_netid(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					queue_info->txrx_queues[i], vsi_id, vld);
	}
	if (vld)
		ret = phy_ops->cfg_dsch_group_to_port(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
						      group_id, common->eth_id, vld);

	return phy_ops->cfg_dsch_net_to_group(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					      vsi_id, group_id, vld);
}

static int nbl_res_queue_setup_cqs(void *priv, u16 vsi_id, u16 real_qps)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];
	u16 rss_queue_num;

	if (real_qps == queue_info->curr_qps)
		return 0;

	if (real_qps) {
		nbl_res_queue_cfg_rss(res_mgt, phy_ops, vsi_id);
		rss_queue_num = (real_qps >= queue_info->num_txrx_queues) ?
				(queue_info->num_txrx_queues - 1) : real_qps;
		phy_ops->cfg_epro_rss_ret(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					  queue_info->rss_ret_base,
					  queue_info->rss_entry_size, rss_queue_num,
					  queue_info->txrx_queues);
	}

	phy_ops->cfg_epro_vpt_tbl(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), vsi_id);

	queue_info->curr_qps = real_qps;
	return 0;
}

static void nbl_res_queue_remove_cqs(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u16 func_id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_queue_info *queue_info = &queue_mgt->queue_info[func_id];

	queue_info->curr_qps = 0;
}

static int nbl_res_queue_init(void *priv)
{
	return 0;
}

static int nbl_res_get_queue_err_stats(void *priv, u16 func_id, u8 queue_id,
				       struct nbl_queue_err_stats *queue_err_stats,
				       bool is_tx)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_queue_mgt *queue_mgt = NBL_RES_MGT_TO_QUEUE_MGT(res_mgt);
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_queue_info *queue_info;
	struct nbl_phy_ops *phy_ops;
	u16 global_queue_id;

	func_id = nbl_res_vsi_id_to_func_id(res_mgt, common->vsi_id);
	queue_info = &queue_mgt->queue_info[func_id];

	if (queue_id >= queue_info->num_txrx_queues)
		return -EINVAL;

	phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	global_queue_id = queue_info->txrx_queues[queue_id];

	if (is_tx)
		phy_ops->get_tx_queue_err_stats(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
						global_queue_id, queue_err_stats);
	else
		phy_ops->get_rx_queue_err_stats(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
						global_queue_id, queue_err_stats);

	return 0;
}

/* NBL_QUEUE_SET_OPS(ops_name, func)
 *
 * Use X Macros to reduce setup and remove codes.
 */
#define NBL_QUEUE_OPS_TBL									\
do {												\
	NBL_QUEUE_SET_OPS(alloc_txrx_queues, nbl_res_queue_alloc_txrx_queues);			\
	NBL_QUEUE_SET_OPS(free_txrx_queues, nbl_res_queue_free_txrx_queues);			\
	NBL_QUEUE_SET_OPS(setup_queue, nbl_res_queue_setup_queue);				\
	NBL_QUEUE_SET_OPS(remove_all_queues, nbl_res_queue_remove_all_queues);			\
	NBL_QUEUE_SET_OPS(cfg_dsch, nbl_res_queue_cfg_dsch);					\
	NBL_QUEUE_SET_OPS(setup_cqs, nbl_res_queue_setup_cqs);					\
	NBL_QUEUE_SET_OPS(remove_cqs, nbl_res_queue_remove_cqs);				\
	NBL_QUEUE_SET_OPS(queue_init, nbl_res_queue_init);					\
	NBL_QUEUE_SET_OPS(get_queue_err_stats, nbl_res_get_queue_err_stats);			\
	NBL_QUEUE_SET_OPS(cfg_qdisc_mqprio, NULL);						\
} while (0)

int nbl_queue_setup_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_QUEUE_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = func; ; } while (0)
	NBL_QUEUE_OPS_TBL;
#undef  NBL_QUEUE_SET_OPS

	return 0;
}

void nbl_queue_remove_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_QUEUE_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = NULL; ; } while (0)
	NBL_QUEUE_OPS_TBL;
#undef  NBL_QUEUE_SET_OPS
}

void nbl_queue_mgt_init_bootis(struct nbl_queue_mgt *queue_mgt)
{
	//TODO
}
