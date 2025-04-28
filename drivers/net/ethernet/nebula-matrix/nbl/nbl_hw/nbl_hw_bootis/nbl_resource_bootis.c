// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_queue_bootis.h"
#include "nbl_resource_bootis.h"

static bool is_ops_inited;

static u16 pfvfid_to_vsi_id(void *p, int pfid, int vfid, u16 type)
{
	switch (pfid) {
	case NBL_BOOTIS_ECPU_ETH0_FUNCTION:
		return NBL_BOOTIS_ECPU_ETH0_VSI;
	case NBL_BOOTIS_ECPU_ETH1_FUNCTION:
		return NBL_BOOTIS_ECPU_ETH1_VSI;
	default:
		pr_err("ilegal pf/vf function id, pf:%u, vf:%u\n", pfid, vfid);
		return 0xffff;
	}
}

static u16 vsi_id_to_func_id(void *p, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)p;
	int pfid;
	int vfid;

	switch (vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		pfid = NBL_BOOTIS_ECPU_ETH0_FUNCTION;
		vfid = U32_MAX;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		pfid = NBL_BOOTIS_ECPU_ETH1_FUNCTION;
		vfid = U32_MAX;
		break;
	default:
		pr_err("ilegal vsi_id id:%u.\n", vsi_id);
		return 0xffff;
	}

	return nbl_res_pfvfid_to_func_id(res_mgt, pfid, vfid);
}

static int vsi_id_to_pf_id(void *p, u16 vsi_id)
{
	int pfid;

	switch (vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		pfid = NBL_BOOTIS_ECPU_ETH0_FUNCTION;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		pfid = NBL_BOOTIS_ECPU_ETH1_FUNCTION;
		break;
	default:
		pr_err("ilegal vsi_id id:%u.\n", vsi_id);
		return 0xffff;
	}

	return pfid;
}

static u16 func_id_to_vsi_id(void *p, u16 func_id, u16 type)
{
	int pfid = U32_MAX;
	int vfid = U32_MAX;

	nbl_res_func_id_to_pfvfid(p, func_id, &pfid, &vfid);

	return pfvfid_to_vsi_id(p, pfid, vfid, type);
}

static u16 get_particular_queue_id(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u16 queue_id = 0;

	switch (vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		queue_id = NBL_BOOTIS_ECPU_ETH0_FIXED_QUEUE;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		queue_id = NBL_BOOTIS_ECPU_ETH1_FIXED_QUEUE;
		break;
	default:
		nbl_err(common, NBL_DEBUG_QUEUE, "ilegal vsi_id %u for getting particular queue id.\n",
			vsi_id);
	}
	return queue_id;
}

static u8 eth_id_to_pf_id(void *priv, u8 eth_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u8 pf_id = 0;

	switch (eth_id) {
	case NBL_PORT_ETH0:
		pf_id = NBL_BOOTIS_ECPU_ETH0_FUNCTION;
		break;
	case NBL_PORT_ETH1:
		pf_id = NBL_BOOTIS_ECPU_ETH1_FUNCTION;
		break;
	default:
		nbl_err(common, NBL_DEBUG_QUEUE, "ilegal eth_id %u for getting pf id.\n",
			eth_id);
	}
	return pf_id;
}

static void nbl_res_setup_common_ops(struct nbl_resource_mgt *res_mgt)
{
	res_mgt->common_ops.pfvfid_to_vsi_id = pfvfid_to_vsi_id;
	res_mgt->common_ops.vsi_id_to_func_id = vsi_id_to_func_id;
	res_mgt->common_ops.vsi_id_to_pf_id = vsi_id_to_pf_id;
	res_mgt->common_ops.func_id_to_vsi_id = func_id_to_vsi_id;
	res_mgt->common_ops.eth_id_to_pf_id = eth_id_to_pf_id;
	res_mgt->common_ops.get_particular_queue_id = get_particular_queue_id;
}

static int nbl_res_get_queue_num(struct nbl_resource_mgt *res_mgt,
				 u16 func_id, u16 *tx_queue_num, u16 *rx_queue_num)
{
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u16 q_num = 0;

	switch (common->vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		q_num = NBL_BOOTIS_ECPU_ETH0_QUEUE_NUM;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		q_num = NBL_BOOTIS_ECPU_ETH1_QUEUE_NUM;
		break;
	default:
		pr_err("ilegal vsi_id id:%u.\n", common->vsi_id);
	}

	*tx_queue_num = q_num;
	*rx_queue_num = q_num;

	return 0;
}

static int nbl_res_register_net(void *priv, u16 func_id,
				struct nbl_register_net_param *register_param,
				struct nbl_register_net_result *register_result)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	netdev_features_t csumo_features;
	netdev_features_t tso_features;
	u16 tx_queue_num, rx_queue_num;
	u8 mac[ETH_ALEN] = {0};
	int ret = 0;

	csumo_features = NBL_FEATURE(NETIF_F_RXCSUM) |
			 NBL_FEATURE(NETIF_F_IP_CSUM) |
			 NBL_FEATURE(NETIF_F_IPV6_CSUM);
	tso_features = NBL_FEATURE(NETIF_F_TSO) |
		       NBL_FEATURE(NETIF_F_TSO6) |
		       NBL_FEATURE(NETIF_F_GSO_UDP_L4);
	register_result->hw_features |= csumo_features |
					tso_features |
					NBL_FEATURE(NETIF_F_SG);
	register_result->features |= register_result->hw_features |
				     NBL_FEATURE(NETIF_F_HW_TC) |
				     NBL_FEATURE(NETIF_F_HW_VLAN_CTAG_FILTER) |
				     NBL_FEATURE(NETIF_F_HW_VLAN_STAG_FILTER);

	register_result->max_mtu = NBL_MAX_JUMBO_FRAME_SIZE;

	ret = phy_ops->get_eth_mac_address(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), common->eth_id, mac);
	if (ret) {
		nbl_err(common, NBL_DEBUG_RESOURCE, "faile to get mac address for eth :%u.\n",
			common->eth_id);
		return -ENODEV;
	}
	memcpy(register_result->mac, mac, ETH_ALEN);

	func_id = nbl_res_vsi_id_to_func_id(res_mgt, common->vsi_id);
	nbl_res_get_queue_num(res_mgt, func_id, &tx_queue_num, &rx_queue_num);
	register_result->tx_queue_num = tx_queue_num;
	register_result->rx_queue_num = rx_queue_num;
	register_result->queue_size = NBL_DEFAULT_DESC_NUM;

	switch (common->vsi_id) {
	case NBL_BOOTIS_ECPU_ETH0_VSI:
		NBL_RES_BASE_QID(res_mgt) = NBL_BOOTIS_ECPU_ETH0_QUEUE_BASE;
		break;
	case NBL_BOOTIS_ECPU_ETH1_VSI:
		NBL_RES_BASE_QID(res_mgt) = NBL_BOOTIS_ECPU_ETH1_QUEUE_BASE;
		break;
	default:
		nbl_err(common, NBL_DEBUG_RESOURCE, "ilegal vsi_id id:%u.\n", common->vsi_id);
		return -ENODEV;
	}

	return ret;
}

static u16 nbl_res_get_vsi_id(void *priv, u16 func_id, u16 type)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u16 vsi_id;

	switch (NBL_COMMON_TO_PCI_FUNC_ID(common)) {
	case NBL_BOOTIS_ECPU_ETH0_FUNCTION:
		vsi_id = NBL_BOOTIS_ECPU_ETH0_VSI;
		break;
	case NBL_BOOTIS_ECPU_ETH1_FUNCTION:
		vsi_id = NBL_BOOTIS_ECPU_ETH1_VSI;
		break;
	default:
		nbl_err(common, NBL_DEBUG_RESOURCE, "ilegal function:%u.\n",
			NBL_COMMON_TO_PCI_FUNC_ID(common));
		vsi_id = 0xffff;
		break;
	}
	return vsi_id;
}

static void nbl_res_get_eth_id(void *priv, u16 vsi_id, u8 *eth_mode, u8 *eth_id, u8 *logic_eth_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);

	switch (NBL_COMMON_TO_PCI_FUNC_ID(common)) {
	case NBL_BOOTIS_ECPU_ETH0_FUNCTION:
		*eth_id = 0;
		*logic_eth_id = 0;
		break;
	case NBL_BOOTIS_ECPU_ETH1_FUNCTION:
		*eth_id = 1;
		*logic_eth_id = 1;
		break;
	default:
		nbl_err(common, NBL_DEBUG_RESOURCE, "ilegal function:%u.\n",
			NBL_COMMON_TO_PCI_FUNC_ID(common));
		break;
	}

	*eth_mode = NBL_TWO_ETHERNET_PORT;
}

static int nbl_res_enable_lag_protocol(void *priv, u16 eth_id, bool lag_en)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_enable_lag_param enable_lag_param = {0};
	int ret = 0;

	enable_lag_param.enable = lag_en;
	enable_lag_param.pa_ext_type_tbl_id = NBL_LAG_UPA_EXT_TYPE_TBL_ID;
	enable_lag_param.flow_tbl_id = NBL_LAG_UPA_PCMRT_TBL_BASE;
	enable_lag_param.upcall_queue =
			nbl_res_get_particular_queue_id(res_mgt,
							NBL_RES_MGT_TO_COMMON(res_mgt)->vsi_id);

	ret = phy_ops->enable_lag_protocol(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					   eth_id, &enable_lag_param);
	return ret;
}

static int nbl_res_cfg_lag_hash_algorithm(void *priv, u16 eth_id, u16 lag_id,
					  enum netdev_lag_hash hash_type)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->cfg_lag_hash_algorithm(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					       eth_id, lag_id, hash_type);
}

static int nbl_res_cfg_lag_member_fwd(void *priv, u16 eth_id, u16 lag_id, u8 fwd)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->cfg_lag_member_fwd(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id, lag_id, fwd);
}

static int nbl_res_set_sfp_state(void *priv, u8 eth_id, u8 state)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->set_sfp_state(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id, state);
}

static int nbl_res_cfg_lag_member_list(void *priv, struct nbl_lag_member_list_param *param)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->cfg_lag_member_list(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), param);
}

static int nbl_res_cfg_lag_member_up_attr(void *priv, u16 eth_id, u16 lag_id, bool enable)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->cfg_lag_member_up_attr(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					       eth_id, lag_id, enable);
}

static int nbl_res_get_firmware_version(void *priv, char *firmware_verion)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	int ret = 0;

	ret = phy_ops->get_firmware_version(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), firmware_verion);
	return ret;
}

static int nbl_res_get_driver_info(void *priv, struct nbl_driver_info *driver_info)
{
	strscpy(driver_info->driver_version, NBL_BOOTIS_DRIVER_VERSION,
		sizeof(driver_info->driver_version));
	return 1;
}

static void nbl_res_register_rdma(void *priv, u16 vsi_id, struct nbl_rdma_register_param *param)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_resource_info *resource_info = NBL_RES_MGT_TO_RES_INFO(res_mgt);

	param->has_rdma = false;
	param->mem_type = NBL_RDMA_MEM_TYPE_MAX;

	if (resource_info->rdma_info.rdma_vacant > 0) {
		resource_info->rdma_info.rdma_vacant--;
		param->has_rdma = true;
		param->intr_num = NBL_RES_RDMA_INTR_NUM;
		param->id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id) -
			    NBL_BOOTIS_ECPU_ETH0_FUNCTION;
	}
}

static void nbl_res_unregister_rdma(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_resource_info *resource_info = NBL_RES_MGT_TO_RES_INFO(res_mgt);

	resource_info->rdma_info.rdma_vacant++;
}

static void nbl_res_register_rdma_bond(void *priv, struct nbl_lag_member_list_param *list_param,
				       struct nbl_rdma_register_param *register_param)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_resource_info *resource_info = NBL_RES_MGT_TO_RES_INFO(res_mgt);
	struct nbl_rdma_info *rdma_info = &resource_info->rdma_info;

	register_param->has_rdma = false;

	if (rdma_info->rdma_vacant > 0) {
		rdma_info->rdma_vacant--;
		register_param->has_rdma = true;
		register_param->intr_num = NBL_RES_RDMA_INTR_NUM;
	}
}

static void nbl_res_unregister_rdma_bond(void *priv, u16 lag_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_resource_info *resource_info = NBL_RES_MGT_TO_RES_INFO(res_mgt);
	struct nbl_rdma_info *rdma_info = &resource_info->rdma_info;

	rdma_info->rdma_vacant++;
}

static u8 __iomem *nbl_res_get_hw_addr(void *priv, size_t *size)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->get_hw_addr(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), size);
}

static u64 nbl_res_get_real_hw_addr(void *priv, u16 vsi_id)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct pci_dev *pdev = NBL_COMMON_TO_PDEV(common);
	u64 real_hw_addr;
	u32 val;

	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &val);
	real_hw_addr = (u64)(val & PCI_BASE_ADDRESS_MEM_MASK);
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0 + 4, &val);
	real_hw_addr |= ((u64)val << 32);

	return real_hw_addr;
}

static void nbl_res_get_real_bdf(void *priv, u16 vsi_id, u8 *bus, u8 *dev, u8 *function)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);

	*bus = common->bus;
	*dev = common->devid;
	*function = NBL_COMMON_TO_PCI_FUNC_ID(common);
}

static void nbl_res_get_reg_dump(void *priv, u32 *data, u32 len)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	phy_ops->get_reg_dump(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), data, len);
}

static int nbl_res_get_reg_dump_len(void *priv)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->get_reg_dump_len(NBL_RES_MGT_TO_PHY_PRIV(res_mgt));
}

static int nbl_res_get_board_id(void *priv)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);

	return NBL_COMMON_TO_BOARD_ID(common);
}

static struct nbl_resource_ops res_ops = {
	.register_net = nbl_res_register_net,
	.get_firmware_version = nbl_res_get_firmware_version,
	.get_driver_info = nbl_res_get_driver_info,
	.get_vsi_id = nbl_res_get_vsi_id,
	.get_eth_id = nbl_res_get_eth_id,
	.register_rdma = nbl_res_register_rdma,
	.unregister_rdma = nbl_res_unregister_rdma,
	.register_rdma_bond = nbl_res_register_rdma_bond,
	.unregister_rdma_bond = nbl_res_unregister_rdma_bond,
	.get_hw_addr = nbl_res_get_hw_addr,
	.get_real_hw_addr = nbl_res_get_real_hw_addr,
	.get_real_bdf = nbl_res_get_real_bdf,
	.enable_lag_protocol = nbl_res_enable_lag_protocol,
	.cfg_lag_hash_algorithm = nbl_res_cfg_lag_hash_algorithm,
	.cfg_lag_member_fwd = nbl_res_cfg_lag_member_fwd,
	.set_sfp_state = nbl_res_set_sfp_state,
	.cfg_lag_member_list = nbl_res_cfg_lag_member_list,
	.cfg_lag_member_up_attr = nbl_res_cfg_lag_member_up_attr,
	.get_reg_dump = nbl_res_get_reg_dump,
	.get_reg_dump_len = nbl_res_get_reg_dump_len,
	.get_product_flex_cap = nbl_res_get_flex_capability,
	.get_product_fix_cap = nbl_res_get_fix_capability,
	.get_board_id = nbl_res_get_board_id,
};

static struct nbl_res_product_ops product_ops = {
	.queue_mgt_init			= nbl_queue_mgt_init_bootis,
};

static int nbl_res_ctrl_dev_sriov_info_init(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct device *dev =  NBL_COMMON_TO_DEV(common);
	struct nbl_sriov_info *sriov_info = NULL;
	u16 function;

	sriov_info = devm_kzalloc(dev, sizeof(struct nbl_sriov_info), GFP_KERNEL);
	if (!sriov_info)
		return -ENOMEM;

	NBL_RES_MGT_TO_SRIOV_INFO(res_mgt) = sriov_info;

	function = NBL_COMMON_TO_PCI_FUNC_ID(common);
	sriov_info->bdf = PCI_DEVID(common->bus, PCI_DEVFN(common->devid, function));

	return 0;
}

static void nbl_res_ctrl_dev_sriov_info_remove(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_sriov_info **sriov_info = &NBL_RES_MGT_TO_SRIOV_INFO(res_mgt);
	struct device *dev = NBL_RES_MGT_TO_DEV(res_mgt);

	if (!(*sriov_info))
		return;

	devm_kfree(dev, *sriov_info);
	*sriov_info = NULL;
}

static void nbl_res_stop(struct nbl_resource_mgt_bootis *res_mgt_bootis)
{
	struct nbl_resource_mgt *res_mgt = &res_mgt_bootis->res_mgt;

	nbl_queue_mgt_stop(res_mgt);
	nbl_txrx_mgt_stop(res_mgt);
	nbl_intr_mgt_stop(res_mgt);
	nbl_vsi_mgt_stop(res_mgt);
	nbl_flow_mgt_stop_bootis(res_mgt);
	nbl_port_mgt_stop_bootis(res_mgt);
	nbl_res_ctrl_dev_sriov_info_remove(res_mgt);
}

static void nbl_res_init_pf_num(struct nbl_resource_mgt *res_mgt)
{
	NBL_RES_MGT_TO_PF_NUM(res_mgt) = NBL_MAX_PF_BOOTIS;

	return;
}

static int nbl_res_start(struct nbl_resource_mgt_bootis *res_mgt_bootis,
			 struct nbl_func_caps caps)
{
	struct nbl_resource_mgt *res_mgt = &res_mgt_bootis->res_mgt;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_resource_info *resource_info = NBL_RES_MGT_TO_RES_INFO(res_mgt);
	int ret = 0;

	resource_info->eth_mode = NBL_TWO_ETHERNET_PORT;
	NBL_COMMON_TO_MGT_PF(common) = NBL_COMMON_TO_PCI_FUNC_ID(common);

	nbl_res_init_pf_num(res_mgt);

	ret = nbl_res_ctrl_dev_sriov_info_init(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_flow_mgt_start_bootis(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_vsi_mgt_start(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_intr_mgt_start(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_txrx_mgt_start(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_queue_mgt_start(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_port_mgt_start_bootis(res_mgt);
	if (ret)
		goto start_fail;

	ret = nbl_res_intr_init_msix_resource(res_mgt);
	if (ret)
		goto start_fail;

	nbl_res_set_fix_capability(res_mgt, NBL_ETH_SUPPORT_NRZ_RS_FEC_544);

	return 0;

start_fail:
	nbl_res_stop(res_mgt_bootis);
	return ret;
}

static int nbl_res_setup_res_mgt(struct nbl_common_info *common,
				 struct nbl_resource_mgt_bootis **res_mgt)
{
	struct device *dev;
	struct nbl_resource_info *resource_info;

	dev = NBL_COMMON_TO_DEV(common);
	*res_mgt = devm_kzalloc(dev, sizeof(struct nbl_resource_mgt_bootis), GFP_KERNEL);
	if (!*res_mgt)
		return -ENOMEM;
	NBL_RES_MGT_TO_COMMON(&(*res_mgt)->res_mgt) = common;

	resource_info = devm_kzalloc(dev, sizeof(struct nbl_resource_info), GFP_KERNEL);
	if (!resource_info)
		return -ENOMEM;
	NBL_RES_MGT_TO_RES_INFO(&(*res_mgt)->res_mgt) = resource_info;

	return 0;
}

static void nbl_res_remove_res_mgt(struct nbl_common_info *common,
				   struct nbl_resource_mgt_bootis **res_mgt)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, NBL_RES_MGT_TO_RES_INFO(&(*res_mgt)->res_mgt));
	devm_kfree(dev, *res_mgt);
	*res_mgt = NULL;
}

static void nbl_res_remove_ops(struct device *dev, struct nbl_resource_ops_tbl **res_ops_tbl)
{
	devm_kfree(dev, *res_ops_tbl);
	*res_ops_tbl = NULL;
}

static int nbl_res_setup_ops(struct device *dev, struct nbl_resource_ops_tbl **res_ops_tbl,
			     struct nbl_resource_mgt_bootis *res_mgt)
{
	int ret = 0;

	*res_ops_tbl = devm_kzalloc(dev, sizeof(struct nbl_resource_ops_tbl), GFP_KERNEL);
	if (!*res_ops_tbl)
		return -ENOMEM;

	if (!is_ops_inited) {
		ret = nbl_flow_setup_ops_bootis(&res_ops);
		if (ret)
			goto setup_fail;

		ret = nbl_queue_setup_ops_bootis(&res_ops);
		if (ret)
			goto setup_fail;

		ret = nbl_txrx_setup_ops(&res_ops);
		if (ret)
			goto setup_fail;

		ret = nbl_intr_setup_ops_bootis(&res_ops);
		if (ret)
			goto setup_fail;

		ret = nbl_vsi_setup_ops(&res_ops);
		if (ret)
			goto setup_fail;

		ret = nbl_port_setup_ops_bootis(&res_ops);
		if (ret)
			goto setup_fail;

		is_ops_inited = true;
	}

	NBL_RES_OPS_TBL_TO_OPS(*res_ops_tbl) = &res_ops;
	NBL_RES_OPS_TBL_TO_PRIV(*res_ops_tbl) = res_mgt;

	return 0;

setup_fail:
	nbl_res_remove_ops(dev, res_ops_tbl);
	return -EAGAIN;
}

int nbl_res_init_bootis(void *p, struct nbl_init_param *param)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct device *dev;
	struct nbl_common_info *common;
	struct nbl_resource_mgt_bootis **res_mgt;
	struct nbl_resource_ops_tbl **res_ops_tbl;
	struct nbl_phy_ops_tbl *phy_ops_tbl;
	struct nbl_channel_ops_tbl *chan_ops_tbl;
	int ret = 0;

	dev = NBL_ADAPTER_TO_DEV(adapter);
	common = NBL_ADAPTER_TO_COMMON(adapter);
	res_mgt = (struct nbl_resource_mgt_bootis **)&NBL_ADAPTER_TO_RES_MGT(adapter);
	res_ops_tbl = &NBL_ADAPTER_TO_RES_OPS_TBL(adapter);
	phy_ops_tbl = NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);
	chan_ops_tbl = NBL_ADAPTER_TO_CHAN_OPS_TBL(adapter);

	ret = nbl_res_setup_res_mgt(common, res_mgt);
	if (ret)
		goto setup_mgt_fail;

	nbl_res_setup_common_ops(&(*res_mgt)->res_mgt);
	NBL_RES_MGT_TO_CHAN_OPS_TBL(&(*res_mgt)->res_mgt) = chan_ops_tbl;
	NBL_RES_MGT_TO_PHY_OPS_TBL(&(*res_mgt)->res_mgt) = phy_ops_tbl;

	NBL_RES_MGT_TO_PROD_OPS(&(*res_mgt)->res_mgt) = &product_ops;

	ret = nbl_res_start(*res_mgt, param->caps);
	if (ret)
		goto start_fail;

	ret = nbl_res_setup_ops(dev, res_ops_tbl, *res_mgt);
	if (ret)
		goto setup_ops_fail;

	return 0;

setup_ops_fail:
	nbl_res_stop(*res_mgt);
start_fail:
	nbl_res_remove_res_mgt(common, res_mgt);
setup_mgt_fail:
	return ret;
}

void nbl_res_remove_bootis(void *p)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct device *dev;
	struct nbl_common_info *common;
	struct nbl_resource_mgt_bootis **res_mgt;
	struct nbl_resource_ops_tbl **res_ops_tbl;

	dev = NBL_ADAPTER_TO_DEV(adapter);
	common = NBL_ADAPTER_TO_COMMON(adapter);
	res_mgt = (struct nbl_resource_mgt_bootis **)&NBL_ADAPTER_TO_RES_MGT(adapter);
	res_ops_tbl = &NBL_ADAPTER_TO_RES_OPS_TBL(adapter);

	nbl_res_remove_ops(dev, res_ops_tbl);
	nbl_res_stop(*res_mgt);
	nbl_res_remove_res_mgt(common, res_mgt);
}
