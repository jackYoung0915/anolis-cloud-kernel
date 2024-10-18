// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include "txgbe_vf.h"
#include "txgbe_mbx.h"

static const char txgbe_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"Link test   (on/offline)"
};

#define TXGBE_TEST_LEN (sizeof(txgbe_gstrings_test) / ETH_GSTRING_LEN)

struct txgbe_stat {
	char name[ETH_GSTRING_LEN];
	int size;
	int offset;
};

struct txgbe_queue_stats {
	u64 packets;
	u64 bytes;
};

#define TXGBE_STAT(_name, _stru, _stat) { \
	.name = _name, \
	.size = sizeof_field(_stru, _stat), \
	.offset = offsetof(_stru, _stat), \
}

#define TXGBE_NET_STAT(_stat) \
		TXGBE_STAT("net."#_stat, txgbe_net_stats_t, _stat)
static const struct txgbe_stat txgbe_net_stats[] = {
//	TXGBE_NET_STAT(rx_packets),
//	TXGBE_NET_STAT(tx_packets),
//	TXGBE_NET_STAT(rx_bytes),
//	TXGBE_NET_STAT(tx_bytes),
//	TXGBE_NET_STAT(multicast),
};

#define TXGBE_NET_STATS_LEN   ARRAY_SIZE(txgbe_net_stats)

#define TXGBE_SW_STAT(_name, _stat) \
		TXGBE_STAT("sw." _name, struct txgbe_adapter, _stat)
static struct txgbe_stat txgbe_sw_stats[] = {
	TXGBE_SW_STAT("tx_busy",              sw_stats.tx_busy),
	TXGBE_SW_STAT("tx_restart_queue",     sw_stats.tx_restart_queue),
	TXGBE_SW_STAT("tx_timeout_count",     sw_stats.tx_timeout_count),

	TXGBE_SW_STAT("rx_csum_bad",          sw_stats.rx_csum_bad),
	TXGBE_SW_STAT("rx_no_dma_resources",  sw_stats.rx_no_dma_resources),
	TXGBE_SW_STAT("rx_alloc_page_failed", sw_stats.rx_alloc_page_failed),
	TXGBE_SW_STAT("rx_alloc_buff_failed", sw_stats.rx_alloc_buff_failed),
};

#define TXGBE_SW_STATS_LEN	ARRAY_SIZE(txgbe_sw_stats)

#define TXGBE_HW_STAT(_name, _stat) \
		TXGBE_STAT("hw." _name, struct txgbe_adapter, _stat)

static struct txgbe_stat txgbe_hw_stats[] = {
	TXGBE_HW_STAT("rx_packets",       stats.gprc),
	TXGBE_HW_STAT("tx_packets",       stats.gptc),
	TXGBE_HW_STAT("rx_bytes",       stats.gorc),
	TXGBE_HW_STAT("tx_bytes",       stats.gotc),
	TXGBE_HW_STAT("multicast",       stats.mprc),
	TXGBE_HW_STAT("last_gprc",  last_stats.gprc),
	TXGBE_HW_STAT("last_gptc",  last_stats.gptc),
	TXGBE_HW_STAT("last_gorc",  last_stats.gorc),
	TXGBE_HW_STAT("last_gotc",  last_stats.gotc),
	TXGBE_HW_STAT("last_mprc",  last_stats.mprc),
	TXGBE_HW_STAT("base_gprc",  base_stats.gprc),
	TXGBE_HW_STAT("base_gptc",  base_stats.gptc),
	TXGBE_HW_STAT("base_gorc",  base_stats.gorc),
	TXGBE_HW_STAT("base_gotc",  base_stats.gotc),
	TXGBE_HW_STAT("base_mprc",  base_stats.mprc),
	TXGBE_HW_STAT("reset_gprc", reset_stats.gprc),
	TXGBE_HW_STAT("reset_gptc", reset_stats.gptc),
	TXGBE_HW_STAT("reset_gorc", reset_stats.gorc),
	TXGBE_HW_STAT("reset_gotc", reset_stats.gotc),
	TXGBE_HW_STAT("reset_mprc", reset_stats.mprc),
};

#define TXGBE_HW_STATS_LEN	ARRAY_SIZE(txgbe_hw_stats)

#define TXGBE_QUEUE_STATS_LEN \
	   ((((struct txgbe_adapter *)netdev_priv(netdev))->num_tx_queues \
	      + ((struct txgbe_adapter *)netdev_priv(netdev))->num_rx_queues) \
	      * (sizeof(struct txgbe_queue_stats) / sizeof(u64)))

#define TXGBE_STATS_LEN (TXGBE_NET_STATS_LEN \
			 + TXGBE_SW_STATS_LEN \
			 + TXGBE_HW_STATS_LEN \
			 + TXGBE_QUEUE_STATS_LEN)

static int txgbevf_get_link_ksettings(struct net_device *netdev,
				      struct ethtool_link_ksettings *cmd)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	struct txgbe_hw *hw = &adapter->hw;
	u32 link_speed = 0;
	bool link_up = false;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported,
					     10000baseT_Full);
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = -1;

	if (!in_interrupt()) {
		hw->mac.get_link_status = 1;
		hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
	} else {
		/* this case is a special workaround for RHEL5 bonding
		 * that calls this routine from interrupt context
		 */
		link_speed = adapter->link_speed;
		link_up = adapter->link_up;
	}

	if (link_up) {
		__u32 speed = SPEED_10000;

		switch (link_speed) {
		case TXGBE_LINK_SPEED_10GB_FULL:
			speed = SPEED_10000;
			break;
		case TXGBE_LINK_SPEED_1GB_FULL:
			speed = SPEED_1000;
			break;
		case TXGBE_LINK_SPEED_100_FULL:
			speed = SPEED_100;
			break;
		}

		cmd->base.speed = speed;
		cmd->base.duplex = DUPLEX_FULL;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static void txgbe_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, txgbevf_driver_name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, txgbevf_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, txgbe_firmware_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}

static int txgbevf_set_link_ksettings(struct net_device __always_unused *netdev,
				      const struct ethtool_link_ksettings __always_unused *cmd)
{
	return -EINVAL;
}

#define TXGBE_REGS_LEN  45
static int txgbe_get_regs_len(struct net_device __always_unused *netdev)
{
	return TXGBE_REGS_LEN * sizeof(u32);
}

#define TXGBE_GET_STAT(_A_, _R_) ((_A_)->stats.(_R_))

static void txgbe_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			   void *p)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	struct txgbe_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, TXGBE_REGS_LEN * sizeof(u32));

	regs->version = (1 << 24) | hw->revision_id << 16 | hw->device_id;

	/* TXGBE_VFCTRL is a Write Only register, so just return 0 */
	regs_buff[0] = 0x0;

	/* General Registers */
	regs_buff[1] = rd32(hw, TXGBE_VXSTATUS);
	regs_buff[3] = rd32(hw, TXGBE_VXRXMEMWRAP);

	/* Interrupt */
	regs_buff[5] = rd32(hw, TXGBE_VXICR);
	regs_buff[6] = rd32(hw, TXGBE_VXICS);
	regs_buff[7] = rd32(hw, TXGBE_VXIMS);
	regs_buff[8] = rd32(hw, TXGBE_VXIMC);
	regs_buff[11] = rd32(hw, TXGBE_VXITR(0));
	regs_buff[12] = rd32(hw, TXGBE_VXIVAR(0));
	regs_buff[13] = rd32(hw, TXGBE_VXIVAR_MISC);

	/* Receive DMA */
	for (i = 0; i < 2; i++)
		regs_buff[14 + i] = rd32(hw, TXGBE_VXRDBAL(i));
	for (i = 0; i < 2; i++)
		regs_buff[16 + i] = rd32(hw, TXGBE_VXRDBAH(i));
	for (i = 0; i < 2; i++)
		regs_buff[20 + i] = rd32(hw, TXGBE_VXRDH(i));
	for (i = 0; i < 2; i++)
		regs_buff[22 + i] = rd32(hw, TXGBE_VXRDT(i));
	for (i = 0; i < 2; i++)
		regs_buff[24 + i] = rd32(hw, TXGBE_VXRXDCTL(i));

	/* Receive */
	regs_buff[28] = rd32(hw, TXGBE_VXMRQC);

	/* Transmit */
	for (i = 0; i < 2; i++)
		regs_buff[29 + i] = rd32(hw, TXGBE_VXTDBAL(i));
	for (i = 0; i < 2; i++)
		regs_buff[31 + i] = rd32(hw, TXGBE_VXTDBAH(i));
	for (i = 0; i < 2; i++)
		regs_buff[35 + i] = rd32(hw, TXGBE_VXTDH(i));
	for (i = 0; i < 2; i++)
		regs_buff[37 + i] = rd32(hw, TXGBE_VXTDT(i));
	for (i = 0; i < 2; i++)
		regs_buff[39 + i] = rd32(hw, TXGBE_VXTXDCTL(i));
}

static int txgbe_nway_reset(struct net_device *netdev)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		txgbevf_reinit_locked(adapter);

	return 0;
}

static int txgbe_get_eeprom(struct net_device __always_unused *netdev,
			    struct ethtool_eeprom __always_unused *eeprom,
			    u8 __always_unused *bytes)
{
	return -EOPNOTSUPP;
}

static int txgbe_set_eeprom(struct net_device __always_unused *netdev,
			    struct ethtool_eeprom __always_unused *eeprom,
			    u8 __always_unused *bytes)
{
	return -EOPNOTSUPP;
}

static u32 txgbe_get_rxfh_indir_size(struct net_device *netdev)
{
	return TXGBE_VFRETA_SIZE;
}

static u32 txgbe_get_rxfh_key_size(struct net_device *netdev)
{
	return TXGBE_RSS_HASH_KEY_SIZE;
}

static int txgbevf_get_reta_locked(struct txgbe_hw *hw, u32 *reta,
				   int num_rx_queues)
{
	int err, i, j;
	u32 msgbuf[TXGBE_VXMAILBOX_SIZE];
	u32 *hw_reta = &msgbuf[1];
	u32 mask = 0;

	/* We have to use a mailbox for 82599 and x540 devices only.
	 * For these devices RETA has 128 entries.
	 * Also these VFs support up to 4 RSS queues. Therefore PF will compress
	 * 16 RETA entries in each DWORD giving 2 bits to each entry.
	 */
	int dwords = 128 / 16;

	/* We support the RSS querying for 82599 and x540 devices only.
	 * Thus return an error if API doesn't support RETA querying or querying
	 * is not supported for this device type.
	 */
	switch (hw->api_version) {
	case txgbe_mbox_api_13:
	case txgbe_mbox_api_12:
			break;
	default:
		return -EOPNOTSUPP;
	}

	msgbuf[0] = TXGBE_VF_GET_RETA;

	err = hw->mbx.ops.write_posted(hw, msgbuf, 1, 0);

	if (err)
		return err;

	err = hw->mbx.ops.read_posted(hw, msgbuf, dwords + 1, 0);

	if (err)
		return err;

	msgbuf[0] &= ~TXGBE_VT_MSGTYPE_CTS;

	/* If the operation has been refused by a PF return -EPERM */
	if (msgbuf[0] == (TXGBE_VF_GET_RETA | TXGBE_VT_MSGTYPE_NACK))
		return -EPERM;

	/* If we didn't get an ACK there must have been
	 * some sort of mailbox error so we should treat it
	 * as such.
	 */
	if (msgbuf[0] != (TXGBE_VF_GET_RETA | TXGBE_VT_MSGTYPE_ACK))
		return TXGBE_ERR_MBX;

	/* ixgbevf doesn't support more than 2 queues at the moment */
	if (num_rx_queues > 1)
		mask = 0x1;

	for (i = 0; i < dwords; i++)
		for (j = 0; j < 16; j++)
			reta[i * 16 + j] = (hw_reta[i] >> (2 * j)) & mask;

	return 0;
}

/**
 * txgbevf_get_rss_key_locked - get the RSS Random Key
 * @hw: pointer to the HW structure
 * @rss_key: buffer to fill with RSS Hash Key contents.
 *
 * The "rss_key" buffer should be big enough to contain 10 registers.
 *
 * Returns: 0 on success.
 *          if API doesn't support this operation - (-EOPNOTSUPP).
 */
static int txgbevf_get_rss_key_locked(struct txgbe_hw *hw, u8 *rss_key)
{
	int err;
	u32 msgbuf[TXGBE_VXMAILBOX_SIZE];

	/* We currently support the RSS Random Key retrieval for 82599 and x540
	 * devices only.
	 *
	 * Thus return an error if API doesn't support RSS Random Key retrieval
	 * or if the operation is not supported for this device type.
	 */
	switch (hw->api_version) {
	case txgbe_mbox_api_13:
	case txgbe_mbox_api_12:
			break;
	default:
		return -EOPNOTSUPP;
	}

	msgbuf[0] = TXGBE_VF_GET_RSS_KEY;
	err = hw->mbx.ops.write_posted(hw, msgbuf, 1, 0);

	if (err)
		return err;

	err = hw->mbx.ops.read_posted(hw, msgbuf, 11, 0);

	if (err)
		return err;

	msgbuf[0] &= ~TXGBE_VT_MSGTYPE_CTS;

	/* If the operation has been refused by a PF return -EPERM */
	if (msgbuf[0] == (TXGBE_VF_GET_RSS_KEY | TXGBE_VT_MSGTYPE_NACK))
		return -EPERM;

	/* If we didn't get an ACK there must have been
	 * some sort of mailbox error so we should treat it
	 * as such.
	 */
	if (msgbuf[0] != (TXGBE_VF_GET_RSS_KEY | TXGBE_VT_MSGTYPE_ACK))
		return TXGBE_ERR_MBX;

	memcpy(rss_key, msgbuf + 1, TXGBE_RSS_HASH_KEY_SIZE);

	return 0;
}

static int txgbe_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	int err = 0;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (adapter->hw.mac.type == 1) {
		if (key)
			memcpy(key, adapter->rss_key, txgbe_get_rxfh_key_size(netdev));

		if (indir) {
			int i;

			for (i = 0; i < TXGBE_VFRETA_SIZE; i++)
				indir[i] = adapter->rss_indir_tbl[i];
		}
	} else {
		if (!indir && !key)
			return 0;
		spin_lock_bh(&adapter->mbx_lock);
		if (indir)
			err = txgbevf_get_reta_locked(&adapter->hw, indir,
						      adapter->num_rx_queues);

		if (!err && key)
			err = txgbevf_get_rss_key_locked(&adapter->hw, key);
		spin_unlock_bh(&adapter->mbx_lock);
	}
	return err;
}

static void txgbe_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = TXGBE_MAX_RXD;
	ring->tx_max_pending = TXGBE_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int txgbe_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	struct txgbe_ring *tx_ring = NULL, *rx_ring = NULL;
	int i, err = 0;
	u32 new_rx_count, new_tx_count;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	new_rx_count = max_t(u32, ring->rx_pending, (u32)TXGBE_MIN_RXD);
	new_rx_count = min_t(u32, new_rx_count, (u32)TXGBE_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, TXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = max_t(u32, ring->tx_pending, (u32)TXGBE_MIN_TXD);
	new_tx_count = min_t(u32, new_tx_count, (u32)TXGBE_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, TXGBE_REQ_TX_DESCRIPTOR_MULTIPLE);

	if (new_tx_count == adapter->tx_ring_count &&
	    new_rx_count == adapter->rx_ring_count)
		return 0;

	while (test_and_set_bit(__TXGBE_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	/* If the adapter isn't up and running then just set the
	 * new parameters and scurry for the exits.
	 */
	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	if (new_tx_count != adapter->tx_ring_count) {
		tx_ring = vmalloc(adapter->num_tx_queues * sizeof(*tx_ring));
		if (!tx_ring) {
			err = -ENOMEM;
			goto clear_reset;
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			/* clone ring and setup updated count */
			tx_ring[i] = *adapter->tx_ring[i];
			tx_ring[i].count = new_tx_count;
			err = txgbevf_setup_tx_resources(&tx_ring[i]);
			if (err) {
				while (i) {
					i--;
					txgbevf_free_tx_resources(&tx_ring[i]);
				}

				vfree(tx_ring);
				tx_ring = NULL;

				goto clear_reset;
			}
		}
	}

	if (new_rx_count != adapter->rx_ring_count) {
		rx_ring = vmalloc(adapter->num_rx_queues * sizeof(*rx_ring));
		if (!rx_ring) {
			err = -ENOMEM;
			goto clear_reset;
		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			/* clone ring and setup updated count */
			rx_ring[i] = *adapter->rx_ring[i];
			rx_ring[i].count = new_rx_count;
			err = txgbevf_setup_rx_resources(adapter, &rx_ring[i]);
			if (err) {
				while (i) {
					i--;
					txgbevf_free_rx_resources(&rx_ring[i]);
				}

				vfree(rx_ring);
				rx_ring = NULL;

				goto clear_reset;
			}
		}
	}

	txgbevf_down(adapter);
	txgbe_free_irq(adapter);

	/* Tx */
	if (tx_ring) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			txgbevf_free_tx_resources(adapter->tx_ring[i]);
			*adapter->tx_ring[i] = tx_ring[i];
		}
		adapter->tx_ring_count = new_tx_count;

		vfree(tx_ring);
		tx_ring = NULL;
	}

	/* Rx */
	if (rx_ring) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			txgbevf_free_rx_resources(adapter->rx_ring[i]);
			*adapter->rx_ring[i] = rx_ring[i];
		}
		adapter->rx_ring_count = new_rx_count;

		vfree(rx_ring);
		rx_ring = NULL;
	}

	txgbe_configure(adapter);
	txgbe_request_irq(adapter);
	txgbe_up_complete(adapter);

clear_reset:
	/* free Tx resources if Rx error is encountered */
	if (tx_ring) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			txgbevf_free_tx_resources(&tx_ring[i]);
		vfree(tx_ring);
	}

	clear_bit(__TXGBE_RESETTING, &adapter->state);
	return err;
}

static u32 txgbe_get_msglevel(struct net_device *netdev)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void txgbe_set_msglevel(struct net_device *netdev, u32 data)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static int txgbe_link_test(struct txgbe_adapter *adapter, u64 *data)
{
	struct txgbe_hw *hw = &adapter->hw;
	bool link_up;
	u32 link_speed = 0;
	*data = 0;

	hw->mac.ops.check_link(hw, &link_speed, &link_up, true);
	if (!link_up)
		*data = 1;

	return *data;
}

/* ethtool register test data */
struct txgbe_reg_test {
	u16 reg;
	u8  array_len;
	u8  test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x40 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST    1
#define SET_READ_TEST   2
#define WRITE_NO_TEST   3
#define TABLE32_TEST    4
#define TABLE64_TEST_LO 5
#define TABLE64_TEST_HI 6

/* default VF register test */
static struct txgbe_reg_test reg_test_vf[] = {
	{ TXGBE_VXRDBAL(0), 2, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFF80 },
	{ TXGBE_VXRDBAH(0), 2, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ TXGBE_VXRDT(0), 2, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ TXGBE_VXRXDCTL(0), 2, WRITE_NO_TEST, 0, 0 },
	{ TXGBE_VXTDBAL(0), 2, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ TXGBE_VXTDBAH(0), 2, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0, 0 }
};

static int
reg_pattern_test(struct txgbe_hw *hw, u32 r, u32 m, u32 w)
{
	static const u32 _test[] = {
		0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF
	};
	u32 pat, val, before;
	struct txgbe_adapter *adapter;

	if (TXGBE_REMOVED(hw->hw_addr))
		return 1;

	adapter = hw->back;
	for (pat = 0; pat < ARRAY_SIZE(_test); pat++) {
		before = rd32(hw, r);
		wr32(hw, r, _test[pat] & w);
		val = rd32(hw, r);
		if (val != (_test[pat] & w & m)) {
			e_err(drv,
			      "pattern test reg %04X failed: got 0x%08X expected 0x%08X\n",
			      r, val, _test[pat] & w & m);
			wr32(hw, r, before);
			return 1;
		}
		wr32(hw, r, before);
	}
	return 0;
}

static int
reg_set_and_check(struct txgbe_hw *hw, u32 r, u32 m, u32 w)
{
	u32 val, before;
	struct txgbe_adapter *adapter;

	if (TXGBE_REMOVED(hw->hw_addr))
		return 1;

	adapter = hw->back;

	before = rd32(hw, r);
	wr32(hw, r, w & m);
	val = rd32(hw, r);
	if ((w & m) != (val & m)) {
		e_err(hw,
		      "set/check reg %04X test failed: got 0x%08X expected 0x%08X\n",
		      r, (val & m), (w & m));
		wr32(hw, r, before);
		return 1;
	}
	wr32(hw, r, before);

	return 0;
}

int txgbe_diag_reg_test(struct txgbe_hw *hw)
{
	struct txgbe_reg_test *test;
	int rc;
	u32 i;
	struct txgbe_adapter *adapter = hw->back;

	if (TXGBE_REMOVED(hw->hw_addr)) {
		e_err(hw, "Adapter removed - register test blocked\n");
		return 1;
	}

	test = reg_test_vf;

	/* Perform the register test, looping through the test table
	 * until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			rc = 0;
			switch (test->test_type) {
			case PATTERN_TEST:
				rc = reg_pattern_test(hw,
						      test->reg + (i * 0x40),
						      test->mask,
						      test->write);
				break;
			case SET_READ_TEST:
				rc = reg_set_and_check(hw,
						       test->reg + (i * 0x40),
						       test->mask,
						       test->write);
				break;
			case WRITE_NO_TEST:
				wr32(hw, test->reg + (i * 0x40),
				     test->write);
				break;
			case TABLE32_TEST:
				rc = reg_pattern_test(hw,
						      test->reg + (i * 4),
						      test->mask,
						      test->write);
				break;
			case TABLE64_TEST_LO:
				rc = reg_pattern_test(hw,
						      test->reg + (i * 8),
						      test->mask,
						      test->write);
				break;
			case TABLE64_TEST_HI:
				rc = reg_pattern_test(hw,
						      test->reg + 4 + (i * 8),
						      test->mask,
						      test->write);
				break;
			}
			if (rc)
				return rc;
		}
		test++;
	}

	return 0;
}

static int txgbe_reg_test(struct txgbe_adapter *adapter, u64 *data)
{
	struct txgbe_hw *hw = &adapter->hw;

	*data = txgbe_diag_reg_test(hw);

	return *data;
}

static void txgbe_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	bool if_running = netif_running(netdev);

	if (TXGBE_REMOVED(adapter->hw.hw_addr)) {
		e_err(hw, "Adapter removed - test blocked\n");
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[0] = 1;
		data[1] = 1;
		return;
	}
	set_bit(__TXGBE_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		e_info(hw, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (txgbe_link_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			txgbevf_close(netdev);
		else
			txgbevf_reset(adapter);

		e_info(hw, "register testing starting\n");
		if (txgbe_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		txgbevf_reset(adapter);

		clear_bit(__TXGBE_TESTING, &adapter->state);
		if (if_running)
			txgbevf_open(netdev);
	} else {
		e_info(hw, "online testing starting\n");
		/* Online tests */
		if (txgbe_link_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Online tests aren't run; pass by default */
		data[0] = 0;

		clear_bit(__TXGBE_TESTING, &adapter->state);
	}
	msleep_interruptible(4 * 1000);
}

static int txgbe_get_sset_count(struct net_device *netdev, int stringset)
{
	switch (stringset) {
	case ETH_SS_TEST:
		return TXGBE_TEST_LEN;
	case ETH_SS_STATS:
		return TXGBE_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void txgbe_get_strings(struct net_device *netdev, u32 stringset,
			      u8 *data)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	char *p = (char *)data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *txgbe_gstrings_test,
		       TXGBE_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:

		for (i = 0; i < TXGBE_HW_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%-26s\t",
				 txgbe_hw_stats[i].name);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < TXGBE_SW_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%-26s\t",
				 txgbe_sw_stats[i].name);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			sprintf(p, "tx_queue_%u_%-16s", i, "packets");
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_%-16s", i, "bytes");
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_rx_queues; i++) {
			sprintf(p, "rx_queue_%u_%-16s", i, "packets");
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_%-16s", i, "bytes");
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < TXGBE_NET_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%-26s\t",
				 txgbe_net_stats[i].name);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void txgbe_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats __always_unused *stats,
				      u64 *data)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	//txgbe_net_stats_t *net_stats = &adapter->net_stats;
	struct txgbe_ring *ring;
	int i = 0, j;
	char *p;
	unsigned int start;

	txgbevf_update_stats(adapter);

	for (j = 0; j < TXGBE_HW_STATS_LEN; j++) {
		p = (char *)adapter + txgbe_hw_stats[j].offset;
		data[i++] = (txgbe_hw_stats[j].size == sizeof(u64))
			? *(u64 *)p : *(u32 *)p;
	}

	for (j = 0; j < TXGBE_SW_STATS_LEN; j++) {
		p = (char *)adapter + txgbe_sw_stats[j].offset;
		data[i++] = (txgbe_sw_stats[j].size == sizeof(u64))
			? *(u64 *)p : *(u32 *)p;
	}

	/* populate Tx queue data */
	for (j = 0; j < adapter->num_tx_queues; j++) {
		ring = adapter->tx_ring[j];
		if (!ring) {
			data[i++] = 0;
			data[i++] = 0;

			continue;
		}

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);

			data[i]   = ring->stats.packets;
			data[i + 1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
		i += 2;
	}

	/* populate Rx queue data */
	for (j = 0; j < adapter->num_rx_queues; j++) {
		ring = adapter->rx_ring[j];
		if (!ring) {
			data[i++] = 0;
			data[i++] = 0;
			continue;
		}

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);

			data[i]   = ring->stats.packets;
			data[i + 1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		i += 2;
	}
}

static int txgbe_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	/* only valid if in constant ITR mode */
	ec->rx_coalesce_usecs = adapter->rx_itr_setting > 1
				  ? adapter->rx_itr_setting << 1
				  : adapter->rx_itr_setting;

	/* if in mixed tx/rx queues per vector mode, report only rx settings */
	if (adapter->q_vector[0]->tx.count && adapter->q_vector[0]->rx.count)
		return 0;

	/* only valid if in constant ITR mode */
	ec->tx_coalesce_usecs = adapter->tx_itr_setting > 1
				  ? adapter->tx_itr_setting << 1
				  : adapter->tx_itr_setting;

	return 0;
}

static int txgbe_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	struct txgbe_q_vector *q_vector;
	int i;
	u16 tx_itr_param, rx_itr_param;

	/* don't accept tx specific changes if we've got mixed RxTx vectors */
	if (adapter->q_vector[0]->tx.count &&
	    adapter->q_vector[0]->rx.count &&
		 ec->tx_coalesce_usecs)
		return -EINVAL;

	if ((ec->rx_coalesce_usecs > TXGBE_VXITR_INTERVAL(~0) >> 1) ||
	    (ec->tx_coalesce_usecs > TXGBE_VXITR_INTERVAL(~0) >> 1))
		return -EINVAL;

	adapter->rx_itr_setting = ec->rx_coalesce_usecs > 1
				    ? ec->rx_coalesce_usecs >> 1
				    : ec->rx_coalesce_usecs;

	if (adapter->rx_itr_setting == 1)
		rx_itr_param = TXGBE_20K_ITR;
	else
		rx_itr_param = adapter->rx_itr_setting;

	adapter->tx_itr_setting = ec->tx_coalesce_usecs > 1
				    ? ec->tx_coalesce_usecs >> 1
				    : ec->tx_coalesce_usecs;

	if (adapter->tx_itr_setting == 1)
		tx_itr_param = TXGBE_12K_ITR;
	else
		tx_itr_param = adapter->tx_itr_setting;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		q_vector = adapter->q_vector[i];
		if (q_vector->tx.count && !q_vector->rx.count)
			/* tx only */
			q_vector->itr = tx_itr_param;
		else
			/* rx only or mixed */
			q_vector->itr = rx_itr_param;
		txgbevf_write_eitr(q_vector);
	}

	return 0;
}

static int txgbe_get_rss_hash_opts(struct txgbe_adapter *adapter,
				   struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;

	/* Report default options for RSS on txgbevf */
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		goto TCP_V4_FLOW_CASE;
	case UDP_V4_FLOW:
		goto UDP_V4_FLOW_CASE;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		goto IPV4_FLOW_CASE;
	case TCP_V6_FLOW:
		goto TCP_V6_FLOW_CASE;
	case UDP_V6_FLOW:
		goto UDP_V6_FLOW_CASE;
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		goto IPV6_FLOW_CASE;
	default:
		return -EINVAL;
	}

TCP_V4_FLOW_CASE:
	cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
UDP_V4_FLOW_CASE:
	if (adapter->flagsd & TXGBE_F_ENA_RSS_IPV4UDP)
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
IPV4_FLOW_CASE:
	cmd->data |= RXH_IP_SRC | RXH_IP_DST;
	return 0;

TCP_V6_FLOW_CASE:
	cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
UDP_V6_FLOW_CASE:
	if (adapter->flagsd & TXGBE_F_ENA_RSS_IPV6UDP)
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
IPV6_FLOW_CASE:
	cmd->data |= RXH_IP_SRC | RXH_IP_DST;
	return 0;
}

#define UDP_RSS_FLAGS (TXGBE_F_ENA_RSS_IPV4UDP | \
		       TXGBE_F_ENA_RSS_IPV6UDP)

static int txgbe_set_rss_hash_opt(struct txgbe_adapter *adapter,
				  struct ethtool_rxnfc *nfc)
{
	struct txgbe_hw *hw = &adapter->hw;
	u32 flags = adapter->flagsd;

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    !(nfc->data & RXH_L4_B_0_1) ||
		    !(nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	case UDP_V4_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~TXGBE_F_ENA_RSS_IPV4UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= TXGBE_F_ENA_RSS_IPV4UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case UDP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~TXGBE_F_ENA_RSS_IPV6UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= TXGBE_F_ENA_RSS_IPV6UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    (nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* if we changed something we need to update flags */
	if (flags != adapter->flagsd) {
		u32 vfmrqc = 0;

		if ((flags & UDP_RSS_FLAGS) &&
		    !(adapter->flagsd & UDP_RSS_FLAGS))
			e_info(drv, "enabling UDP RSS: fragmented packets may arrive out of order to the stack above\n");

		adapter->flagsd = flags;

		vfmrqc = rd32(hw, TXGBE_VXMRQC) >> 16;
		/* Perform hash on these packet types */
		vfmrqc |= TXGBE_VXMRQC_RSS_ALG_IPV4 |
			  TXGBE_VXMRQC_RSS_ALG_IPV4_TCP |
			  TXGBE_VXMRQC_RSS_ALG_IPV6 |
			  TXGBE_VXMRQC_RSS_ALG_IPV6_TCP;

		vfmrqc &= ~(TXGBE_VXMRQC_RSS_ALG_IPV4_UDP |
			    TXGBE_VXMRQC_RSS_ALG_IPV6_UDP);

		if (flags & TXGBE_F_ENA_RSS_IPV4UDP)
			vfmrqc |= TXGBE_VXMRQC_RSS_ALG_IPV4_UDP;

		if (flags & TXGBE_F_ENA_RSS_IPV6UDP)
			vfmrqc |= TXGBE_VXMRQC_RSS_ALG_IPV6_UDP;

		wr32m(hw, TXGBE_VXMRQC, TXGBE_VXMRQC_RSS(~0),
		      TXGBE_VXMRQC_RSS(vfmrqc));
	}

	return 0;
}

static int txgbe_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			   __always_unused u32 *rule_locs)
{
	struct txgbe_adapter *adapter = netdev_priv(dev);
	int ret;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = adapter->num_rx_queues;
		break;
	case ETHTOOL_GRXFH:
		ret = txgbe_get_rss_hash_opts(adapter, info);
		if (ret)
			return ret;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int txgbe_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct txgbe_adapter *adapter = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	if (cmd->cmd == ETHTOOL_SRXFH)
		ret = txgbe_set_rss_hash_opt(adapter, cmd);

	return ret;
}

static const struct ethtool_ops txgbe_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS |
								 ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ,
	.get_link_ksettings     = txgbevf_get_link_ksettings,
	.set_link_ksettings     = txgbevf_set_link_ksettings,
	.get_drvinfo            = txgbe_get_drvinfo,
	.get_regs_len           = txgbe_get_regs_len,
	.get_regs               = txgbe_get_regs,
	.nway_reset             = txgbe_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_eeprom             = txgbe_get_eeprom,
	.set_eeprom             = txgbe_set_eeprom,
	.get_rxfh_indir_size    = txgbe_get_rxfh_indir_size,
	.get_rxfh_key_size      = txgbe_get_rxfh_key_size,
	.get_rxfh               = txgbe_get_rxfh,
	.get_ringparam          = txgbe_get_ringparam,
	.set_ringparam          = txgbe_set_ringparam,
	.get_msglevel           = txgbe_get_msglevel,
	.set_msglevel           = txgbe_set_msglevel,
	.self_test              = txgbe_diag_test,
	.get_sset_count         = txgbe_get_sset_count,
	.get_strings            = txgbe_get_strings,
	.get_ethtool_stats      = txgbe_get_ethtool_stats,
	.get_coalesce           = txgbe_get_coalesce,
	.set_coalesce           = txgbe_set_coalesce,
	.get_rxnfc              = txgbe_get_rxnfc,
	.set_rxnfc              = txgbe_set_rxnfc,
};

void txgbevf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &txgbe_ethtool_ops;
}
