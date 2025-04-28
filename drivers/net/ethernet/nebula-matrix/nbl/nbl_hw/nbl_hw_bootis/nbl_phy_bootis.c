// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_phy_bootis.h"

static u8 __iomem *nbl_phy_get_hw_addr(void *priv, size_t *size)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;

	if (size)
		*size = (size_t)phy_mgt->hw_size;
	return phy_mgt->hw_addr;
}

static int nbl_phy_fltr_proc_result(struct nbl_phy_mgt *phy_mgt, u32 opt)
{
	int ret = NBL_FAIL;
	struct nbl_common_info *common;
	union nbl_memt_status_op result;
	int i = NBL_MV_GET_RESULT_TIMES;

	common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	while (i--) {
		nbl_hw_read_regs(phy_mgt,
				 NBL_MEMT_STATUS_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
				 result.data, CPU_MEMT_STATUS_SIZE);
		nbl_debug(common, NBL_DEBUG_FLOW, "op status, type %01x, success %01x, done %01x, errtype %01x.\n",
			  result.info.memt_op_type,
			  result.info.memt_op_success,
			  result.info.memt_op_done,
			  result.info.memt_op_errtype);

		if (result.info.memt_op_done) {
			if (result.info.memt_op_success || opt == NBL_MEMT_OP_TYPE_DEL)
				ret = NBL_OK;
			else
				ret = NBL_FAIL;
			break;
		}

		/* delay 10-20us read once until success */
		usleep_range(10, 20);
	}

	return ret;
}

static int nbl_phy_add_mv_tbl(void *priv, u16 vsi, const void *key, const void *act, u16 result_idx)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	const union nbl_cpu_memt_key_reg *memt_key = key;
	union nbl_memt_op mem_operation;
	const union nbl_mac_result_reg *action = act;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);

	memset(&mem_operation, 0, sizeof(mem_operation));

	nbl_hw_write_regs(phy_mgt, NBL_MEMT_KEY_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
			  memt_key->data, CPU_MEMT_KEY_REG_SIZE);

	nbl_debug(common, NBL_DEBUG_PHY, "add mac-vlan tbl, key: 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x.\n",
		  memt_key->data[7], memt_key->data[6], memt_key->data[5], memt_key->data[4],
		  memt_key->data[3], memt_key->data[2], memt_key->data[1], memt_key->data[0]);

	/* result idx & result action */
	nbl_hw_wr32(phy_mgt, NBL_MEMT_RESULT_TBL_IDX_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
		    result_idx);
	nbl_hw_write_regs(phy_mgt, NBL_MEMT_RESULT_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
			  action->data, MAC_RESULT_TBL_SIZE);

	nbl_debug(common, NBL_DEBUG_PHY, "add mac-vlan tbl, result_idx: %u, act: 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x.\n",
		  result_idx,
		  action->data[7], action->data[6], action->data[5], action->data[4],
		  action->data[3], action->data[2], action->data[1], action->data[0]);

	/* start | optype */
	mem_operation.info.memt_op_type = NBL_MEMT_OP_TYPE_ADD;
	mem_operation.info.memt_op_start = 1;
	nbl_hw_wr32(phy_mgt, NBL_MEMT_OP_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
		    *(u32 *)mem_operation.data);

	return nbl_phy_fltr_proc_result(phy_mgt, mem_operation.info.memt_op_type);
}

static int nbl_phy_del_mv_tbl(void *priv, const void *key)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	const union nbl_cpu_memt_key_reg *memt_key = key;
	union nbl_memt_op mem_operation;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);

	memset(&mem_operation, 0, sizeof(mem_operation));

	nbl_hw_write_regs(phy_mgt, NBL_MEMT_KEY_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
			  memt_key->data, CPU_MEMT_KEY_REG_SIZE);

	nbl_debug(common, NBL_DEBUG_PHY, "del mac-vlan tbl, key: 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x.\n",
		  memt_key->data[7], memt_key->data[6], memt_key->data[5], memt_key->data[4],
		  memt_key->data[3], memt_key->data[2], memt_key->data[1], memt_key->data[0]);

	/* start | optype */
	mem_operation.info.memt_op_type = NBL_MEMT_OP_TYPE_DEL;
	mem_operation.info.memt_op_start = 1;
	nbl_hw_wr32(phy_mgt, NBL_MEMT_OP_REG(NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id),
		    *(u32 *)mem_operation.data);

	return nbl_phy_fltr_proc_result(phy_mgt, mem_operation.info.memt_op_type);
}

static int nbl_phy_set_qid_map_table(void *priv, void *data, int qid_map_select)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_ecpu_qid_map_param *param = (struct nbl_ecpu_qid_map_param *)data;
	union nbl_ecpu_qid_map_table qid_map;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	struct pci_dev *pdev;
	u64 notify_addr;

	memset(&qid_map, 0, sizeof(qid_map));

	pdev = NBL_COMMON_TO_PDEV(common);
	notify_addr = pci_resource_start(pdev, NBL_MEMORY_BAR) + phy_mgt->notify_offset;

	qid_map.info.max_qid = param->max_qid;
	qid_map.info.base_qid = param->base_qid;
	qid_map.info.device_type = param->device_type;
	qid_map.info.notify_addr_l = notify_addr & 0xffff;
	qid_map.info.notify_addr_h = (notify_addr >> 16) & 0xffffffff;
	qid_map.info.valid = param->valid ? 1 : 0;

	nbl_hw_write_regs(phy_mgt, NBL_NOTIFY_CONFIGURE_TABLE(param->table_id),
			  qid_map.data, NBL_ECPU_QID_MAP_TBL_SIZE);

	return 0;
}

static int nbl_phy_cfg_rss_alg(void *priv, u16 vsi, const void *param)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	const struct nbl_rss_alg_param *rss_param = (struct nbl_rss_alg_param *)param;
	union nbl_uflow_rss_reg old_rss_reg, uflow_rss_reg;
	u8 vsi_hash_func_table, vsi_hash_func_bit;
	union nbl_uflow_rss_hash_func hash_func_reg;

	uflow_rss_reg.info.hash_field_type_v4 = rss_param->hash_field_type_v4;
	uflow_rss_reg.info.hash_field_type_v6 = rss_param->hash_field_type_v6;
	uflow_rss_reg.info.hash_field_mask_dport = rss_param->hash_field_mask_dport;
	uflow_rss_reg.info.hash_field_mask_sport = rss_param->hash_field_mask_sport;
	uflow_rss_reg.info.hash_field_mask_dip = rss_param->hash_field_mask_dip;
	uflow_rss_reg.info.hash_field_mask_sip = rss_param->hash_field_mask_sip;

	nbl_hw_read_regs(phy_mgt, NBL_UFLOW_RSS_REG,
			 old_rss_reg.data, NBL_UFLOW_RSS_REG_SIZE);

	if (*((u32 *)old_rss_reg.data) != *((u32 *)uflow_rss_reg.data))
		nbl_hw_write_regs(phy_mgt, NBL_UFLOW_RSS_REG,
				  uflow_rss_reg.data, NBL_UFLOW_RSS_REG_SIZE);

	vsi_hash_func_table = vsi / (NBL_UFLOW_RSS_HASH_FUNC_SIZE * 8);
	vsi_hash_func_bit = vsi % (NBL_UFLOW_RSS_HASH_FUNC_SIZE * 8);

	nbl_hw_read_regs(phy_mgt, NBL_UFLOW_RSS_HASH_FUNC_REG(vsi_hash_func_table),
			 hash_func_reg.data, NBL_UFLOW_RSS_HASH_FUNC_SIZE);

	if (rss_param->hash_alg_type)
		hash_func_reg.info.vsi_hash_func |= BIT(vsi_hash_func_bit);
	else
		hash_func_reg.info.vsi_hash_func &= ~BIT(vsi_hash_func_bit);

	nbl_hw_write_regs(phy_mgt, NBL_UFLOW_RSS_HASH_FUNC_REG(vsi_hash_func_table),
			  hash_func_reg.data, NBL_UFLOW_RSS_HASH_FUNC_SIZE);

	return 0;
}

static void epro_rss_group_table_cfg(struct nbl_phy_mgt *phy_mgt, u32 table_id,
				     const union nbl_uflow_rss_group_table *cfg)
{
	union nbl_uflow_rss_group_ctrl_reg group_ctrl_reg;
	u8 func = NBL_PHY_MGT_TO_COMMON(phy_mgt)->function;

	nbl_hw_write_regs(phy_mgt, NBL_UEPRO_RSS_GROUP_TABLE_WDATA_REG(func),
			  cfg->data, NBL_UFLOW_RSS_GROUP_TABLE_SIZE);

	group_ctrl_reg.info.rss_group_table_addr = table_id;
	group_ctrl_reg.info.rss_group_table_rw = NBL_UEPRO_RSS_GROUP_TABLE_CTRL_WRITE;
	group_ctrl_reg.info.rss_group_table_start = 1;
	nbl_hw_write_regs(phy_mgt, NBL_UEPRO_RSS_GROUP_TABLE_CTRL_REG(func),
			  group_ctrl_reg.data, NBL_UFLOW_RSS_GROUP_CTRL_REG_SIZE);
}

static int nbl_phy_cfg_epro_rss(void *priv, u32 index, u8 size_type, u32 q_num, u16 *queue_list)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_uflow_rss_group_table group_table;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	u32 table_id, q_index, group_id;
	u32 table_start, table_end;

	if (!q_num) {
		nbl_err(common, NBL_DEBUG_PHY, "invalid queue num %u for cfg rss group table.\n",
			q_num);
		return -1;
	}

	group_id = index;
	table_start = NBL_UEPRO_RSS_TABLES_PER_GROUP * group_id;
	table_end = NBL_UEPRO_RSS_TABLES_PER_GROUP * (group_id + 1);
	q_index = 0;

	for (table_id = table_start; table_id < table_end; table_id++) {
		group_table.info.rss_que_index0 = queue_list[(q_index++ % q_num)];
		group_table.info.rss_que_index1 = queue_list[(q_index++ % q_num)];
		group_table.info.rss_que_index2 = queue_list[(q_index++ % q_num)];
		group_table.info.rss_que_index3 = queue_list[(q_index++ % q_num)];

		epro_rss_group_table_cfg(phy_mgt, table_id, &group_table);
	}

	return 0;
}

static int nbl_phy_cfg_epro_vsi_tbl(void *priv, u16 vsi_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_uepro_vsi_tbl epro_vsi_tbl;

	nbl_hw_read_regs(phy_mgt, NBL_UEPRO_VSI_TABLE(vsi_id),
			 epro_vsi_tbl.data,
			 NBL_UEPRO_VSI_TBL_SIZE);
	epro_vsi_tbl.info.fwd = NBL_UEPRO_FWD_TYPE_NORMAL;
	nbl_hw_write_regs(phy_mgt, NBL_UEPRO_VSI_TABLE(vsi_id),
			  epro_vsi_tbl.data,
			  NBL_UEPRO_VSI_TBL_SIZE);

	return 0;
}

static void nbl_phy_cfg_padpt_txrx_enable(void *priv, bool tx_enable, bool rx_enable)
{
	u32 value32 = 0;
	bool tx_enabled;
	bool rx_enabled;
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;

	/* configure the tx and rx enable */
	nbl_hw_read_regs(phy_mgt, PADAPT_ECPU_TX_ENABLE, (u8 *)&value32, sizeof(value32));
	tx_enabled = !!(value32 & BIT(0));
	if (tx_enabled != tx_enable) {
		value32 = tx_enable ? BIT(0) : 0;
		nbl_hw_write_regs(phy_mgt, PADAPT_ECPU_TX_ENABLE, (u8 *)&value32, sizeof(value32));
	}
	nbl_hw_read_regs(phy_mgt, PADAPT_ECPU_RX_ENABLE, (u8 *)&value32, sizeof(value32));
	rx_enabled = !!(value32 & BIT(0));
	if (rx_enabled != rx_enable) {
		value32 = rx_enable ? BIT(0) : 0;
		nbl_hw_write_regs(phy_mgt, PADAPT_ECPU_RX_ENABLE, (u8 *)&value32, sizeof(value32));
	}
}

static int nbl_phy_reset_tx_queue(void *priv, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	u32 value32;

	/* queue id for reset */
	value32 = queue_id;
	nbl_hw_write_regs(phy_mgt, NBL_DVN_ECPU_QUEUE_RESET_TABLE, (u8 *)&value32, sizeof(value32));
	nbl_debug(common, NBL_DEBUG_QUEUE, "reset dvn queue %d success.\n", value32);
	return 0;
}

static int nbl_phy_reset_rx_queue(void *priv, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	u32 value32;

	/* queue id for reset */
	value32 = queue_id;
	nbl_hw_write_regs(phy_mgt, NBL_UVN_ECPU_QUEUE_RESET_TABLE, (u8 *)&value32, sizeof(value32));
	nbl_debug(common, NBL_DEBUG_QUEUE, "reset uvn queue %d success.\n", value32);
	return 0;
}

static int nbl_phy_cfg_tx_queue(void *priv, void *data, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_queue_cfg_param *queue_cfg = (struct nbl_queue_cfg_param *)data;
	union nbl_dvn_queue_table dvn_qinfo;

	memset(&dvn_qinfo, 0, sizeof(dvn_qinfo));

	dvn_qinfo.info.queue_baddr = queue_cfg->desc;
	if (!queue_cfg->split && !queue_cfg->extend_header)
		queue_cfg->avail = queue_cfg->avail | 3;
	dvn_qinfo.info.avail_baddr = queue_cfg->avail;
	dvn_qinfo.info.used_baddr = queue_cfg->used;
	dvn_qinfo.info.queue_size_mask_pow = ilog2(queue_cfg->size);
	dvn_qinfo.info.queue_type = queue_cfg->split;
	dvn_qinfo.info.extend_header_en = queue_cfg->extend_header;
	dvn_qinfo.info.queue_enable = 1;

	nbl_hw_write_regs(phy_mgt, NBL_DVN_ECPU_QUEUE_TABLE(queue_id),
			  dvn_qinfo.data, NBL_DVN_QUEUE_TABLE_SIZE);

	return 0;
}

static int nbl_phy_cfg_rx_queue(void *priv, void *data, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	struct nbl_queue_cfg_param *queue_cfg = (struct nbl_queue_cfg_param *)data;
	union nbl_uvn_queue_table uvn_qinfo;

	uvn_qinfo.info.queue_baddr = queue_cfg->desc;
	uvn_qinfo.info.avail_baddr = queue_cfg->avail;
	uvn_qinfo.info.used_baddr = queue_cfg->used;
	uvn_qinfo.info.queue_size_mask_pow = ilog2(queue_cfg->size);
	uvn_qinfo.info.queue_type = queue_cfg->split;
	uvn_qinfo.info.extend_header_en = queue_cfg->extend_header;
	uvn_qinfo.info.guest_csum_en = queue_cfg->rxcsum;
	uvn_qinfo.info.queue_enable = 1;

	nbl_hw_write_regs(phy_mgt, NBL_UVN_ECPU_QUEUE_TABLE(queue_id),
			  uvn_qinfo.data, NBL_UVN_QUEUE_TABLE_SIZE);

	nbl_hw_read_regs(phy_mgt, NBL_UVN_ECPU_QUEUE_TABLE(queue_id),
			 uvn_qinfo.data, NBL_UVN_QUEUE_TABLE_SIZE);
	if (!uvn_qinfo.info.queue_enable) {
		nbl_err(common, NBL_DEBUG_QUEUE, "enable uvn queue %d failed.\n", queue_id);
		return -1;
	}

	return 0;
}

static int nbl_phy_disable_tx(void *priv, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dvn_queue_table dvn_qinfo;

	memset(&dvn_qinfo, 0, sizeof(dvn_qinfo));

	nbl_hw_write_regs(phy_mgt, NBL_DVN_ECPU_QUEUE_TABLE(queue_id),
			  dvn_qinfo.data, NBL_DVN_QUEUE_TABLE_SIZE);
	return 0;
}

static int nbl_phy_disable_rx(void *priv, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_uvn_queue_table uvn_qinfo;

	memset(&uvn_qinfo, 0, sizeof(uvn_qinfo));

	nbl_hw_write_regs(phy_mgt, NBL_UVN_ECPU_QUEUE_TABLE(queue_id),
			  uvn_qinfo.data, NBL_UVN_QUEUE_TABLE_SIZE);
	return 0;
}

static void nbl_phy_update_tail_ptr(void *priv, struct nbl_notify_param *param)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u8 __iomem *notify_addr = phy_mgt->hw_addr + phy_mgt->notify_offset;
	u32 global_qid = param->notify_qid;
	u32 tail_ptr = param->tail_ptr;

	writel((((u32)tail_ptr << 16) | (u32)global_qid), notify_addr);
}

static u8 *nbl_phy_get_tail_ptr(void *priv)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;

	return phy_mgt->hw_addr + phy_mgt->notify_offset;
}

static int nbl_phy_cfg_q2tc_netid(void *priv, u16 queue_id, u16 netid, u16 vld)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dsch_vn_q2tc_tbl q2tc_tbl;

	memset(&q2tc_tbl, 0, sizeof(q2tc_tbl));

	if (vld) {
		q2tc_tbl.info.tc_id = netid << 3;
		q2tc_tbl.info.vld = 1;
	}

	nbl_hw_write_regs(phy_mgt, NBL_DSCH_VN_Q2TC_TBL(queue_id),
			  q2tc_tbl.data, NBL_DSCH_VN_Q2TC_TBL_SIZE);
	return 0;
}

static int nbl_phy_set_tc_wgt(void *priv, u16 func_id, u8 *weight, u16 num_tc)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dsch_vn_tc_wgt_tbl wgt_cfg;
	int i;

	memset(&wgt_cfg, 0, sizeof(wgt_cfg));

	for (i = 0; i < num_tc; i++)
		wgt_cfg.data[i] = weight[i];
	nbl_hw_write_regs(phy_mgt, NBL_DSCH_VN_TC_WGT_TBL(func_id),
			  wgt_cfg.data, NBL_DSCH_VN_TC_WGT_TBL_SIZE);

	return 0;
}

static int nbl_phy_set_tc_spwrr(void *priv, u16 func_id, u8 spwrr)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dsch_vn_tc_spwrr_tbl spwrr_tbl;

	spwrr_tbl.info.tc_sprr = spwrr;
	nbl_hw_write_regs(phy_mgt, NBL_DSCH_VN_TC_SPWRR_TBL(func_id),
			  spwrr_tbl.data, NBL_DSCH_VN_TC_SPWRR_TBL_SIZE);

	return 0;
}

static int nbl_phy_cfg_dsch_net_to_group(void *priv, u16 func_id, u16 group_id, u16 vld)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dsch_vn_n2g_tbl n2g_tbl;

	n2g_tbl.info.grpid = group_id;
	n2g_tbl.info.vld = vld;
	nbl_hw_write_regs(phy_mgt, NBL_DSCH_VN_N2G_TBL(func_id),
			  n2g_tbl.data, NBL_DSCH_VN_N2G_TBL_SIZE);
	return 0;
}

static int nbl_phy_cfg_group_to_port(void *priv, u16 group_id, u16 dport, u16 vld)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dsch_vn_g2p_tbl g2p_tbl;

	g2p_tbl.info.sport = NBL_DSCH_N2G_SPORT_ECPU;
	g2p_tbl.info.dport = dport;
	g2p_tbl.info.vld = vld;
	nbl_hw_write_regs(phy_mgt, NBL_DSCH_VN_G2P_TBL(group_id),
			  g2p_tbl.data, NBL_DSCH_VN_G2P_TBL_SIZE);
	return 0;
}

static void nbl_phy_configure_msix_map(void *priv, u16 func_id, bool valid,
				       dma_addr_t dma_addr, u8 bus, u8 devid, u8 function)
{
	//TODO
}

static void nbl_phy_configure_msix_info(void *priv, u16 func_id, bool valid, u16 interrupt_id,
					u8 bus, u8 devid, u8 function, bool msix_mask_en)
{
	union nbl_ecpu_msix_info_tbl msix_info;

	memset(&msix_info, 0, sizeof(msix_info));

	if (valid) {
		msix_info.info.intrl_pnum = 0;
		msix_info.info.intrl_rate = 0;
		msix_info.info.function = function;
		msix_info.info.devid = devid;
		msix_info.info.bus = bus;
		msix_info.info.valid = 1;
		msix_info.info.mask_en = msix_mask_en;
	}

	nbl_hw_write_regs(priv, NBL_ECPU_MSIX_INFO_TBL(interrupt_id),
			  msix_info.data, NBL_ECPU_MSIX_INFO_TBL_SIZE);
}

static u8 *nbl_phy_get_msix_irq_enable_info(void *priv, u16 local_vector_id, u32 *irq_data)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;

	*irq_data = 0;

	return (u8 *)phy_mgt->hw_addr + NBL_ECPU_MSIX_TBL(local_vector_id)
		+ NBL_ECPU_MSIX_TBL_MASK_OFFSET;
}

static void nbl_phy_enable_msix_irq(void *priv, u16 local_vector_id)
{
	union nbl_ecpu_msix_tbl msix_ctrl;

	memset(&msix_ctrl, 0, sizeof(msix_ctrl));

	nbl_hw_wr32(priv, NBL_ECPU_MSIX_TBL(local_vector_id) + NBL_ECPU_MSIX_TBL_MASK_OFFSET,
		    *((u32 *)&msix_ctrl.data[NBL_ECPU_MSIX_TBL_MASK_OFFSET]));
}

static void nbl_phy_get_msix_resource(void *priv, u16 func_id, u16 *msix_base, u16 *msix_max)
{
	u32 value;

	*msix_base = (u16)nbl_hw_rd32(priv, NBL_MSIX_BASE_IDX(func_id));
	value = nbl_hw_rd32(priv, NBL_MSIX_BASE_IDX(func_id + 1));
	*msix_max = (value - *msix_base) ? (value - *msix_base) : 0;
}

static int nbl_phy_set_vnet_queue_info(void *priv, struct nbl_vnet_queue_info_param *param,
				       u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_ecpu_vnet_map_tbl vnet_map;

	vnet_map.info.function = param->function_id;
	vnet_map.info.devid = param->device_id;
	vnet_map.info.bus = param->bus_id;
	vnet_map.info.valid = param->valid;
	vnet_map.info.msix_idx = param->msix_idx;
	vnet_map.info.msix_idx_valid = param->msix_idx_valid;

	nbl_hw_write_regs(phy_mgt, NBL_ECPU_VNET_MAP_TBL(queue_id),
			  vnet_map.data, NBL_ECPU_VNET_MAP_TBL_SIZE);

	return 0;
}

static int nbl_phy_clear_vnet_queue_info(void *priv, u16 queue_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_ecpu_vnet_map_tbl vnet_map;

	memset(&vnet_map, 0, sizeof(vnet_map));

	nbl_hw_write_regs(phy_mgt, NBL_ECPU_VNET_MAP_TBL(queue_id),
			  vnet_map.data, NBL_ECPU_VNET_MAP_TBL_SIZE);
	return 0;
}

static int nbl_phy_set_spoof_check_addr(void *priv, u16 vsi_id, u8 *mac)
{
	return 0;
}

static int nbl_phy_set_spoof_check_enable(void *priv, u16 vsi_id, u8 enable)
{
	return 0;
}

static void cfg_upa_pcmrt_tbl(struct nbl_phy_mgt *phy_mgt, u16 tbl_id,
			      u8 *key, u8 *mask, u8 *action, u8 *action_0, u8 *action_1)
{
	nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_KEY_TABLE(tbl_id),
			  key, NBL_PA_PCMRT_KEY_TBL_SIZE);
	nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_MASK_TABLE(tbl_id),
			  mask, NBL_PA_PCMRT_MASK_TBL_SIZE);
	if (action)
		nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_ACTION_TABLE(tbl_id),
				  action, NBL_PA_PCMRT_ACTION_TBL_SIZE);
	if (action_0)
		nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_ETH_ACTION_TABLE(0, tbl_id),
				  action_0, NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE);
	if (action_1)
		nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_ETH_ACTION_TABLE(1, tbl_id),
				  action_1, NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE);

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg upa pcmrt table %u, key 0x%08x, mask 0x%08x, action 0x%08x, action_0 0x%08x, action_1 0x%08x",
		  tbl_id, *(u32 *)key, *(u32 *)mask,
		  action ? *(u32 *)action : ~0,
		  action_0 ? *(u32 *)action_0 : ~0,
		  action_1 ? *(u32 *)action_1 : ~0);
}

static __maybe_unused void nbl_phy_cfg_pcmrt_tbl(struct nbl_phy_mgt *phy_mgt)
{
	union nbl_pa_pcmrt_key_tbl key_value;
	union nbl_pa_pcmrt_mask_tbl mask_value;
	union nbl_pa_pcmrt_action_tbl action;
	union nbl_pa_pcmrt_eth_action_tbl action_0;
	union nbl_pa_pcmrt_eth_action_tbl action_1;
	u32 tbl_id = 3;
	u32 mcc_id = 0;
	u32 mcc_ctrl_tbl;
	union nbl_umcc_table_wdata umcc_data;

	memset(&key_value, 0, sizeof(key_value));
	memset(&mask_value, 0, sizeof(mask_value));
	memset(&action, 0, sizeof(action));
	memset(&action_0, 0, sizeof(action_0));
	memset(&action_1, 0, sizeof(action_1));
	memset(&umcc_data, 0, sizeof(umcc_data));

	/* capture the ARP broadcast packets */
	key_value.info.dmac_type = PCMRT_DMAC_TYPE_BCAST;
	key_value.info.etype = PCMRT_ETYPE_ARP;
	key_value.info.valid = 1;

	mask_value.info.mask_dmac_type = 0;
	mask_value.info.mask_etype = 0;
	mask_value.info.mask_ip_protocol_type = 1;
	mask_value.info.mask_dport_type = 1;
	mask_value.info.mask_tcp_ctrl_bits_type = 1;

	action.info.fwd = NBL_BOOTIS_FWD_UPCALL;

	action_0.info.valid = 1;
	action_0.info.dport = NBL_BOOTIS_DPORT_ECPU;
	action_0.info.dport_id = NBL_BOOTIS_ECPU_ETH0_VSI;
	action_0.info.dport_info = 1839;

	action_1.info.valid = 1;
	action_1.info.dport = NBL_BOOTIS_DPORT_ECPU;
	action_1.info.dport_id = NBL_BOOTIS_ECPU_ETH1_VSI;
	action_1.info.dport_info = 1855;

	cfg_upa_pcmrt_tbl(phy_mgt, tbl_id, key_value.data, mask_value.data,
			  action.data, action_0.data, action_1.data);
	tbl_id++;

	/* capture the L3 multicast packets */
	key_value.info.dmac_type = PCMRT_DMAC_TYPE_L3_MCAST;
	key_value.info.etype = 0;
	key_value.info.valid = 1;

	mask_value.info.mask_dmac_type = 0;
	mask_value.info.mask_etype = 1;
	mask_value.info.mask_ip_protocol_type = 1;
	mask_value.info.mask_dport_type = 1;
	mask_value.info.mask_tcp_ctrl_bits_type = 1;
	cfg_upa_pcmrt_tbl(phy_mgt, tbl_id, key_value.data, mask_value.data,
			  action.data, action_0.data, action_1.data);
	tbl_id++;

	/* capture the ipv6 multicast packets */
	key_value.info.dmac_type = PCMRT_DMAC_TYPE_V6_MCAST;
	key_value.info.etype = 0;
	key_value.info.valid = 1;

	mask_value.info.mask_dmac_type = 0;
	mask_value.info.mask_etype = 1;
	mask_value.info.mask_ip_protocol_type = 1;
	mask_value.info.mask_dport_type = 1;
	mask_value.info.mask_tcp_ctrl_bits_type = 1;
	cfg_upa_pcmrt_tbl(phy_mgt, tbl_id, key_value.data, mask_value.data,
			  action.data, action_0.data, action_1.data);
	tbl_id++;

	/* add the mcc leaf tables for pf2 & pf3 */
	umcc_data.info.next_pntr = mcc_id + 1;
	umcc_data.info.dport = 0x2;
	umcc_data.info.dport_id = NBL_BOOTIS_ECPU_ETH0_VSI;
	umcc_data.info.rss = 1;
	umcc_data.info.valid = 1;
	nbl_hw_write_regs(phy_mgt, NBL_UMCC_TABLE_WDATA_REG,
			  umcc_data.data, NBL_UMCC_TABLE_WDATA_SIZE);

	mcc_ctrl_tbl = mcc_id;
	nbl_hw_write_regs(phy_mgt, NBL_UMCC_TABLE_CTRL_REG,
			  (u8 *)&mcc_ctrl_tbl, sizeof(mcc_ctrl_tbl));
	usleep_range(20000, 50000);
	umcc_data.info.next_pntr = 0;
	umcc_data.info.tail = 1;
	umcc_data.info.dport = 0x2;
	umcc_data.info.dport_id = NBL_BOOTIS_ECPU_ETH1_VSI;
	umcc_data.info.rss = 1;
	umcc_data.info.valid = 1;
	nbl_hw_write_regs(phy_mgt, NBL_UMCC_TABLE_WDATA_REG,
			  umcc_data.data, NBL_UMCC_TABLE_WDATA_SIZE);

	mcc_ctrl_tbl = (mcc_id + 1);
	nbl_hw_write_regs(phy_mgt, NBL_UMCC_TABLE_CTRL_REG,
			  (u8 *)&mcc_ctrl_tbl, sizeof(mcc_ctrl_tbl));
}

static void port_set_loopback_mode(struct nbl_phy_mgt *phy_mgt, u16 loopback_mode)
{
}

static void port_set_pkt_len_max(struct nbl_phy_mgt *phy_mgt, u32 maxlen)
{
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_eth_rx_pkt_len_reg pkt_len_reg;

	nbl_hw_read_regs(phy_mgt, DF200_ETH_RX_PKT_LEN_REG(common->eth_id),
			 pkt_len_reg.data, NBL_ETH_RX_PKT_LEN_REG_SIZE);

	pkt_len_reg.info.max_pkt_len = maxlen;

	nbl_hw_write_regs(phy_mgt, DF200_ETH_RX_PKT_LEN_REG(common->eth_id),
			  pkt_len_reg.data, NBL_ETH_RX_PKT_LEN_REG_SIZE);
}

static void port_set_pkt_len_min(struct nbl_phy_mgt *phy_mgt, u32 minlen)
{
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_eth_rx_pkt_len_reg pkt_len_reg;

	nbl_hw_read_regs(phy_mgt, DF200_ETH_RX_PKT_LEN_REG(common->eth_id),
			 pkt_len_reg.data, NBL_ETH_RX_PKT_LEN_REG_SIZE);

	pkt_len_reg.info.min_pkt_len = minlen;

	nbl_hw_write_regs(phy_mgt, DF200_ETH_RX_PKT_LEN_REG(common->eth_id),
			  pkt_len_reg.data, NBL_ETH_RX_PKT_LEN_REG_SIZE);
}

static void port_set_tx_enable(struct nbl_phy_mgt *phy_mgt)
{
	struct nbl_common_info *common = phy_mgt->common;
	u32 value;
	u32 ctrl;
	u32 benable = 1;

	nbl_hw_read_regs(phy_mgt, DF200_LOGIC_MAC_TX_EN_REG(common->eth_id),
			 (u8 *)&ctrl, sizeof(ctrl));
	/* [0] enable */
	ctrl &= 0xFFFFFFFE;
	value = benable & 0x00000001;
	ctrl |= value;

	nbl_hw_write_regs(phy_mgt, DF200_LOGIC_MAC_TX_EN_REG(common->eth_id),
			  (const u8 *)&ctrl, sizeof(ctrl));
}

static void port_set_rx_enable(struct nbl_phy_mgt *phy_mgt)
{
	struct nbl_common_info *common = phy_mgt->common;
	u32 ctrl;
	u32 benable = 0x1;

	printk("port_set_rx_enable: eth %u, addr 0x%llx.", common->eth_id, DF200_LOGIC_MAC_RX_EN_REG(common->eth_id));
	nbl_hw_read_regs(phy_mgt, DF200_LOGIC_MAC_RX_EN_REG(common->eth_id),
			 (u8 *)&ctrl, sizeof(ctrl));
	/* [0] enable */
	ctrl |= benable;

	nbl_hw_write_regs(phy_mgt, DF200_LOGIC_MAC_RX_EN_REG(common->eth_id),
			  (const u8 *)&ctrl, sizeof(ctrl));
}

static int port_reset(struct nbl_phy_mgt *phy_mgt)
{
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_eth_ip_reset eth_reset;
	union nbl_eth_ip_status link_status;
	u32 read_count = 0;

	nbl_hw_read_regs(phy_mgt, DF200_ETH_RESET_REG(common->eth_id),
			 eth_reset.data, NBL_ETH_IP_RESET_SIZE);
	eth_reset.info.csr_rst = 1;
	nbl_hw_write_regs(phy_mgt, DF200_ETH_RESET_REG(common->eth_id),
			  eth_reset.data, NBL_ETH_IP_RESET_SIZE);
	do {
		nbl_hw_read_regs(phy_mgt, DF200_ETH_IP_STAT_REG(common->eth_id),
				 link_status.data, NBL_ETH_IP_STATUS_SIZE);
		if (link_status.info.tx_pll_locked)
			break;
		usleep_range(200000, 500000);
		read_count++;
	} while (read_count < 5);
	if (read_count >= 5) {
		nbl_err(common, NBL_DEBUG_PHY, "reset the eth %u failed.\n", common->eth_id);
		return -1;
	}
	usleep_range(200000, 300000);
	eth_reset.info.csr_rst = 0;
	nbl_hw_write_regs(phy_mgt, DF200_ETH_RESET_REG(common->eth_id),
			  eth_reset.data, NBL_ETH_IP_RESET_SIZE);
	return 0;
}

static int nbl_phy_init_port(void *priv)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = phy_mgt->common;

	port_reset(phy_mgt);

	port_set_pkt_len_max(phy_mgt, ETH_PORT_PKT_MAX_LENGTH); /* 16000 */
	port_set_pkt_len_min(phy_mgt, ETH_PORT_PKT_MIN_LENGTH);

	port_set_loopback_mode(phy_mgt, 0);

	port_set_rx_enable(phy_mgt);
	port_set_tx_enable(phy_mgt);

	nbl_info(common, NBL_DEBUG_PHY, "nbl port init successed.\n");

	return 0;
}

static int nbl_phy_init_fec(void *priv)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = phy_mgt->common;

	nbl_info(common, NBL_DEBUG_PHY, "nbl fec init successed.\n");
	return 0;
}

static void phy_xcvr_avmm_write_data(struct nbl_phy_mgt *phy_mgt,
				     u32 eth_id, u32 lane, u32 address, u8 data)
{
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_eth_xcvr_avmm_ctrl_reg avmm_ctrl;
	struct nbl_eth_xcvr_avmm_data_reg wdata = {0};
	struct nbl_eth_xcvr_avmm_addr_reg addr = {0};
	int try_time;

	memset(&avmm_ctrl, 0, sizeof(avmm_ctrl));

	INIT_TRY_TIME(try_time);
	while (try_time--) {
		nbl_hw_read_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
				 avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);
		if (avmm_ctrl.info.rdy)
			break;
		usleep_range(200000, 500000);
	}

	if (try_time < 0)
		nbl_err(common, NBL_DEBUG_PHY, "xcvr avmm not ready to write for eth %u, lane %u.\n",
			eth_id, lane);

	/* rw = 0 indicates write */
	avmm_ctrl.info.rw = 0;
	avmm_ctrl.info.success = 0;
	nbl_hw_write_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
			  avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);

	wdata.data = data;
	nbl_hw_write_regs(phy_mgt, NBL_ETH_XCVR_AVMM_WDATA_REG(eth_id, lane),
			  (const u8 *)&wdata, sizeof(wdata));
	addr.addr = address;
	nbl_hw_write_regs(phy_mgt, NBL_ETH_XCVR_AVMM_ADDR_REG(eth_id, lane),
			  (const u8 *)&addr, sizeof(addr));

	INIT_TRY_TIME(try_time);
	while (try_time--) {
		nbl_hw_read_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
				 avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);
		if (avmm_ctrl.info.success)
			break;
		usleep_range(200000, 500000);
	}

	if (try_time < 0)
		nbl_err(common, NBL_DEBUG_PHY, "cfg xcvr avmm failed for eth %u, lane %u, addr: 0x%02x.\n",
			eth_id, lane, addr.addr);
}

static int phy_xcvr_avmm_read_data(struct nbl_phy_mgt *phy_mgt,
				   u32 eth_id, u32 lane, u32 address, u8 *data)
{
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_eth_xcvr_avmm_ctrl_reg avmm_ctrl;
	struct nbl_eth_xcvr_avmm_data_reg rdata = {0};
	struct nbl_eth_xcvr_avmm_addr_reg addr = {0};
	int try_time;

	memset(&avmm_ctrl, 0, sizeof(avmm_ctrl));

	INIT_TRY_TIME(try_time);
	while (try_time--) {
		nbl_hw_read_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
				 avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);
		if (avmm_ctrl.info.rdy)
			break;
		usleep_range(200000, 500000);
	}

	if (try_time < 0)
		nbl_err(common, NBL_DEBUG_PHY, "xcvr avmm not ready to read for eth %u, lane %u.\n",
			eth_id, lane);

	/* rw = 1 indicates read */
	avmm_ctrl.info.rw = 1;
	avmm_ctrl.info.success = 0;
	nbl_hw_write_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
			  avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);

	addr.addr = address;
	nbl_hw_write_regs(phy_mgt, NBL_ETH_XCVR_AVMM_ADDR_REG(eth_id, lane),
			  (const u8 *)&addr, sizeof(addr));

	INIT_TRY_TIME(try_time);
	while (try_time--) {
		nbl_hw_read_regs(phy_mgt, NBL_ETH_XCVR_AVMM_CTRL_REG(eth_id, lane),
				 avmm_ctrl.data, NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE);
		if (avmm_ctrl.info.success)
			break;
		usleep_range(200000, 500000);
	}

	if (try_time < 0) {
		nbl_err(common, NBL_DEBUG_PHY, "read xcvr avmm failed for eth %u, lane %u, addr: 0x%02x.\n",
			eth_id, lane, addr.addr);
		return -1;
	}

	nbl_hw_read_regs(phy_mgt, NBL_ETH_XCVR_AVMM_RDATA_REG(eth_id, lane),
			 (u8 *)&rdata, sizeof(rdata));

	*data = rdata.data;

	return 0;
}

static int phy_xcvr_code_operation(struct nbl_phy_mgt *phy_mgt, u32 eth_id, u8 lane,
				   u16 op_code, u16 op_data)
{
	struct nbl_common_info *common = phy_mgt->common;
	u8 send_ok = 0;
	u8 action_done = 0;
	int try_time, ret;
	u8 data;

	data = (u8)(op_data & 0xff);
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ATTRIBUTE_DATA_L, data);
	data = (u8)((op_data >> 8) & 0xff);
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ATTRIBUTE_DATA_H, data);
	data = (u8)(op_code & 0xff);
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ATTRIBUTE_CODE_L, data);
	data = (u8)((op_code >> 8) & 0xff);
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ATTRIBUTE_CODE_H, data);
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ATTRIBUTE_LOAD_START, 0x1);

	INIT_TRY_TIME(try_time);
	while (try_time--) {
		ret = phy_xcvr_avmm_read_data(phy_mgt, eth_id, lane,
					      NBL_PMA_ATTRIBUTE_SEND_OK, &send_ok);
		if (!ret && (send_ok & 0x80)) {
			ret = phy_xcvr_avmm_read_data(phy_mgt, eth_id, lane,
						      NBL_PMA_ATTRIBUTE_FINISHED,
						      &action_done);
			if (!ret && !(action_done & 0x1))
				break;
		}
	}

	if (try_time < 0) {
		nbl_err(common, NBL_DEBUG_PHY, "check the cfg failed for eth %u, lane %u, send_result: 0x%02x, action_done: 0x%02x.\n",
			eth_id, lane, send_ok, action_done);
		return -1;
	}
	send_ok |= 0x80;
	phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane,
				 NBL_PMA_ATTRIBUTE_SEND_OK, send_ok);
	return 0;
}

static int nbl_phy_setup_loopback(void *priv, u32 eth_id, u32 enable)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = phy_mgt->common;
	u32 lane;
	u16 op_data;
	int ret, result = 0;
	u16 loopback = enable ? 0x1 : 0;

	for (lane = 0; lane < ARRAY_SIZE(eth_xcvr_base_addr); lane++) {
		op_data = OP_DATA_INTERNAL_LOOPBACK | loopback;
		ret = phy_xcvr_code_operation(phy_mgt, eth_id, lane,
					      OP_CODE_LOOPBACK_SETTING, op_data);
		if (ret) {
			nbl_err(common, NBL_DEBUG_PHY, "xcvr code cfg failed for eth %u, lane %u, op_code: 0x%04x, op_data: 0x%04x.\n",
				eth_id, lane, OP_CODE_LOOPBACK_SETTING, op_data);
			result |= ret;
		}
	}

	nbl_info(common, NBL_DEBUG_PHY, "set loopback successed for eth %u loopback %u.\n",
		 eth_id, enable);
	return result;
}

static void nbl_phy_get_eth_ip_reg(void *priv, u32 eth_id, u64 addr_off, u32 *data)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u64 reg_addr;

	reg_addr = NBL_ETH_AVMM_REGISTER_MAP(eth_id) + (addr_off * 4);
	nbl_hw_read_regs(phy_mgt, reg_addr, (u8 *)data, sizeof(*data));
}

static void nbl_phy_set_eth_ip_reg(void *priv, u32 eth_id, u64 addr_off, u32 data)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u64 reg_addr;

	reg_addr = NBL_ETH_AVMM_REGISTER_MAP(eth_id) + (addr_off * 4);
	nbl_hw_write_regs(phy_mgt, reg_addr, (u8 *)&data, sizeof(data));
}

static int phy_pma_analog_reset(struct nbl_phy_mgt *phy_mgt, u32 eth_id)
{
	struct nbl_common_info *common = phy_mgt->common;
	u8 status = 0;
	u32 lane;
	int try_time, ret, result = 0;

	/* reset all lane firstly */
	for (lane = 0; lane < ARRAY_SIZE(eth_xcvr_base_addr); lane++) {
		phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ANALOG_RESET_REG0, 0);
		phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ANALOG_RESET_REG1, 0);
		phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ANALOG_RESET_REG2, 0);
		phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_ANALOG_RESET_REG3, 0x81);

		/* reset one lane may take abort 3s */
		try_time = 10;
		while (try_time--) {
			ret = phy_xcvr_avmm_read_data(phy_mgt, eth_id, lane,
						      NBL_PMA_ANALOG_RESET_STATUS, &status);
			if (!ret && status == PMA_ANALOG_RESET_DONE_VALUE)
				break;
			usleep_range(300000, 5000000);
		}
		if (try_time < 0) {
			nbl_err(common, NBL_DEBUG_PHY, "reset pma analog failed for eth %u, lane %u, status: 0x%02x.\n",
				eth_id, lane, status);
			result |= ret;
		}
	}
	usleep_range(10000, 20000);
	/* load initial PMA configuration */
	for (lane = 0; lane < ARRAY_SIZE(eth_xcvr_base_addr); lane++)
		phy_xcvr_avmm_write_data(phy_mgt, eth_id, lane, NBL_PMA_LOAD_INITIAL_SETTING, 1);

	return result;
}

static int phy_fec_dynamic_reconfigure(struct nbl_phy_mgt *phy_mgt,
				       u32 eth_id, enum nbl_port_mode mode)
{
	struct nbl_common_info *common = phy_mgt->common;
	union eth_ip_dr_ch_en_cfg dr_ch_en_cfg = {.data = 0};
	union eth_ip_dr_ch_mode_cfg dr_ch_mode = {.data = 0};
	union eth_ip_dr_fec_cfg dr_fec_cfg = {.data = 0};
	u32 status = 0;

	dr_fec_cfg.fec_en_ch0 = 1;
	dr_fec_cfg.fec_en_ch1 = 1;
	dr_fec_cfg.fec_en_ch2 = 1;
	dr_fec_cfg.fec_en_ch3 = 1;

	switch (mode) {
	case NBL_PORT_NRZ_NORSFEC:
		dr_fec_cfg.data = 0;
		break;
	case NBL_PORT_NRZ_544:
		dr_fec_cfg.fec_protocol = FEC_PROTOCOL_NRZ;
		dr_fec_cfg.fec_mode = FEC_MODE_KR_FEC_544;
		break;
	case NBL_PORT_NRZ_528:
		dr_fec_cfg.fec_protocol = FEC_PROTOCOL_NRZ;
		dr_fec_cfg.fec_mode = FEC_MODE_KR_FEC_528;
		break;
	case NBL_PORT_PAM4_544:
		dr_fec_cfg.fec_protocol = FEC_PROTOCOL_PAM4;
		dr_fec_cfg.fec_mode = FEC_MODE_KR_FEC_544;
		break;
	default:
		nbl_err(common, NBL_DEBUG_PHY, "fec mode %u not supported for eth %u.\n",
			mode, eth_id);
		return -1;
	}

	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_CFG_FEC, dr_fec_cfg.data);

	dr_ch_en_cfg.en_lane0_100G = 1;
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_CFG_CH_EN, dr_ch_en_cfg.data);

	dr_ch_mode.ch0_mode_sel = CHANNEL_MODE_MAC_PCS;
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_CFG_CH_MODE, dr_ch_mode.data);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_CONTROL, 1);

	/* need take more than 2s */
	usleep_range(2000000, 2500000);
	nbl_phy_get_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_STATUS, &status);
	if (status) {
		nbl_err(common, NBL_DEBUG_PHY, "cfg fec mode %u for eth %u failed, status %u.\n",
			mode, eth_id, status);
		return -1;
	}

	return 0;
}

static void phy_fec_dr_reset(struct nbl_phy_mgt *phy_mgt, u32 eth_id)
{
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0x8);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0xc);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0xe);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0xf);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0xe);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0xc);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0x8);
	usleep_range(1000, 1500);
	nbl_phy_set_eth_ip_reg(phy_mgt, eth_id, ETH_IP_DR_RESET, 0x0);
	usleep_range(1000, 1500);
}

static int nbl_phy_set_eth_fec_mode(void *priv, u32 eth_id, enum nbl_port_mode mode)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = NBL_PHY_MGT_TO_COMMON(phy_mgt);
	u32 lane;
	u16 op_code, op_data;
	int ret, result = 0;

	if (mode >= NBL_PORT_MODE_MAX) {
		nbl_err(common, NBL_DEBUG_PHY, "fec mode %u not supported for eth %u.\n",
			mode, eth_id);
		return -1;
	}
	/* disable all lane PMA */
	for (lane = 0; lane < ARRAY_SIZE(eth_xcvr_base_addr); lane++) {
		op_code = OP_CODE_ENABLE_DISABLE;
		op_data = OP_DATA_DISABLE_ALL;
		ret = phy_xcvr_code_operation(phy_mgt, eth_id, lane, op_code, op_data);
		if (ret) {
			nbl_err(common, NBL_DEBUG_PHY, "xcvr code cfg failed for eth %u, lane %u, op_code: 0x%04x, op_data: 0x%04x.\n",
				eth_id, lane, op_code, op_data);
			result |= ret;
		}
	}
	usleep_range(10000, 20000);

	/* asert the TX and RX soft reset signals */
	nbl_phy_set_eth_ip_reg(priv, eth_id, ETH_IP_PHY_CONFIGURATION,
			       PHY_CONFIGURATION_SOFT_TX_RESET | PHY_CONFIGURATION_SOFT_RX_RESET);
	usleep_range(10000, 20000);

	/* PMA analog resetting and reloading initial configuration */
	result |= phy_pma_analog_reset(phy_mgt, eth_id);
	usleep_range(10000, 20000);

	/* dr switch */
	result |= phy_fec_dynamic_reconfigure(phy_mgt, eth_id, mode);
	usleep_range(10000, 20000);

	/* enable all lane PMA */
	op_code = OP_CODE_ENABLE_DISABLE;
	op_data = OP_DATA_ENABLE_TX | OP_DATA_ENABLE_RX | OP_DATA_ENABLE_TX_OUTPUT;
	result |= phy_xcvr_code_operation(phy_mgt, eth_id, 0, op_code, op_data);
	result |= phy_xcvr_code_operation(phy_mgt, eth_id, 2, op_code, op_data);
	if (mode == NBL_PORT_PAM4_544)
		op_data = OP_DATA_ENABLE_TX | OP_DATA_ENABLE_RX;

	result |= phy_xcvr_code_operation(phy_mgt, eth_id, 1, op_code, op_data);
	result |= phy_xcvr_code_operation(phy_mgt, eth_id, 3, op_code, op_data);
	usleep_range(10000, 20000);

	/* FEC dr reset */
	phy_fec_dr_reset(phy_mgt, eth_id);
	usleep_range(10000, 20000);

	/* release soft tx and keep soft_rx */
	nbl_phy_set_eth_ip_reg(priv, eth_id, ETH_IP_PHY_CONFIGURATION,
			       PHY_CONFIGURATION_SOFT_RX_RESET);
	usleep_range(30000, 35000);

	return result;
}

static __maybe_unused void nbl_phy_cfg_uiflt(struct nbl_phy_mgt *phy_mgt, u16 eth_id, u32 table_id)
{
	union nbl_uiflt_mkey_mask uiflt_mkey;
	union nbl_uiflt_mkey_mask uiflt_mask;
	union nbl_uiflt_action uiflt_action;
	int i = 0;

	memset(&uiflt_mkey, 0, sizeof(uiflt_mkey));
	memset(&uiflt_action, 0, sizeof(uiflt_action));
	memset(uiflt_mask.data, 0xff, sizeof(uiflt_mask.data));

	uiflt_mask.info.eth_lag_id = 0;
	nbl_hw_write_regs(phy_mgt, UIFLT_MASK_TABLE(table_id),
			  uiflt_mask.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MASK_TABLE(%08x): ", UIFLT_MASK_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mask.data[i * 4]));

	uiflt_mkey.info.valid = 1;
	uiflt_mkey.info.eth_lag_id = eth_id;
	nbl_hw_write_regs(phy_mgt, UIFLT_MKEY_TABLE(table_id),
			  uiflt_mkey.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MKEY_TABLE(%08x): ", UIFLT_MKEY_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mkey.data[i * 4]));

	uiflt_action.info.upcall_dport_id = NBL_PHY_MGT_TO_COMMON(phy_mgt)->vsi_id;
	uiflt_action.info.valid = 1;
	uiflt_action.info.upcall_en = 1;
	uiflt_action.info.upcall_straight = 1;
	uiflt_action.info.upcall_dport = NBL_BOOTIS_DPORT_ECPU;
	uiflt_action.info.upcall_rss_en = 1;
	uiflt_action.info.upcall_fwd = NBL_BOOTIS_FWD_UPCALL;
	uiflt_action.info.count_en = 1;
	uiflt_action.info.count_id = table_id;
	nbl_hw_write_regs(phy_mgt, UIFLT_ACTION_TABLE(table_id),
			  uiflt_action.data, NBL_UIFLT_ACTION_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_ACTION_TABLE(%08x): ", UIFLT_ACTION_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_ACTION_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_action.data[i * 4]));

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "uiflt match table ok\n");
}

static int nbl_phy_init_chip_module(void *priv, u8 eth_speed, u8 eth_num)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u32 static_done, dynamic_done;
	int i = 10;

	while (i--) {
		nbl_hw_read_regs(phy_mgt, NBL_STATIC_INIT_DONE,
				 (u8 *)&static_done, sizeof(static_done));
		nbl_hw_read_regs(phy_mgt, NBL_DYNAMIC_INIT_DONE,
				 (u8 *)&dynamic_done, sizeof(dynamic_done));
		if ((static_done & NBL_STATIC_INIT_DONE_VALUE) == NBL_STATIC_INIT_DONE_VALUE &&
		    dynamic_done == NBL_INIT_DONE_BIT)
			break;
		usleep_range(1000000, 2000000);
	}

	if (i < 0) {
		nbl_err(NBL_PHY_MGT_TO_COMMON(phy_mgt), NBL_DEBUG_PHY,
			"chip init done check failed, static_done 0x%08x, dynamic_done 0x%08x.\n",
			static_done, dynamic_done);
		return -1;
	}

	/*nbl_phy_cfg_pcmrt_tbl(phy_mgt);*/
/*	nbl_phy_cfg_uiflt(phy_mgt, NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id,
 *			  NBL_PHY_MGT_TO_COMMON(phy_mgt)->eth_id);
 */

	nbl_phy_init_port(phy_mgt);

	return 0;
}

static int nbl_phy_get_firmware_version(void *priv, char *firmware_verion)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u32 prj_id, version_id;

	nbl_hw_read_regs(phy_mgt, NBL_STATIC_PRJ_ID, (u8 *)&prj_id, sizeof(prj_id));
	nbl_hw_read_regs(phy_mgt, NBL_STATIC_VERSION_ID, (u8 *)&version_id, sizeof(version_id));

	snprintf(firmware_verion, ETHTOOL_FWVERS_LEN - 1, "%08x-%08x", prj_id, version_id);

	return 0;
}

static void nbl_phy_set_promisc_mode(void *priv, u16 vsi_id, u16 eth_id, u16 mode)
{
	//TODO
}

static void cfg_upa_ext_etype(struct nbl_phy_mgt *phy_mgt, u32 id)
{
	u32 value;

	value = (0x8809 << 16) | 0x88cc;

	nbl_hw_write_regs(phy_mgt, NBL_UPA_EXT_ETYPE_REG(id), (const u8 *)&value, sizeof(value));

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "upa ext etype table ok\n");
}

static __maybe_unused void cfg_uiflt_match_table(struct nbl_phy_mgt *phy_mgt,
						 u16 eth_id, u32 table_id)
{
	union nbl_uiflt_mkey_mask uiflt_mkey;
	union nbl_uiflt_mkey_mask uiflt_mask;
	union nbl_uiflt_action uiflt_action;
	int i = 0;

	memset(&uiflt_mkey, 0, sizeof(uiflt_mkey));
	memset(&uiflt_action, 0, sizeof(uiflt_action));
	memset(uiflt_mask.data, 0xff, sizeof(uiflt_mask.data));

	uiflt_mask.info.eth_lag_id = 0;
	uiflt_mask.info.dmac_high_16 = 0;
	uiflt_mask.info.dmac_low_32 = 0xffffff;
	nbl_hw_write_regs(phy_mgt, UIFLT_MASK_TABLE(table_id),
			  uiflt_mask.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MASK_TABLE(%08x): ", UIFLT_MASK_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mask.data[i * 4]));

	uiflt_mkey.info.valid = 1;
	uiflt_mkey.info.eth_lag_id = eth_id;
	uiflt_mkey.info.dmac_high_16 = 0x0180;
	uiflt_mkey.info.dmac_low_32 = 0xc2 << 24;
	nbl_hw_write_regs(phy_mgt, UIFLT_MKEY_TABLE(table_id),
			  uiflt_mkey.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MKEY_TABLE(%08x): ", UIFLT_MKEY_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mkey.data[i * 4]));

	uiflt_action.info.upcall_dport_id = NBL_PHY_MGT_TO_COMMON(phy_mgt)->vsi_id;
	uiflt_action.info.valid = 1;
	uiflt_action.info.upcall_en = 1;
	uiflt_action.info.upcall_dport = NBL_BOOTIS_DPORT_ECPU;
	uiflt_action.info.upcall_rss_en = 1;
	uiflt_action.info.upcall_fwd = NBL_BOOTIS_FWD_UPCALL;
	uiflt_action.info.count_en = 1;
	uiflt_action.info.count_id = 0;
	nbl_hw_write_regs(phy_mgt, UIFLT_ACTION_TABLE(table_id),
			  uiflt_action.data, NBL_UIFLT_ACTION_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_ACTION_TABLE(%08x): ", UIFLT_ACTION_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_ACTION_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_action.data[i * 4]));

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "uiflt match table ok\n");
}

static __maybe_unused void clear_uiflt_match_table(struct nbl_phy_mgt *phy_mgt, u32 table_id)
{
	union nbl_uiflt_mkey_mask uiflt_mkey;
	union nbl_uiflt_mkey_mask uiflt_mask;
	union nbl_uiflt_action uiflt_action;
	int i = 0;

	memset(&uiflt_mkey, 0, sizeof(uiflt_mkey));
	memset(&uiflt_mask, 0, sizeof(uiflt_mask));
	memset(&uiflt_action, 0, sizeof(uiflt_action));

	nbl_hw_write_regs(phy_mgt, UIFLT_MASK_TABLE(table_id),
			  uiflt_mask.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MASK_TABLE(%08x): ", UIFLT_MASK_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mask.data[i * 4]));

	nbl_hw_write_regs(phy_mgt, UIFLT_MKEY_TABLE(table_id),
			  uiflt_mkey.data, NBL_UIFLT_MKEY_MASK_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_MKEY_TABLE(%08x): ", UIFLT_MKEY_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_MKEY_MASK_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_mkey.data[i * 4]));

	nbl_hw_write_regs(phy_mgt, UIFLT_ACTION_TABLE(table_id),
			  uiflt_action.data, NBL_UIFLT_ACTION_TABLE_WIDTH);
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "cfg the UIFLT_ACTION_TABLE(%08x): ", UIFLT_ACTION_TABLE(table_id));
	for (i = 0; i < NBL_UIFLT_ACTION_TABLE_WIDTH / 4; i++)
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "%08x ",
			  *((u32 *)&uiflt_action.data[i * 4]));

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "uiflt match table ok\n");
}

static int nbl_phy_enable_lag_protocol(void *priv, u16 eth_id, void *data)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_enable_lag_param *param = data;
	union nbl_pa_pcmrt_key_tbl key_value;
	union nbl_pa_pcmrt_mask_tbl mask_value;
	union nbl_pa_pcmrt_action_tbl action;
	union nbl_pa_pcmrt_eth_action_tbl eth_action;
	u8 *eth_action_data[2] = {0};
	u32 tbl_id = param->flow_tbl_id;

	memset(&key_value, 0, sizeof(key_value));
	memset(&mask_value, 0, sizeof(mask_value));
	memset(&action, 0, sizeof(action));
	memset(&eth_action, 0, sizeof(eth_action));

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "enable lag protocol for eth %u, ext_tbl_id %u, flow_tbl_id %u",
		  eth_id, param->pa_ext_type_tbl_id, tbl_id);
	if (param->enable) {
		cfg_upa_ext_etype(phy_mgt, param->pa_ext_type_tbl_id);

		/* capture the lldp packets */
		key_value.info.dmac_type = PCMRT_DMAC_TYPE_LLDP;
		key_value.info.etype = PCMRT_ETYPE_EXT_0 + ((param->pa_ext_type_tbl_id) * 2);
		key_value.info.valid = 1;

		mask_value.info.mask_dmac_type = 0;
		mask_value.info.mask_etype = 0;
		mask_value.info.mask_ip_protocol_type = 1;
		mask_value.info.mask_dport_type = 1;
		mask_value.info.mask_tcp_ctrl_bits_type = 1;

		action.info.fwd = NBL_BOOTIS_FWD_UPCALL;
		action.info.mcc_en = 0;

		eth_action.info.dport = NBL_BOOTIS_DPORT_ECPU;
		eth_action.info.dport_id = NBL_PHY_MGT_TO_COMMON(phy_mgt)->vsi_id;
		if (param->upcall_queue)
			eth_action.info.dport_info = param->upcall_queue;
		else
			eth_action.info.rss_en = 1;
		eth_action.info.valid = 1;

		eth_action_data[eth_id] = eth_action.data;

		cfg_upa_pcmrt_tbl(phy_mgt, tbl_id, key_value.data, mask_value.data,
				  action.data, eth_action_data[0], eth_action_data[1]);

		/* capture the lacp packets using the next pcmrt table */
		tbl_id++;
		key_value.info.etype = PCMRT_ETYPE_EXT_0 + ((param->pa_ext_type_tbl_id) * 2) + 1;
		cfg_upa_pcmrt_tbl(phy_mgt, tbl_id, key_value.data, mask_value.data,
				  action.data, eth_action_data[0], eth_action_data[1]);
	} else {
		/* only update the pcmrt action table related the eth port */
		nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_ETH_ACTION_TABLE(eth_id, tbl_id),
				  eth_action.data, NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE);
		tbl_id++;
		nbl_hw_write_regs(phy_mgt, NBL_UPA_PCMRT_ETH_ACTION_TABLE(eth_id, tbl_id),
				  eth_action.data, NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE);
	}
	return 0;
}

static int nbl_phy_cfg_lag_algorithm(void *priv, u16 eth_id, u16 lag_id,
				     enum netdev_lag_hash hash_type)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_dflow_lag_hash_reg lag_hash_type;
	enum nbl_lag_hash_type hw_hash_type = NBL_LAG_HASH_TYPE_L2_KERNEL;

	switch (hash_type) {
	case NETDEV_LAG_HASH_L23:
	case NETDEV_LAG_HASH_E23:
		hw_hash_type = NBL_LAG_HASH_TYPE_L23_KERNEL;
		break;
	case NETDEV_LAG_HASH_L34:
	case NETDEV_LAG_HASH_E34:
		hw_hash_type = NBL_LAG_HASH_TYPE_L34_KERNEL;
		break;
	default:
		break;
	}

	nbl_hw_read_regs(phy_mgt, DFLOW_LAG_HASH_REG,
			 lag_hash_type.data, NBL_DFLOW_LAG_HASH_REG_WIDTH);
	if (lag_hash_type.info.lag0_hash_type != hw_hash_type) {
		lag_hash_type.info.lag0_hash_type = hw_hash_type;
		nbl_hw_write_regs(phy_mgt, DFLOW_LAG_HASH_REG,
				  lag_hash_type.data, NBL_DFLOW_LAG_HASH_REG_WIDTH);
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "set lag hash type reg done.\n");
	} else {
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "lag hash type info not changed.\n");
	}
	return 0;
}

static int nbl_phy_cfg_lag_member_list(void *priv, struct nbl_lag_member_list_param *param)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_depro_lag_reg port_list_info;
	union nbl_depro_lag_reg new_lag;
	u16 lag_id = param->lag_id;

	new_lag.info.lag0_eth0 = param->port_list[0];
	new_lag.info.lag0_eth1 = param->port_list[1];

	nbl_hw_read_regs(phy_mgt, DEPRO_LAG_REG,
			 port_list_info.data, NBL_DEPRO_LAG_REG_WIDTH);

	if ((port_list_info.data[0] & (0x3 << (lag_id * 2))) ^ (new_lag.data[0] << (lag_id * 2))) {
		port_list_info.data[0] = (port_list_info.data[0] & ~(0x3 << (lag_id * 2))) |
				(new_lag.data[0] << (lag_id * 2));
		nbl_hw_write_regs(phy_mgt, DEPRO_LAG_REG,
				  port_list_info.data, NBL_DEPRO_LAG_REG_WIDTH);
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
			  "set lag member reg: %d-%d.\n", param->port_list[0], param->port_list[1]);
	} else {
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "lag hash member info not changed.\n");
	}
	return 0;
}

static bool nbl_phy_sfp_is_present(void *priv, u32 eth_id)
{
	u32 sfp_present = 0;

	nbl_hw_read_regs(priv, NBL_SFP_MODPRESL(eth_id),
			 (u8 *)&sfp_present, sizeof(sfp_present));

	return (sfp_present & NBL_SFP_PRESENT_BIT) ? false : true;
}

static int nbl_phy_read_i2c(void *priv, u32 eth_id, u16 slave_addr,
			    u8 channel, u8 read_byte, u8 addr, u32 *rdata)
{
	union nbl_sfp_ctrl sfp_ctrl;
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	u32 sfp_read_done = 0;
	u32 sfp_endian = 0;
	__be32 sfp_rdata = 0;
	int try_time = 10;

	if (!nbl_phy_sfp_is_present(priv, eth_id)) {
		pr_err("sfp not present for eth %u\n", eth_id);
		return -ENXIO;
	}

	memset(&sfp_ctrl, 0, sizeof(sfp_ctrl));

	sfp_ctrl.info.sfp_chn = channel;
	sfp_ctrl.info.sfp_addr = addr;
	sfp_ctrl.info.sfp_rw = SFP_READ;
	sfp_ctrl.info.sfp_slave_addr = slave_addr;
	sfp_ctrl.info.sfp_byte_num = read_byte;
	nbl_hw_write_regs(priv, NBL_SFP_CTRL(eth_id),
			  sfp_ctrl.data, NBL_SFP_CTRL_REG_WIDTH);

	while (try_time--) {
		nbl_hw_read_regs(priv, NBL_SFP_DONE(eth_id),
				 (u8 *)&sfp_read_done, sizeof(sfp_read_done));
		if (sfp_read_done & BIT(channel))
			break;
		usleep_range(500, 1000);
	}
	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "read data for eth %u, chn %u, slave_addr %u, addr %u, len %u, try_time %u.\n",
		  eth_id, channel, slave_addr, addr, read_byte, try_time);
	if (try_time < 0) {
		pr_err("read data failed for eth %u, chn %u, slave_addr %u, addr %u, len %u.\n",
		       eth_id, channel, slave_addr, addr, read_byte);
		return -EIO;
	}

	nbl_hw_read_regs(priv, NBL_SFP_RDATA(eth_id, channel), (u8 *)&sfp_rdata, sizeof(sfp_rdata));

	nbl_hw_read_regs(priv, NBL_SFP_I2C_ENDIAN_CFG, (u8 *)&sfp_endian, sizeof(sfp_endian));

	if (sfp_endian & NBL_SFP_I2C_ENDIAN_BIG)
		sfp_rdata = be32_to_cpu(sfp_rdata);
	*rdata = sfp_rdata;

	return 0;
}

static enum nbl_sfp_type nbl_phy_get_sfp_type(void *priv, u16 eth_id)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = phy_mgt->common;
	u32 i2c_rdata = 0;
	enum nbl_sfp_type sfp_type;

	nbl_phy_read_i2c(priv, eth_id, NBL_SFF_8636_SLAVE_ADDR, common->function,
			 NBL_I2C_READ_MAXLEN, NBL_SFF8636_IDENTIFIER_OFFSET, &i2c_rdata);
	sfp_type = i2c_rdata & 0xff;
	return sfp_type;
}

static int nbl_phy_set_sfp_state(void *priv, u8 eth_id, u8 state)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	struct nbl_common_info *common = phy_mgt->common;
	union nbl_sfp28_tx_disable sfp28_tx_disable;
	enum nbl_sfp_type sfp_type;
	int ret = 0;

	sfp_type = nbl_phy_get_sfp_type(priv, eth_id);
	switch (sfp_type) {
	case SFF8024_IDENTIFIER_VALUE_QSFP28:
		sfp28_tx_disable.info.tx_disable_lane0 = !state;
		sfp28_tx_disable.info.tx_disable_lane1 = !state;
		sfp28_tx_disable.info.tx_disable_lane2 = !state;
		sfp28_tx_disable.info.tx_disable_lane3 = !state;
		nbl_hw_write_regs(phy_mgt, NBL_ETH_SFP_TX_DISABLE(eth_id),
				  sfp28_tx_disable.data, NBL_SFP28_TX_DISABLE_REG_WIDTH);
		break;
	default:
		ret = -1;
		break;
	}
	nbl_debug(common, NBL_DEBUG_PHY, "set sfp state %u, type 0x%x.\n", state, sfp_type);

	return ret;
}

static int nbl_phy_cfg_lag_member_fwd(void *priv, u16 eth_id, u16 lag_id, u8 fwd)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_depro_eth_port_reg eth_port_reg;

	nbl_hw_read_regs(phy_mgt, DEPRO_ETH_PORT_REG,
			 eth_port_reg.data, NBL_DEPRO_ETH_PORT_REG_WIDTH);

	if (eth_id == 0)
		eth_port_reg.info.eth0_fwd = fwd;
	if (eth_id == 1)
		eth_port_reg.info.eth1_fwd = fwd;

	nbl_hw_write_regs(phy_mgt, DEPRO_ETH_PORT_REG,
			  eth_port_reg.data, NBL_DEPRO_ETH_PORT_REG_WIDTH);

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "set epro eth port reg done.\n");
	return 0;
}

static int nbl_phy_cfg_lag_member_up_attr(void *priv, u16 eth_id, u16 lag_id, bool enable)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_uipro_eth_port_reg port_reg_info;

	nbl_hw_read_regs(phy_mgt, UIPRO_ETH_PORT_REG(eth_id),
			 port_reg_info.data, NBL_UIPRO_ETH_PORT_REG_WIDTH);
	if (enable == port_reg_info.info.lag_en && lag_id == port_reg_info.info.lag_id) {
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY, "port lag reg info not changed.\n");
		return 0;
	}

	if (enable) {
		port_reg_info.info.lag_en = 1;
		port_reg_info.info.lag_id = lag_id;
	} else {
		port_reg_info.info.lag_en = 0;
		port_reg_info.info.lag_id = 0;
	}
	nbl_hw_write_regs(phy_mgt, UIPRO_ETH_PORT_REG(eth_id),
			  port_reg_info.data, NBL_UIPRO_ETH_PORT_REG_WIDTH);

	nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
		  "set port lag reg done, eth_id: %d, lag_id: %d, lag_en: %d.\n",
		  eth_id, lag_id, enable);
	return 0;
}

static void nbl_phy_get_rx_queue_drop_stats(void *priv, u16 queue_id,
					    struct nbl_queue_err_stats *queue_err_stats)
{
	union nbl_uvn_queue_stat uvn_stat;

	nbl_hw_read_regs(priv, NBL_UVN_ECPU_QUEUE_STAT(queue_id),
			 uvn_stat.data, NBL_UVN_QUEUE_STAT_SIZE);
	queue_err_stats->uvn_stat_pkt_drop = uvn_stat.info.pkt_drop_cnt;
}

static void nbl_phy_get_tx_queue_drop_stats(void *priv, u16 queue_id,
					    struct nbl_queue_err_stats *queue_err_stats)
{
	union nbl_dvn_queue_stat dvn_stat;

	nbl_hw_read_regs(priv, NBL_DVN_ECPU_QUEUE_STAT(queue_id),
			 dvn_stat.data, NBL_DVN_QUEUE_STAT_SIZE);
	queue_err_stats->dvn_pkt_drop_cnt = dvn_stat.info.pkt_drop_cnt;
}

static void nbl_phy_get_coalesce(void *priv, u16 interrupt_id, u16 *pnum, u16 *rate)
{
	union nbl_ecpu_msix_info_tbl msix_info;

	nbl_hw_read_regs(priv, NBL_ECPU_MSIX_INFO_TBL(interrupt_id),
			 msix_info.data, NBL_ECPU_MSIX_INFO_TBL_SIZE);

	*pnum = msix_info.info.intrl_pnum;
	*rate = NBL_ECPU_MSIX_SOFT_RATE(msix_info.info.intrl_rate);
}

static void nbl_phy_set_coalesce(void *priv, u16 interrupt_id, u16 pnum, u16 rate)
{
	union nbl_ecpu_msix_info_tbl msix_info;
	u32 hw_rate;

	nbl_hw_read_regs(priv, NBL_ECPU_MSIX_INFO_TBL(interrupt_id),
			 msix_info.data, NBL_ECPU_MSIX_INFO_TBL_SIZE);

	hw_rate = (u16)NBL_ECPU_MSIX_HW_RATE(rate);
	msix_info.info.intrl_pnum = pnum;
	msix_info.info.intrl_rate = hw_rate;
	nbl_hw_write_regs(priv, NBL_ECPU_MSIX_INFO_TBL(interrupt_id),
			  msix_info.data, NBL_ECPU_MSIX_INFO_TBL_SIZE);
}

static int nbl_phy_get_eth_address(void *priv, u32 eth_id, u8 *eth_addr)
{
	union nbl_board_info board_info;
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;

	nbl_hw_read_regs(priv, NBL_BOARD_INFO_REG, board_info.data, sizeof(board_info));
	switch (eth_id) {
	case 0:
		memcpy(eth_addr, board_info.mac4, ETH_ALEN);
		break;
	case 1:
		memcpy(eth_addr, board_info.mac5, ETH_ALEN);
		break;
	default:
		nbl_debug(phy_mgt->common, NBL_DEBUG_PHY,
			  "get eth mac address failed for eth %u.\n", eth_id);
		return -1;
	}
	nbl_info(phy_mgt->common, NBL_DEBUG_PHY,
		 "get eth mac address for eth %u, addr: %pM.\n", eth_id, eth_addr);
	return 0;
}

static void nbl_phy_set_eth_stats_snapshot(void *priv, u32 eth_id, u8 snapshot)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	union nbl_eth_ip_reset eth_reset;

	/* set the eth mac stats snapshot */
	nbl_hw_read_regs(phy_mgt, DF200_ETH_RESET_REG(eth_id),
			 eth_reset.data, NBL_ETH_IP_RESET_SIZE);
	eth_reset.info.stats_snapshot = snapshot & 0x1;
	nbl_hw_write_regs(phy_mgt, DF200_ETH_RESET_REG(eth_id),
			  eth_reset.data, NBL_ETH_IP_RESET_SIZE);
}

static const u32 nbl_bootis_reg_dump_list[] = {
	NBL_STATIC_PRJ_ID,
	NBL_STATIC_VERSION_ID,
};

static void nbl_phy_get_reg_dump(void *priv, u32 *data, u32 len)
{
	struct nbl_phy_mgt *phy_mgt = (struct nbl_phy_mgt *)priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(nbl_bootis_reg_dump_list) && i < len; i++) {
		nbl_hw_read_regs(phy_mgt, nbl_bootis_reg_dump_list[i],
				 (u8 *)&data[i], sizeof(data[i]));
		data[i] = cpu_to_be32(data[i]);
	}
}

static int nbl_phy_get_reg_dump_len(void *priv)
{
	return ARRAY_SIZE(nbl_bootis_reg_dump_list) * sizeof(u32);
}

static struct nbl_phy_ops phy_ops_bootis = {
	.init_chip_module		= nbl_phy_init_chip_module,
	.get_firmware_version		= nbl_phy_get_firmware_version,
	.set_promisc_mode		= nbl_phy_set_promisc_mode,
	.add_mv_tbl			= nbl_phy_add_mv_tbl,
	.del_mv_tbl			= nbl_phy_del_mv_tbl,
	.cfg_rss_alg			= nbl_phy_cfg_rss_alg,
	.set_qid_map_table		= nbl_phy_set_qid_map_table,
	.cfg_epro_rss_ret		= nbl_phy_cfg_epro_rss,
	.cfg_epro_vpt_tbl		= nbl_phy_cfg_epro_vsi_tbl,
	.reset_dvn_cfg			= nbl_phy_reset_tx_queue,
	.reset_uvn_cfg			= nbl_phy_reset_rx_queue,
	.cfg_tx_queue			= nbl_phy_cfg_tx_queue,
	.cfg_rx_queue			= nbl_phy_cfg_rx_queue,
	.get_rx_queue_err_stats		= nbl_phy_get_rx_queue_drop_stats,
	.get_tx_queue_err_stats		= nbl_phy_get_tx_queue_drop_stats,
	.cfg_padpt_txrx_enable		= nbl_phy_cfg_padpt_txrx_enable,
	.cfg_q2tc_netid			= nbl_phy_cfg_q2tc_netid,
	.set_tc_wgt			= nbl_phy_set_tc_wgt,
	.set_tc_spwrr			= nbl_phy_set_tc_spwrr,
	.cfg_dsch_net_to_group		= nbl_phy_cfg_dsch_net_to_group,
	.cfg_dsch_group_to_port		= nbl_phy_cfg_group_to_port,
	.disable_dvn			= nbl_phy_disable_tx,
	.disable_uvn			= nbl_phy_disable_rx,
	.configure_msix_map		= nbl_phy_configure_msix_map,
	.configure_msix_info		= nbl_phy_configure_msix_info,
	.enable_msix_irq		= nbl_phy_enable_msix_irq,
	.get_msix_irq_enable_info	= nbl_phy_get_msix_irq_enable_info,
	.get_msix_resource		= nbl_phy_get_msix_resource,
	.set_vnet_queue_info		= nbl_phy_set_vnet_queue_info,
	.clear_vnet_queue_info		= nbl_phy_clear_vnet_queue_info,
	.add_mcc			= NULL,
	.del_mcc			= NULL,
	.get_coalesce			= nbl_phy_get_coalesce,
	.set_coalesce			= nbl_phy_set_coalesce,
	.update_tail_ptr		= nbl_phy_update_tail_ptr,
	.get_tail_ptr			= nbl_phy_get_tail_ptr,
	.get_hw_addr			= nbl_phy_get_hw_addr,
	.set_spoof_check_addr		= nbl_phy_set_spoof_check_addr,
	.set_spoof_check_enable		= nbl_phy_set_spoof_check_enable,
	.init_port			= nbl_phy_init_port,
	.init_fec			= nbl_phy_init_fec,
	.set_eth_stats_snapshot		= nbl_phy_set_eth_stats_snapshot,
	.get_eth_ip_reg			= nbl_phy_get_eth_ip_reg,
	.set_eth_fec_mode		= nbl_phy_set_eth_fec_mode,
	.sfp_is_present			= nbl_phy_sfp_is_present,
	.read_i2c			= nbl_phy_read_i2c,
	.get_eth_mac_address		= nbl_phy_get_eth_address,
	.setup_loopback			= nbl_phy_setup_loopback,
	.enable_lag_protocol		= nbl_phy_enable_lag_protocol,
	.cfg_lag_hash_algorithm		= nbl_phy_cfg_lag_algorithm,
	.cfg_lag_member_fwd		= nbl_phy_cfg_lag_member_fwd,
	.set_sfp_state			= nbl_phy_set_sfp_state,
	.cfg_lag_member_list		= nbl_phy_cfg_lag_member_list,
	.cfg_lag_member_up_attr		= nbl_phy_cfg_lag_member_up_attr,
	.get_reg_dump			= nbl_phy_get_reg_dump,
	.get_reg_dump_len		= nbl_phy_get_reg_dump_len,
};

/* Structure starts here, adding an op should not modify anything below */
static int nbl_phy_setup_phy_mgt(struct nbl_common_info *common,
				 struct nbl_phy_mgt_bootis **phy_mgt_bootis)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	*phy_mgt_bootis = devm_kzalloc(dev, sizeof(struct nbl_phy_mgt_bootis), GFP_KERNEL);
	if (!*phy_mgt_bootis)
		return -ENOMEM;

	NBL_PHY_MGT_TO_COMMON(&(*phy_mgt_bootis)->phy_mgt) = common;

	return 0;
}

static void nbl_phy_remove_phy_mgt(struct nbl_common_info *common,
				   struct nbl_phy_mgt_bootis **phy_mgt_bootis)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, *phy_mgt_bootis);
	*phy_mgt_bootis = NULL;
}

static int nbl_phy_setup_ops(struct nbl_common_info *common, struct nbl_phy_ops_tbl **phy_ops_tbl,
			     struct nbl_phy_mgt_bootis *phy_mgt_bootis)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	*phy_ops_tbl = devm_kzalloc(dev, sizeof(struct nbl_phy_ops_tbl), GFP_KERNEL);
	if (!*phy_ops_tbl)
		return -ENOMEM;

	NBL_PHY_OPS_TBL_TO_OPS(*phy_ops_tbl) = &phy_ops_bootis;
	NBL_PHY_OPS_TBL_TO_PRIV(*phy_ops_tbl) = phy_mgt_bootis;

	return 0;
}

static void nbl_phy_remove_ops(struct nbl_common_info *common, struct nbl_phy_ops_tbl **phy_ops_tbl)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, *phy_ops_tbl);
	*phy_ops_tbl = NULL;
}

int nbl_phy_init_bootis(void *p, struct nbl_init_param *param)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct nbl_common_info *common;
	struct pci_dev *pdev;
	struct nbl_phy_mgt_bootis **phy_mgt_bootis;
	struct nbl_phy_mgt *phy_mgt;
	struct nbl_phy_ops_tbl **phy_ops_tbl;
	int bar_mask;
	int ret = 0;

	common = NBL_ADAPTER_TO_COMMON(adapter);
	phy_mgt_bootis = (struct nbl_phy_mgt_bootis **)&NBL_ADAPTER_TO_PHY_MGT(adapter);
	phy_ops_tbl = &NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);
	pdev = NBL_COMMON_TO_PDEV(common);

	ret = nbl_phy_setup_phy_mgt(common, phy_mgt_bootis);
	if (ret)
		goto setup_mgt_fail;

	phy_mgt = &(*phy_mgt_bootis)->phy_mgt;
	bar_mask = BIT(NBL_MEMORY_BAR);
	ret = pci_request_selected_regions(pdev, bar_mask, NBL_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Request memory bar failed, err = %d\n", ret);
		goto request_bar_region_fail;
	}

	phy_mgt->hw_addr = pci_ioremap_bar(pdev, NBL_MEMORY_BAR);
	if (!phy_mgt->hw_addr) {
		dev_err(&pdev->dev, "Memory bar ioremap failed\n");
		ret = -EIO;
		goto ioremap_err;
	}
	phy_mgt->hw_size = pci_resource_len(pdev, NBL_MEMORY_BAR);

	switch (NBL_COMMON_TO_PCI_FUNC_ID(common)) {
	case NBL_BOOTIS_ECPU_ETH0_FUNCTION:
		phy_mgt->notify_offset = PF2_NET_NOTIFY_ADDR;
		break;
	case NBL_BOOTIS_ECPU_ETH1_FUNCTION:
		phy_mgt->notify_offset = PF3_NET_NOTIFY_ADDR;
		break;
	default:
		break;
	}

	ret = nbl_phy_setup_ops(common, phy_ops_tbl, *phy_mgt_bootis);
	if (ret)
		goto setup_ops_fail;

	return 0;

setup_ops_fail:
	iounmap(phy_mgt->hw_addr);
ioremap_err:
	pci_release_selected_regions(pdev, bar_mask);
request_bar_region_fail:
	nbl_phy_remove_phy_mgt(common, phy_mgt_bootis);
setup_mgt_fail:
	return ret;
}

void nbl_phy_remove_bootis(void *p)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct nbl_common_info *common;
	struct nbl_phy_mgt_bootis **phy_mgt_bootis;
	struct nbl_phy_ops_tbl **phy_ops_tbl;
	struct pci_dev *pdev;
	u8 __iomem *hw_addr;
	int bar_mask = BIT(NBL_MEMORY_BAR);

	common = NBL_ADAPTER_TO_COMMON(adapter);
	phy_mgt_bootis = (struct nbl_phy_mgt_bootis **)&NBL_ADAPTER_TO_PHY_MGT(adapter);
	phy_ops_tbl = &NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);
	pdev = NBL_COMMON_TO_PDEV(common);

	hw_addr = (*phy_mgt_bootis)->phy_mgt.hw_addr;

	iounmap(hw_addr);
	pci_release_selected_regions(pdev, bar_mask);
	nbl_phy_remove_phy_mgt(common, phy_mgt_bootis);

	nbl_phy_remove_ops(common, phy_ops_tbl);
}
