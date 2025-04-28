// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_flow_bootis.h"
#include "nbl_resource_bootis.h"

//void nbl_set_up_vsi_port_tbl(struct nbl_hw *hw,
//			     int vsi,
//			     union nbl_up_vsi_action *value)
//{
//	wr32_for_each(hw, UP_VSI_ACTION_TBL_DW_SIZE, UP_VSI_ACTION_TABLE(vsi), value->data);
//}
//
//void nbl_set_down_vsi_port_tbl(struct nbl_hw *hw,
//			       int vsi,
//			       union nbl_down_vsi_action *action)
//{
//	wr32_for_each(hw, DN_VSI_ACTION_TBL_DW_SIZE,
//		      DOWN_VSI_ACTION_TABLE(vsi), action->data);
//}

static void flow_memt_key_init(union nbl_cpu_memt_key_reg *memt_key,
			       u16 eth_id,
			       const u8 *mac,
			       u16 vlan,
			       u8 dir_down)
{
	memset(memt_key, 0, sizeof(*memt_key));
	if (dir_down)
		memt_key->key.direction_down = 1;

	memt_key->key.eth_id = eth_id;
	memt_key->key.mac0 = mac[0];
	memt_key->key.mac1 = mac[1];
	memt_key->key.mac2 = mac[2];
	memt_key->key.mac3_l = mac[3];
	memt_key->key.mac3_h = mac[3] >> 4;
	memt_key->key.mac4 = mac[4];
	memt_key->key.mac5 = mac[5];
	memt_key->key.vid = vlan;
}

static int flow_del_mv_tbl(struct nbl_resource_mgt *res_mgt,
			   u8 *mac, u16 vlan, u8 eth, u8 dir_down)
{
	union nbl_cpu_memt_key_reg memt_key;
	struct nbl_phy_ops *phy_ops;

	phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	flow_memt_key_init(&memt_key, eth, mac, vlan, dir_down);

	return phy_ops->del_mv_tbl(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), &memt_key);
}

static int flow_macvlan_up_del(struct nbl_resource_mgt *res_mgt, u8 *mac, u16 vlan, u8 eth)
{
	struct nbl_flow_mgt *flow_mgt = NBL_RES_MGT_TO_FLOW_MGT(res_mgt);
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	struct nbl_flow_macvlan_node_data *rule_data;
	void *mac_hash_tbl;
	int ret;

	nbl_debug(common, NBL_DEBUG_FLOW, "Delete up mac-vlan for mac %pM, vlan 0x%04x, eth %u.\n",
		  mac, vlan, eth);

	mac_hash_tbl = flow_mgt->mac_hash_tbl[0];
	rule_data = nbl_common_get_hash_xy_node(mac_hash_tbl, mac, &vlan);
	if (!rule_data)
		return -EAGAIN;

	ret = flow_del_mv_tbl(res_mgt, mac, vlan, eth, !NBL_MV_DIRECTION_DOWN);
	if (ret) {
		pr_err("Delete hw failed for mac %pM, vlan 0x%04x, eth %u.\n",
		       mac, vlan, eth);
		return -EAGAIN;
	}

	clear_bit(rule_data->flow_id, flow_mgt->flow_id_bitmap);
	nbl_common_free_hash_xy_node(mac_hash_tbl, mac, &vlan);

	return 0;
}

static int flow_macvlan_up_add(struct nbl_resource_mgt *res_mgt,
			       u8 *mac, u16 vlan, u8 eth, u16 vsi)
{
	struct nbl_phy_ops *phy_ops;
	struct nbl_flow_mgt *flow_mgt;
	struct nbl_common_info *common;
	union nbl_mac_result_reg up_result;
	union nbl_cpu_memt_key_reg memt_key;
	struct nbl_flow_macvlan_node_data rule_data = {0};
	struct nbl_flow_macvlan_node_data *rule_data_tmp;
	void *mac_hash_tbl;
	int ret;
	u16 flow_id, result_tbl;
	u16 node_num;

	phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	flow_mgt = NBL_RES_MGT_TO_FLOW_MGT(res_mgt);
	common = NBL_RES_MGT_TO_COMMON(res_mgt);

	nbl_debug(common, NBL_DEBUG_FLOW, "Add up mac-vlan for mac %pM, vlan 0x%04x, eth %u, vsi %u.\n",
		  mac, vlan, eth, vsi);

	mac_hash_tbl = flow_mgt->mac_hash_tbl[0];
	rule_data_tmp = nbl_common_get_hash_xy_node(mac_hash_tbl, mac, &vlan);
	if (rule_data_tmp) {
		pr_err("The mac %pM, vlan 0x%04x table existed, vsi %u, eth %u.\n",
		       mac, vlan, vsi, eth);
		return -EEXIST;
	}

	node_num = nbl_common_get_hash_xy_node_num(mac_hash_tbl);
	if (node_num >= flow_mgt->unicast_mac_threshold)
		return -ENOSPC;

	flow_id = (typeof(flow_id))find_first_zero_bit(flow_mgt->flow_id_bitmap,
						       NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM);
	if (flow_id >= NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM)
		return -EAGAIN;

	memset(&up_result, 0, sizeof(up_result));
	memset(&memt_key, 0, sizeof(memt_key));
	/* straight */
	up_result.info.up_result.straight = 1;
	/* dport */
	up_result.info.up_result.dport = NBL_DPORT_TO_ECPU;
	/* dport_id */
	up_result.info.up_result.dport_id = vsi;
	up_result.info.up_result.dport_id_high = (vsi >> 9) & 0x3;
	up_result.info.up_result.rss = 1;

	flow_memt_key_init(&memt_key, eth, mac, vlan, !NBL_MV_DIRECTION_DOWN);

	result_tbl = NBL_BOOTIS_MACVLAN_RESULT_TBL_BASE(eth) + flow_id;
	ret = phy_ops->add_mv_tbl(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
				  vsi, &memt_key, &up_result, result_tbl);
	if (ret) {
		pr_err("cfg hw failed for mac %pM, vlan 0x%04x, vsi %u, eth %u.\n",
		       mac, vlan, vsi, eth);
		goto err;
	}

	rule_data.vsi = vsi;
	rule_data.flow_id = flow_id;
	ret = nbl_common_alloc_hash_xy_node(mac_hash_tbl, mac, &vlan, &rule_data);
	if (ret)
		goto err;

	set_bit(flow_id, flow_mgt->flow_id_bitmap);

	return 0;
err:
	flow_del_mv_tbl(res_mgt, mac, vlan, NBL_RES_MGT_TO_COMMON(res_mgt)->eth_id,
			!NBL_MV_DIRECTION_DOWN);

	return -EFAULT;
}

static void nbl_flow_del_macvlan(void *priv, u8 *mac, u16 vlan, u16 vsi)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;

	flow_macvlan_up_del(res_mgt, mac, vlan, NBL_RES_MGT_TO_COMMON(res_mgt)->eth_id);
}

static void nbl_flow_macvlan_node_del_action_func(void *priv, void *x_key, void *y_key,
						  void *data)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	u8 *mac = (u8 *)x_key;
	u16 vlan = *(u16 *)y_key;

	flow_del_mv_tbl(res_mgt, mac, vlan, NBL_RES_MGT_TO_COMMON(res_mgt)->eth_id,
			!NBL_MV_DIRECTION_DOWN);
}

static int nbl_flow_add_macvlan(void *priv, u8 *mac, u16 vlan, u16 vsi)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	int ret = 0;

	ret = flow_macvlan_up_add(res_mgt, mac, vlan, NBL_RES_MGT_TO_COMMON(res_mgt)->eth_id, vsi);

	return ret;
}

#define NBL_FLOW_OPS_TBL								\
do {											\
	NBL_FLOW_SET_OPS(add_macvlan, nbl_flow_add_macvlan);				\
	NBL_FLOW_SET_OPS(del_macvlan, nbl_flow_del_macvlan);				\
} while (0)

static void nbl_flow_remove_mgt(struct device *dev, struct nbl_resource_mgt *res_mgt)
{
	struct nbl_flow_mgt *flow_mgt = NBL_RES_MGT_TO_FLOW_MGT(res_mgt);
	struct nbl_hash_xy_tbl_del_key del_key;

	NBL_HASH_XY_TBL_DEL_KEY_INIT(&del_key, res_mgt, &nbl_flow_macvlan_node_del_action_func);
	nbl_common_remove_hash_xy_table(flow_mgt->mac_hash_tbl[0], &del_key);

	devm_kfree(dev, flow_mgt->flow_id_bitmap);
	devm_kfree(dev, flow_mgt);
	NBL_RES_MGT_TO_FLOW_MGT(res_mgt) = NULL;
}

static int nbl_flow_setup_mgt(struct device *dev, struct nbl_resource_mgt *res_mgt)
{
	struct nbl_flow_mgt *flow_mgt;
	struct nbl_hash_xy_tbl_key macvlan_tbl_key;

	flow_mgt = devm_kzalloc(dev, sizeof(struct nbl_flow_mgt), GFP_KERNEL);
	if (!flow_mgt)
		return -ENOMEM;

	NBL_RES_MGT_TO_FLOW_MGT(res_mgt) = flow_mgt;

	flow_mgt->flow_id_bitmap = devm_kcalloc(dev,
						BITS_TO_LONGS(NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM),
						sizeof(long), GFP_KERNEL);
	if (!flow_mgt->flow_id_bitmap)
		goto settup_mgt_failed;

	NBL_HASH_XY_TBL_KEY_INIT(&macvlan_tbl_key, dev, ETH_ALEN, sizeof(u16),
				 sizeof(struct nbl_flow_macvlan_node_data),
				 NBL_MACVLAN_TBL_BUCKET_SIZE, NBL_MACVLAN_X_AXIS_BUCKET_SIZE,
				 NBL_MACVLAN_Y_AXIS_BUCKET_SIZE, false);
	flow_mgt->mac_hash_tbl[0] = nbl_common_init_hash_xy_table(&macvlan_tbl_key);
	if (!flow_mgt->mac_hash_tbl[0])
		goto settup_mgt_failed;

	flow_mgt->unicast_mac_threshold = NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM;

	return 0;

settup_mgt_failed:
	nbl_flow_remove_mgt(dev, res_mgt);
	return -1;
}

int nbl_flow_mgt_start_bootis(struct nbl_resource_mgt *res_mgt)
{
	struct device *dev;
	int ret = 0;

	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	ret = nbl_flow_setup_mgt(dev, res_mgt);
	if (ret)
		goto setup_mgt_fail;

	return 0;

setup_mgt_fail:
	return ret;
}

void nbl_flow_mgt_stop_bootis(struct nbl_resource_mgt *res_mgt)
{
	struct device *dev;
	struct nbl_flow_mgt **flow_mgt;

	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	flow_mgt = &NBL_RES_MGT_TO_FLOW_MGT(res_mgt);

	if (!(*flow_mgt))
		return;

	nbl_flow_remove_mgt(dev, res_mgt);
}

int nbl_flow_setup_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_FLOW_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = func; ; } while (0)
	NBL_FLOW_OPS_TBL;
#undef  NBL_FLOW_SET_OPS

	return 0;
}

void nbl_flow_remove_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_FLOW_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = NULL; ; } while (0)
	NBL_FLOW_OPS_TBL;
#undef  NBL_FLOW_SET_OPS
}
