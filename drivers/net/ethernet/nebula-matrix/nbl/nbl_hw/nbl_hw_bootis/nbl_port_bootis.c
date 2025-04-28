// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_port_bootis.h"
#include "nbl_resource_bootis.h"

static int nbl_port_init_bootis(void *priv)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	int ret = 0;

	ret = phy_ops->init_port(NBL_RES_MGT_TO_PHY_PRIV(res_mgt));
	if (ret)
		return ret;
	ret = phy_ops->init_fec(NBL_RES_MGT_TO_PHY_PRIV(res_mgt));
	if (ret)
		return ret;
	return ret;
}

static int nbl_port_get_port_state(void *priv, u8 eth_id, struct nbl_port_state *port_state)
{
//	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
//	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	port_state->link_speed = SPEED_100000;
	port_state->port_type = NBL_PORT_TYPE_FIBRE;
	port_state->port_max_rate = NBL_PORT_MAX_RATE_100G;

	return 0;
}

static int nbl_port_setup_loopback(void *priv, u32 eth_id, u32 enable)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);

	return phy_ops->setup_loopback(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id, enable);
}

static int nbl_port_get_module_info(void *priv, u8 eth_id, struct ethtool_modinfo *info)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	/*struct nbl_eth_info *eth_info = NBL_RES_MGT_TO_ETH_INFO(res_mgt);*/
	struct device *dev = NBL_COMMON_TO_DEV(res_mgt->common);

	if (!phy_ops->sfp_is_present(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id)) {
		dev_err(dev, "SFP module of ETH port %u is not present.\n", eth_id);
		return -ENXIO;
	}

	/* only support SFF-8636 */
	info->type = ETH_MODULE_SFF_8636;
	info->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;

	return 0;
}

static int nbl_port_get_module_eeprom(void *priv, u8 eth_id,
				      struct ethtool_eeprom *eeprom, u8 *data)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	struct nbl_common_info *common = NBL_RES_MGT_TO_COMMON(res_mgt);
	u8 channel = common->function;
	int ret;
	u32 rdata32;
	int i = 0;

	if (eeprom->len == 0)
		return -EINVAL;

	for (i = eeprom->offset; i < eeprom->offset + eeprom->len; i += NBL_I2C_READ_MAXLEN) {
		ret = phy_ops->read_i2c(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id,
					NBL_SFF_8636_SLAVE_ADDR,
					channel, NBL_I2C_READ_MAXLEN,
					i, &rdata32);

		if (ret) {
			pr_err("Read SFP module eeprom at offset %d failed, rdata32 0x%04x.\n",
			       i, rdata32);
			return -EIO;
		}

		memcpy(data + i - eeprom->offset, &rdata32, NBL_I2C_READ_MAXLEN);
	}

	return NBL_OK;
}

#define ADD_STATISTICS(name, addr) \
	{#name, (addr)}

static struct nbl_eth_statistics_info _eth_statistics[] = {
	ADD_STATISTICS(tx_frames_less_than_64B_err,		0x800),
	ADD_STATISTICS(tx_frames_oversized_err,			0x802),
	ADD_STATISTICS(tx_frames_fcs_err,			0x804),
	ADD_STATISTICS(tx_frames_crc_err,			0x806),
	ADD_STATISTICS(tx_frames_mcast_data_err,		0x808),
	ADD_STATISTICS(tx_frames_bcast_data_err,		0x80a),
	ADD_STATISTICS(tx_frames_ucast_data_err,		0x80c),
	ADD_STATISTICS(tx_frames_mcast_ctrl_err,		0x80e),
	ADD_STATISTICS(tx_frames_bcast_ctrl_err,		0x810),
	ADD_STATISTICS(tx_frames_ucast_ctrl_err,		0x812),
	ADD_STATISTICS(tx_frames_pause_err,			0x814),
	ADD_STATISTICS(tx_frames_64B,				0x816),
	ADD_STATISTICS(tx_frames_65B_to_127B,			0x818),
	ADD_STATISTICS(tx_frames_128B_to_255B,			0x81a),
	ADD_STATISTICS(tx_frames_256B_to_511B,			0x81c),
	ADD_STATISTICS(tx_frames_512B_to_1023B,			0x81e),
	ADD_STATISTICS(tx_frames_1024B_to_1518B,		0x820),
	ADD_STATISTICS(tx_frames_1519B_to_MAXB,			0x822),
	ADD_STATISTICS(tx_frames_oversize,			0x824),
	ADD_STATISTICS(tx_frames_mcast_data_ok,			0x826),
	ADD_STATISTICS(tx_frames_bcast_data_ok,			0x828),
	ADD_STATISTICS(tx_frames_ucast_data_ok,			0x82a),
	ADD_STATISTICS(tx_frames_mcast_ctrl_ok,			0x82c),
	ADD_STATISTICS(tx_frames_bcast_ctrl_ok,			0x82e),
	ADD_STATISTICS(tx_frames_ucast_ctrl_ok,			0x830),
	ADD_STATISTICS(tx_frames_pause_ok,			0x832),
	ADD_STATISTICS(tx_frames_pnt,				0x834),
	ADD_STATISTICS(tx_frames_starts,			0x836),
	ADD_STATISTICS(tx_frames_length_err,			0x838),
	ADD_STATISTICS(tx_frames_pfc_crc_err,			0x83a),
	ADD_STATISTICS(tx_frames_pfc_ok,			0x83c),
	ADD_STATISTICS(tx_payload_octets_ok,			0x860),
	ADD_STATISTICS(tx_frame_octets_ok,			0x862),
	ADD_STATISTICS(tx_frames_malformed_ctrl,		0x864),
	ADD_STATISTICS(tx_frames_dropped_ctrl,			0x866),
	ADD_STATISTICS(tx_frames_badLt_ctrl,			0x868),
	ADD_STATISTICS(rx_frames_less_than_64B_err,		0x900),
	ADD_STATISTICS(rx_frames_oversized_err,			0x902),
	ADD_STATISTICS(rx_frames_fcs_err,			0x904),
	ADD_STATISTICS(rx_frames_crc_err,			0x906),
	ADD_STATISTICS(rx_frames_mcast_data_err,		0x908),
	ADD_STATISTICS(rx_frames_bcast_data_err,		0x90a),
	ADD_STATISTICS(rx_frames_ucast_data_err,		0x90c),
	ADD_STATISTICS(rx_frames_mcast_ctrl_err,		0x90e),
	ADD_STATISTICS(rx_frames_bcast_ctrl_err,		0x910),
	ADD_STATISTICS(rx_frames_ucast_ctrl_err,		0x912),
	ADD_STATISTICS(rx_frames_pause_err,			0x914),
	ADD_STATISTICS(rx_frames_64B,				0x916),
	ADD_STATISTICS(rx_frames_65B_to_127B,			0x918),
	ADD_STATISTICS(rx_frames_128B_to_255B,			0x91a),
	ADD_STATISTICS(rx_frames_256B_to_511B,			0x91c),
	ADD_STATISTICS(rx_frames_512B_to_1023B,			0x91e),
	ADD_STATISTICS(rx_frames_1024B_to_1518B,		0x920),
	ADD_STATISTICS(rx_frames_1519B_to_MAXB,			0x922),
	ADD_STATISTICS(rx_frames_oversize,			0x924),
	ADD_STATISTICS(rx_frames_mcast_data_ok,			0x926),
	ADD_STATISTICS(rx_frames_bcast_data_ok,			0x928),
	ADD_STATISTICS(rx_frames_ucast_data_ok,			0x92a),
	ADD_STATISTICS(rx_frames_mcast_ctrl_ok,			0x92c),
	ADD_STATISTICS(rx_frames_bcast_ctrl_ok,			0x92e),
	ADD_STATISTICS(rx_frames_ucast_ctrl_ok,			0x930),
	ADD_STATISTICS(rx_frames_pause_ok,			0x932),
	ADD_STATISTICS(rx_frames_pnt,				0x934),
	ADD_STATISTICS(rx_frames_starts,			0x936),
	ADD_STATISTICS(rx_frames_length_err,			0x938),
	ADD_STATISTICS(rx_frames_pfc_crc_err,			0x93a),
	ADD_STATISTICS(rx_frames_pfc_ok,			0x93c),
	ADD_STATISTICS(rx_payload_octets_ok,			0x960),
	ADD_STATISTICS(rx_frame_octets_ok,			0x962),
};

static void nbl_port_get_private_stat_len(void *priv, u32 *len)
{
	*len = ARRAY_SIZE(_eth_statistics);
}

static void nbl_port_get_private_stat_data(void *priv, u32 eth_id, u64 *data)
{
	struct nbl_resource_mgt *res_mgt = (struct nbl_resource_mgt *)priv;
	struct nbl_phy_ops *phy_ops = NBL_RES_MGT_TO_PHY_OPS(res_mgt);
	u32 stat_l, stat_h;
	int i;

	phy_ops->set_eth_stats_snapshot(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id, 1);

	for (i = 0; i < ARRAY_SIZE(_eth_statistics); i++) {
		phy_ops->get_eth_ip_reg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					eth_id, _eth_statistics[i].addr, &stat_l);
		phy_ops->get_eth_ip_reg(NBL_RES_MGT_TO_PHY_PRIV(res_mgt),
					eth_id, _eth_statistics[i].addr + 1, &stat_h);
		data[i] = ((u64)stat_h << 32) | stat_l;
	}
	phy_ops->set_eth_stats_snapshot(NBL_RES_MGT_TO_PHY_PRIV(res_mgt), eth_id, 0);
}

static void nbl_port_fill_private_stat_strings(void *priv, u8 *strings)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_eth_statistics); i++) {
		snprintf(strings, ETH_GSTRING_LEN, "%s", _eth_statistics[i].descp);
		strings += ETH_GSTRING_LEN;
	}
}

#define NBL_PORT_OPS_TBL								\
do {											\
	NBL_PORT_SET_OPS(init_port, nbl_port_init_bootis);				\
	NBL_PORT_SET_OPS(get_port_state, nbl_port_get_port_state);			\
	NBL_PORT_SET_OPS(setup_loopback, nbl_port_setup_loopback);			\
	NBL_PORT_SET_OPS(get_module_info, nbl_port_get_module_info);			\
	NBL_PORT_SET_OPS(get_module_eeprom, nbl_port_get_module_eeprom);		\
	NBL_PORT_SET_OPS(get_private_stat_len, nbl_port_get_private_stat_len);		\
	NBL_PORT_SET_OPS(get_private_stat_data, nbl_port_get_private_stat_data);	\
	NBL_PORT_SET_OPS(fill_private_stat_strings, nbl_port_fill_private_stat_strings);\
} while (0)

int nbl_port_setup_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_PORT_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = func; ; } while (0)
	NBL_PORT_OPS_TBL;
#undef  NBL_PORT_SET_OPS

	return 0;
}

void nbl_port_remove_ops_bootis(struct nbl_resource_ops *res_ops)
{
#define NBL_PORT_SET_OPS(name, func) do {res_ops->NBL_NAME(name) = NULL; ; } while (0)
	NBL_PORT_OPS_TBL;
#undef  NBL_PORT_SET_OPS
}

static int nbl_port_setup_mgt(struct device *dev, struct nbl_port_mgt **port_mgt)
{
	*port_mgt = devm_kzalloc(dev, sizeof(struct nbl_port_mgt), GFP_KERNEL);
	if (!*port_mgt)
		return -ENOMEM;

	return 0;
}

static void nbl_port_remove_mgt(struct device *dev, struct nbl_port_mgt **port_mgt)
{
	devm_kfree(dev, *port_mgt);
	*port_mgt = NULL;
}

int nbl_port_mgt_start_bootis(struct nbl_resource_mgt *res_mgt)
{
	struct device *dev;
	struct nbl_port_mgt **port_mgt;

	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	port_mgt = &NBL_RES_MGT_TO_PORT_MGT(res_mgt);

	return nbl_port_setup_mgt(dev, port_mgt);
}

void nbl_port_mgt_stop_bootis(struct nbl_resource_mgt *res_mgt)
{
	struct device *dev;
	struct nbl_port_mgt **port_mgt;

	dev = NBL_RES_MGT_TO_DEV(res_mgt);
	port_mgt = &NBL_RES_MGT_TO_PORT_MGT(res_mgt);

	if (!(*port_mgt))
		return;

	nbl_port_remove_mgt(dev, port_mgt);
}
