// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_resource_virtio.h"

static u8 nbl_res_get_virtio_status(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u8 device_status;

	phy_ops->get_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_status),
				&device_status, sizeof(u8));

	return device_status;
}

static u8 nbl_res_reset_virtio_status(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	int i = 0;
	u8 device_status = 0;

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_status),
				&device_status, sizeof(u8));
	do {
		usleep_range(100000, 200000);
		device_status = nbl_res_get_virtio_status(res_mgt);
		i++;
	} while (device_status && i <= NBL_VIRTIO_RESET_WAIT_TIMES);

	return device_status;
}

static void nbl_res_set_virtio_status(struct nbl_resource_mgt *res_mgt, u8 status)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u8 device_status;

	usleep_range(1000, 2000);
	device_status = nbl_res_get_virtio_status(res_mgt) | status;
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_status),
				&device_status, sizeof(u8));
}

static u64 nbl_res_get_virtio_features(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 device_feature_select;
	u32 device_feature_lo;
	u32 device_feature_hi;

	device_feature_select = 0;
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_feature_select),
				&device_feature_select, sizeof(u32));
	phy_ops->get_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_feature),
				&device_feature_lo, sizeof(u32));

	device_feature_select = 1;
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_feature_select),
				&device_feature_select, sizeof(u32));
	phy_ops->get_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, device_feature),
				&device_feature_hi, sizeof(u32));

	return ((u64)device_feature_hi << 32) | device_feature_lo;
}

static void nbl_res_set_finalize_features(struct nbl_resource_mgt *res_mgt, u64 features)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 guest_feature_select;
	u32 guest_feature_lo;
	u32 guest_feature_hi;

	guest_feature_select = 0;
	guest_feature_lo = (u32)features;
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, guest_feature_select),
				&guest_feature_select, sizeof(u32));
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, guest_feature),
				&guest_feature_lo, sizeof(u32));

	guest_feature_select = 1;
	guest_feature_hi = features >> 32;
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, guest_feature_select),
				&guest_feature_select, sizeof(u32));
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, guest_feature),
				&guest_feature_hi, sizeof(u32));
}

static u16 nbl_res_get_virtio_mtu(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 mtu;

	phy_ops->get_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_net_config, mtu),
				&mtu, sizeof(u16));

	return mtu;
}

static u8 nbl_res_get_virtio_mac(struct nbl_resource_mgt *res_mgt, int index)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u8 mac;

	phy_ops->get_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_net_config, mac) + index,
				&mac, sizeof(u8));

	return mac;
}

static u16 nbl_res_get_queue_pairs(struct nbl_resource_mgt *res_mgt)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 queue_pairs;

	phy_ops->get_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_net_config, max_queue_pairs),
				&queue_pairs, sizeof(u16));

	return queue_pairs;
}

static u16 nbl_res_get_queue_size(struct nbl_resource_mgt *res_mgt, u16 queue_index)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 queue_size;

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_select),
				&queue_index, sizeof(u16));
	phy_ops->get_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_size),
				&queue_size, sizeof(u16));

	return queue_size;
}

static void nbl_res_set_extend_header(struct nbl_resource_mgt *res_mgt, bool enable)
{
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 extend_mode = (u32)enable;

	phy_ops->set_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), NBL_PCI_CFG_EXT_HEADER_OFF,
				&extend_mode, sizeof(u32));
}

static int nbl_res_register_net(void *priv, u16 func_id,
				struct nbl_register_net_param *register_param,
				struct nbl_register_net_result *register_result)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u8 status;
	u8 mac[ETH_ALEN] = {0};
	u16 queue_pairs = 1;
	u16 queue_size = NBL_VIRTIO_DEFAULT_DESC_NUM;
	u16 mtu = NBL_VIRTIO_DEFAULT_MTU;
	u64 device_features = 0;
	u64 negotiate_features = 0;
	u64 hw_features = 0;
	int i;

	status = nbl_res_reset_virtio_status(res_mgt);
	if (status) {
		nbl_err(common, NBL_DEBUG_RESOURCE,
			"nbl virtio device reset failed, status:%u\n", status);
		return -ENODEV;
	}
	nbl_info(common, NBL_DEBUG_RESOURCE, "nbl virtio device been reset.\n");

	nbl_res_set_virtio_status(res_mgt, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
	nbl_info(common, NBL_DEBUG_RESOURCE, "nbl virtio device acknowledge driver.\n");

	device_features = nbl_res_get_virtio_features(res_mgt);
	negotiate_features = device_features & NBL_VIRTIO_DRIVER_FEATURES;
	nbl_res_set_finalize_features(res_mgt, negotiate_features);
	nbl_res_set_virtio_status(res_mgt, VIRTIO_CONFIG_S_FEATURES_OK);
	status = nbl_res_get_virtio_status(res_mgt);
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		nbl_err(common, NBL_DEBUG_RESOURCE,
			"nbl virtio device refuses features:0x%llx, status:%u\n",
			negotiate_features, status);
		return -ENODEV;
	}
	nbl_info(common, NBL_DEBUG_RESOURCE,
		 "nbl virtio device features ok:0x%llx.\n", negotiate_features);

	if (negotiate_features & (1ULL << VIRTIO_NET_F_MTU)) {
		mtu = nbl_res_get_virtio_mtu(res_mgt);
		if (mtu < ETH_MIN_MTU) {
			nbl_err(common, NBL_DEBUG_RESOURCE,
				"nbl virtio device mtu too small:%u.\n", mtu);
			return -EINVAL;
		}
	}

	if (negotiate_features & (1ULL << VIRTIO_NET_F_MAC)) {
		for (i = 0; i < ETH_ALEN; i++)
			mac[i] = nbl_res_get_virtio_mac(res_mgt, i);
	}

	if (negotiate_features & (1ULL << VIRTIO_NET_F_MQ))
		queue_pairs = nbl_res_get_queue_pairs(res_mgt);
	queue_size = nbl_res_get_queue_size(res_mgt, 0);

	if (negotiate_features & (1ULL << VIRTIO_NET_F_CSUM)) {
		hw_features |= NBL_FEATURE(NETIF_F_HW_CSUM) | NBL_FEATURE(NETIF_F_SG);
		if (negotiate_features & (1ULL << VIRTIO_NET_F_GSO))
			hw_features |= NBL_FEATURE(NETIF_F_TSO) | NBL_FEATURE(NETIF_F_TSO6);
		if (negotiate_features & (1ULL << VIRTIO_NET_F_HOST_TSO4))
			hw_features |= NBL_FEATURE(NETIF_F_TSO);
		if (negotiate_features & (1ULL << VIRTIO_NET_F_HOST_TSO6))
			hw_features |= NBL_FEATURE(NETIF_F_TSO6);
	}

	register_result->hw_features = hw_features;
	register_result->features = hw_features | NBL_FEATURE(NETIF_F_GSO_ROBUST);
	register_result->max_mtu = mtu;
	memcpy(register_result->mac, mac, ETH_ALEN);
	register_result->tx_queue_num = queue_pairs;
	register_result->rx_queue_num = queue_pairs;
	register_result->queue_size = queue_size;

	/* when using this driver, the virtio device uses private extend header mode */
	nbl_res_set_extend_header(res_mgt, true);

	return 0;
}

static void nbl_res_register_rdma(void *priv, u16 vsi_id, struct nbl_rdma_register_param *param)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	if (phy_ops->get_rdma_capability(NBL_RES_MGT_TO_PHY_PRIV(res_mgt))) {
		param->has_rdma = true;
		param->intr_num = NBL_RES_RDMA_INTR_NUM;
		param->id = nbl_res_vsi_id_to_func_id(res_mgt, vsi_id);
	}
}

static u16 nbl_res_get_vsi_id(void *priv, u16 func_id, u16 type)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 vsi_id;

	phy_ops->get_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), NBL_PCI_CFG_VIRTIO_VSI_ID,
				&vsi_id, sizeof(u32));

	return (u16)vsi_id;
}

static u16 nbl_res_get_msix_entry_id(void *priv, u16 vsi_id, u16 local_vector_id)
{
	return local_vector_id;
}

static void nbl_res_configure_virtio_dev_msix(void *priv, u16 vector)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, msix_config),
				&vector, sizeof(u16));
}

static void nbl_res_configure_rdma_msix_off(void *priv, u16 vector)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 base_rdma_msix = (u32)vector;

	phy_ops->set_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), NBL_PCI_CFG_RDMA_MSIX_OFF,
				&base_rdma_msix, sizeof(u32));
}

static void nbl_res_configure_virtio_dev_ready(void *priv)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);

	nbl_res_set_virtio_status(res_mgt, VIRTIO_CONFIG_S_DRIVER_OK);
	nbl_info(common, NBL_DEBUG_RESOURCE, "nbl virtio device driver ok.\n");
}

static u16 nbl_res_get_global_vector(void *priv, u16 vsi_id, u16 local_vector_id)
{
	/**
	 * Interrupt mapping is controlled by the emulator
	 * and the driver directly returns the local value
	 */
	return local_vector_id;
}

static int nbl_res_setup_queue(void *priv, struct nbl_txrx_queue_param *param, bool is_tx)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u16 queue_index;
	u32 desc_addr_lo = (u32)param->dma;
	u32 desc_addr_hi = param->dma >> 32;

	if (is_tx)
		queue_index = param->local_queue_id * 2 + 1;
	else
		queue_index = param->local_queue_id * 2;

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_select),
				&queue_index, sizeof(u16));
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_desc_lo),
				&desc_addr_lo, sizeof(u32));
	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_desc_hi),
				&desc_addr_hi, sizeof(u32));

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_size),
				&param->desc_num, sizeof(u16));

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_msix_vector),
				&param->global_vector_id, sizeof(u16));

	phy_ops->set_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_enable),
				&param->intr_en, sizeof(u16));

	phy_ops->get_common_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				offsetof(struct nbl_pci_common_cfg, queue_enable),
				&param->intr_en, sizeof(u16));

	return 0;
}

static int nbl_res_add_macvlan(void *priv, u8 *mac, u16 vlan, u16 vsi)
{
	struct nbl_resource_mgt_virtio *res_mgt_virtio = (struct nbl_resource_mgt_virtio *)priv;
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		phy_ops->set_device_cfg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					offsetof(struct nbl_pci_net_config, mac) + i,
					&mac[i], sizeof(u8));

	return 0;
}

static struct nbl_resource_ops res_ops = {
	.register_net			= nbl_res_register_net,
	.register_rdma			= nbl_res_register_rdma,
	.get_vsi_id			= nbl_res_get_vsi_id,
	.get_msix_entry_id		= nbl_res_get_msix_entry_id,
	.configure_virtio_dev_msix	= nbl_res_configure_virtio_dev_msix,
	.configure_rdma_msix_off	= nbl_res_configure_rdma_msix_off,
	.configure_virtio_dev_ready	= nbl_res_configure_virtio_dev_ready,
	.get_global_vector		= nbl_res_get_global_vector,
	.setup_queue			= nbl_res_setup_queue,
	.add_macvlan			= nbl_res_add_macvlan,
	.get_product_flex_cap		= nbl_res_get_flex_capability,
	.get_product_fix_cap		= nbl_res_get_fix_capability,
};

static bool is_ops_inited;

static int nbl_res_setup_res_mgt(struct nbl_common_info *common,
				 struct nbl_resource_mgt_virtio **res_mgt_virtio)
{
	struct device *dev;
	struct nbl_resource_info *resource_info;

	dev = NBL_COMMON_TO_DEV(common);
	*res_mgt_virtio = devm_kzalloc(dev, sizeof(struct nbl_resource_mgt_virtio), GFP_KERNEL);
	if (!*res_mgt_virtio)
		return -ENOMEM;
	NBL_RES_MGT_TO_COMMON(&(*res_mgt_virtio)->res_mgt) = common;

	resource_info = devm_kzalloc(dev, sizeof(struct nbl_resource_info), GFP_KERNEL);
	if (!resource_info)
		return -ENOMEM;
	NBL_RES_MGT_TO_RES_INFO(&(*res_mgt_virtio)->res_mgt) = resource_info;

	return 0;
}

static void nbl_res_remove_res_mgt(struct nbl_common_info *common,
				   struct nbl_resource_mgt_virtio **res_mgt_virtio)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, NBL_RES_MGT_TO_RES_INFO(&(*res_mgt_virtio)->res_mgt));
	devm_kfree(dev, *res_mgt_virtio);
	*res_mgt_virtio = NULL;
}

static int nbl_res_start(struct nbl_resource_mgt_virtio *res_mgt_virtio,
			 struct nbl_func_caps caps)
{
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;
	int ret = 0;

	ret = nbl_txrx_mgt_start(res_mgt);
	if (ret)
		return ret;

	nbl_res_set_fix_capability(res_mgt, NBL_VIRTIO_CAP);

	return 0;
}

static void nbl_res_stop(struct nbl_resource_mgt_virtio *res_mgt_virtio)
{
	struct nbl_resource_mgt *res_mgt = &res_mgt_virtio->res_mgt;

	nbl_txrx_mgt_stop(res_mgt);
}

static void nbl_res_remove_ops(struct device *dev, struct nbl_resource_ops_tbl **res_ops_tbl)
{
	devm_kfree(dev, *res_ops_tbl);
	*res_ops_tbl = NULL;
}

static int nbl_res_setup_ops(struct device *dev, struct nbl_resource_ops_tbl **res_ops_tbl,
			     struct nbl_resource_mgt_virtio *res_mgt_virtio)
{
	int ret;

	*res_ops_tbl = devm_kzalloc(dev, sizeof(struct nbl_resource_ops_tbl), GFP_KERNEL);
	if (!*res_ops_tbl)
		return -ENOMEM;

	if (!is_ops_inited) {
		ret = nbl_txrx_setup_ops(&res_ops);
		if (ret)
			goto setup_fail;
		is_ops_inited = true;
	}

	NBL_RES_OPS_TBL_TO_OPS(*res_ops_tbl) = &res_ops;
	NBL_RES_OPS_TBL_TO_PRIV(*res_ops_tbl) = res_mgt_virtio;

	return 0;

setup_fail:
	nbl_res_remove_ops(dev, res_ops_tbl);
	return -EAGAIN;
}

int nbl_res_init_virtio(void *p, struct nbl_init_param *param)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct device *dev;
	struct nbl_common_info *common;
	struct nbl_resource_mgt_virtio **res_mgt_virtio;
	struct nbl_resource_ops_tbl **res_ops_tbl;
	struct nbl_phy_ops_tbl *phy_ops_tbl;
	int ret = 0;

	dev = NBL_ADAPTER_TO_DEV(adapter);
	common = NBL_ADAPTER_TO_COMMON(adapter);
	res_mgt_virtio = (struct nbl_resource_mgt_virtio **)&NBL_ADAPTER_TO_RES_MGT(adapter);
	res_ops_tbl = &NBL_ADAPTER_TO_RES_OPS_TBL(adapter);
	phy_ops_tbl = NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);

	ret = nbl_res_setup_res_mgt(common, res_mgt_virtio);
	if (ret)
		goto setup_mgt_fail;

	NBL_RES_MGT_TO_PHY_OPS_TBL(&(*res_mgt_virtio)->res_mgt) = phy_ops_tbl;

	ret = nbl_res_start(*res_mgt_virtio, param->caps);
	if (ret)
		goto start_fail;

	ret = nbl_res_setup_ops(dev, res_ops_tbl, *res_mgt_virtio);
	if (ret)
		goto setup_ops_fail;

	return 0;

setup_ops_fail:
	nbl_res_stop(*res_mgt_virtio);
start_fail:
	nbl_res_remove_res_mgt(common, res_mgt_virtio);
setup_mgt_fail:
	return ret;
}

void nbl_res_remove_virtio(void *p)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct device *dev;
	struct nbl_common_info *common;
	struct nbl_resource_mgt_virtio **res_mgt_virtio;
	struct nbl_resource_ops_tbl **res_ops_tbl;

	dev = NBL_ADAPTER_TO_DEV(adapter);
	common = NBL_ADAPTER_TO_COMMON(adapter);
	res_mgt_virtio = (struct nbl_resource_mgt_virtio **)&NBL_ADAPTER_TO_RES_MGT(adapter);
	res_ops_tbl = &NBL_ADAPTER_TO_RES_OPS_TBL(adapter);

	nbl_res_remove_ops(dev, res_ops_tbl);
	nbl_res_stop(*res_mgt_virtio);
	nbl_res_remove_res_mgt(common, res_mgt_virtio);
}
