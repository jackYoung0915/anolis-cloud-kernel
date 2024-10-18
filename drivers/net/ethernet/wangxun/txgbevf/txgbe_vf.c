// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */
#include "txgbe_vf.h"
#include "txgbe_mbx.h"

s32 txgbe_start_hw_vf(struct txgbe_hw *hw)
{
	/* Clear adapter stopped flag */
	hw->adapter_stopped = false;

	return 0;
}

s32 txgbe_get_mac_addr_vf(struct txgbe_hw *hw, u8 *mac_addr)
{
	int i;

	for (i = 0; i < 6; i++)
		mac_addr[i] = hw->mac.perm_addr[i];

	return 0;
}

s32 txgbe_init_hw_vf(struct txgbe_hw *hw)
{
	s32 status = hw->mac.ops.start_hw(hw);

	hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

	return status;
}

/* txgbe_virt_clr_reg - Set register to default (power on) state.
 *  @hw: pointer to hardware structure
 */
static void txgbe_virt_clr_reg(struct txgbe_hw *hw)
{
	int i;
	u32 vfsrrctl;

	/* VRSRRCTL default values (BSIZEPACKET = 2048, BSIZEHEADER = 256) */
	vfsrrctl = TXGBE_VXRXDCTL_HDRSZ(txgbe_hdr_sz(TXGBE_RX_HDR_SIZE));
	vfsrrctl |= TXGBE_VXRXDCTL_BUFSZ(txgbe_buf_sz(TXGBE_RX_BUF_SIZE));

	for (i = 0; i < 7; i++) {
		wr32m(hw, TXGBE_VXRXDCTL(i),
		      (TXGBE_VXRXDCTL_HDRSZ(~0) | TXGBE_VXRXDCTL_BUFSZ(~0)),
			vfsrrctl);
	}

	txgbe_flush(hw);
}

s32 txgbe_reset_hw_vf(struct txgbe_hw *hw)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 timeout = TXGBE_VF_INIT_TIMEOUT;
	s32 err;
	u32 msgbuf[TXGBE_VF_PERMADDR_MSG_LEN];
	u8 *addr = (u8 *)(&msgbuf[1]);
	u32 i;

	hw->mac.ops.stop_adapter(hw);

	/* reset the api version */
	hw->api_version = txgbe_mbox_api_10;

	txgbevf_dbg(hw, "Issuing a function reset to MAC\n");

	/* backup msix vectors */
	for (i = 0; i < 16; i++)
		hw->b4_buf[i] = txgbe_rd32(hw->b4_addr, i * 4);

	wr32m(hw, TXGBE_VXCTRL, TXGBE_VXCTRL_RST, TXGBE_VXCTRL_RST);
	txgbe_flush(hw);

	msleep(50);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw, 0) && timeout) {
		timeout--;
		udelay(5);
	}

	/* restore msix vectors */
	for (i = 0; i < 16; i++)
		txgbe_wr32(hw->b4_addr, i * 4, hw->b4_buf[i]);

	if (!timeout)
		return TXGBE_ERR_RESET_FAILED;

	/* Reset VF registers to initial values */
	txgbe_virt_clr_reg(hw);

	/* mailbox timeout can now become active */
	mbx->timeout = TXGBE_VF_MBX_INIT_TIMEOUT;

	msgbuf[0] = TXGBE_VF_RESET;
	err = mbx->ops.write_posted(hw, msgbuf, 1, 0);
	if (err)
		return err;

	usleep_range(10000, 20000);

	/* set our "perm_addr" based on info provided by PF
	 * also set up the mc_filter_type which is piggy backed
	 * on the mac address in word 3
	 */
	err = mbx->ops.read_posted(hw, msgbuf,
			TXGBE_VF_PERMADDR_MSG_LEN, 0);
	if (err)
		return err;

	if (msgbuf[0] != (TXGBE_VF_RESET | TXGBE_VT_MSGTYPE_ACK) &&
	    msgbuf[0] != (TXGBE_VF_RESET | TXGBE_VT_MSGTYPE_NACK))
		return TXGBE_ERR_INVALID_MAC_ADDR;

	if (msgbuf[0] == (TXGBE_VF_RESET | TXGBE_VT_MSGTYPE_ACK))
		memcpy(hw->mac.perm_addr, addr, 6);

	hw->mac.mc_filter_type = msgbuf[TXGBE_VF_MC_TYPE_WORD];

	return 0;
}

s32 txgbe_stop_adapter_vf(struct txgbe_hw *hw)
{
	u32 reg_val;
	u16 i;

	/* Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	hw->adapter_stopped = true;

	/* Clear interrupt mask to stop from interrupts being generated */
	wr32(hw, TXGBE_VXIMS, TXGBE_VF_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts, flush previous writes */
	wr32(hw, TXGBE_VXICR, ~0);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < hw->mac.max_tx_queues; i++)
		wr32(hw, TXGBE_VXTXDCTL(i), TXGBE_VXTXDCTL_FLUSH);

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		reg_val = rd32(hw, TXGBE_VXRXDCTL(i));
		reg_val &= ~TXGBE_VXRXDCTL_ENABLE;
		wr32(hw, TXGBE_VXRXDCTL(i), reg_val);
	}
	/* Clear packet split and pool config */
	wr32(hw, TXGBE_VXMRQC, 0);

	/* flush all queues disables */
	txgbe_flush(hw);
	usleep_range(10000, 20000);

	return 0;
}

s32 txgbe_get_fw_version(struct txgbe_hw *hw)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;

	msgbuf[0] = TXGBE_VF_GET_FW_VERSION;
	msgbuf[1] = 0x0;

	err = mbx->ops.write_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	err = mbx->ops.read_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	if (err || (msgbuf[0] & TXGBE_VT_MSGTYPE_NACK)) {
		err = TXGBE_ERR_MBX;
	} else {
		snprintf(txgbe_firmware_version, TXGBE_FW_VER_SIZE, "0x%08x", msgbuf[1]);
		err = 0;
	}

	return err;
}

s32 txgbe_check_mac_link_vf(struct txgbe_hw *hw, txgbe_link_speed *speed,
			    bool *link_up, bool __always_unused autoneg_wait_to_complete)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	struct txgbe_mac_info *mac = &hw->mac;
	s32 err = 0;
	u32 links_reg;
	u32 in_msg = 0;
	u8 i = 0;

	/* If we were hit with a reset drop the link */
	if (!mbx->ops.check_for_rst(hw, 0) || !mbx->timeout)
		mac->get_link_status = true;

	mac->get_link_status = true;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	links_reg = rd32(hw, TXGBE_VXSTATUS);
	if (!(links_reg & TXGBE_VXSTATUS_UP))
		goto out;

	/* for SFP+ modules and DA cables, it can take up to 500usecs
	 * before the link status is correct
	 */
	if (!po32m(hw, TXGBE_VXSTATUS, TXGBE_VXSTATUS_UP, 0, 100, 5))
		goto out;

	for (i = 0; i < 100; i++) {
		udelay(5);
		links_reg = rd32(hw, TXGBE_VXSTATUS);

		if (!(links_reg & TXGBE_VXSTATUS_UP))
			goto out;
	}

	switch (TXGBE_VXSTATUS_SPEED(links_reg)) {
	case TXGBE_VXSTATUS_SPEED_10G:
		*speed = TXGBE_LINK_SPEED_10GB_FULL;
		break;
	case TXGBE_VXSTATUS_SPEED_1G:
		*speed = TXGBE_LINK_SPEED_1GB_FULL;
		break;
	case TXGBE_VXSTATUS_SPEED_100M:
		*speed = TXGBE_LINK_SPEED_100_FULL;
		break;
	}

	/* if the read failed it could just be a mailbox collision, best wait
	 * until we are called again and don't report an error
	 */
	if (mbx->ops.read(hw, &in_msg, 1, 0))
		goto out;

	if (!(in_msg & TXGBE_VT_MSGTYPE_CTS)) {
		/* msg is not CTS and is NACK we must have lost CTS status */
		if (in_msg & TXGBE_VT_MSGTYPE_NACK)
			err = -1;
		goto out;
	}

	/* the pf is talking, if we timed out in the past we reinit */
	if (!mbx->timeout) {
		err = -1;
		goto out;
	}

	/* if we passed all the tests above then the link is up and we no
	 * longer need to check for link
	 */
	mac->get_link_status = false;
out:
	*link_up = !mac->get_link_status;
	return err;
}

s32 txgbe_set_rar_vf(struct txgbe_hw *hw, u32 __always_unused index, u8 *addr,
		     u32 __always_unused vmdq, u32 __always_unused enable_addr)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 err;

	memset(msgbuf, 0, 12);
	msgbuf[0] = TXGBE_VF_SET_MAC_ADDR;
	memcpy(msg_addr, addr, 6);
	err = mbx->ops.write_posted(hw, msgbuf, 3, 0);

	if (!err)
		err = mbx->ops.read_posted(hw, msgbuf, 3, 0);

	msgbuf[0] &= ~TXGBE_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (!err &&
	    (msgbuf[0] == (TXGBE_VF_SET_MAC_ADDR | TXGBE_VT_MSGTYPE_NACK))) {
		txgbe_get_mac_addr_vf(hw, hw->mac.addr);
		return TXGBE_ERR_MBX;
	}

	return err;
}

s32 txgbe_set_uc_addr_vf(struct txgbe_hw *hw, u32 index, u8 *addr)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 err;

	memset(msgbuf, 0, sizeof(msgbuf));
	/* If index is one then this is the start of a new list and needs
	 * indication to the PF so it can do it's own list management.
	 * If it is zero then that tells the PF to just clear all of
	 * this VF's macvlans and there is no new list.
	 */
	msgbuf[0] |= index << TXGBE_VT_MSGINFO_SHIFT;
	msgbuf[0] |= TXGBE_VF_SET_MACVLAN;
	if (addr)
		memcpy(msg_addr, addr, 6);
	err = mbx->ops.write_posted(hw, msgbuf, 3, 0);

	if (!err)
		err = mbx->ops.read_posted(hw, msgbuf, 3, 0);

	msgbuf[0] &= ~TXGBE_VT_MSGTYPE_CTS;

	if (!err)
		if (msgbuf[0] == (TXGBE_VF_SET_MACVLAN | TXGBE_VT_MSGTYPE_NACK))
			err = TXGBE_ERR_OUT_OF_MEM;

	return err;
}

static s32 txgbe_mta_vector(struct txgbe_hw *hw, u8 *mc_addr)
{
	u32 vector = 0;

	switch (hw->mac.mc_filter_type) {
	case 0:   /* use bits [47:36] of the address */
		vector = ((mc_addr[4] >> 4) | (((u16)mc_addr[5]) << 4));
		break;
	case 1:   /* use bits [46:35] of the address */
		vector = ((mc_addr[4] >> 3) | (((u16)mc_addr[5]) << 5));
		break;
	case 2:   /* use bits [45:34] of the address */
		vector = ((mc_addr[4] >> 2) | (((u16)mc_addr[5]) << 6));
		break;
	case 3:   /* use bits [43:32] of the address */
		vector = ((mc_addr[4]) | (((u16)mc_addr[5]) << 8));
		break;
	default:  /* Invalid mc_filter_type */
		txgbevf_dbg(hw, "MC filter type param set incorrectly\n");
		break;
	}

	/* vector can only be 12-bits or boundary will be exceeded */
	vector &= 0xFFF;

	return vector;
}

s32 txgbe_update_mc_addr_list_vf(struct txgbe_hw *hw, u8 *mc_addr_list,
				 u32 mc_addr_count, txgbe_mc_addr_itr next,
				 bool __always_unused clear)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[TXGBE_VXMAILBOX_SIZE];
	u16 *vector_list = (u16 *)&msgbuf[1];
	u32 vector;
	u32 cnt, i;
	u32 vmdq;

	/* Each entry in the list uses 1 16 bit word.  We have 30
	 * 16 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 30 hash values if we pack 'em right.  If
	 * there are more than 30 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 30 later.
	 * It would be unusual for a server to request that many multi-cast
	 * addresses except for in large enterprise network environments.
	 */

	txgbevf_dbg(hw, "MC Addr Count = %d\n", mc_addr_count);

	cnt = (mc_addr_count > 30) ? 30 : mc_addr_count;
	msgbuf[0] = TXGBE_VF_SET_MULTICAST;
	msgbuf[0] |= cnt << TXGBE_VT_MSGINFO_SHIFT;

	for (i = 0; i < cnt; i++) {
		vector = txgbe_mta_vector(hw, next(hw, &mc_addr_list, &vmdq));
		txgbevf_dbg(hw, "Hash value = 0x%03X\n", vector);
		vector_list[i] = (u16)vector;
	}

	return mbx->ops.write_posted(hw, msgbuf, TXGBE_VXMAILBOX_SIZE, 0);
}

s32 txgbe_update_xcast_mode(struct txgbe_hw *hw, int xcast_mode)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;

	switch (hw->api_version) {
	case txgbe_mbox_api_12:
		if (xcast_mode > TXGBE_XCAST_MODE_ALLMULTI)
			return TXGBE_ERR_FEATURE_NOT_SUPPORTED;
	case txgbe_mbox_api_13:
	//case txgbe_mbox_api_15:
		break;
	default:
		return TXGBE_ERR_FEATURE_NOT_SUPPORTED;
	}

	msgbuf[0] = TXGBE_VF_UPDATE_XCAST_MODE;
	msgbuf[1] = xcast_mode;

	err = mbx->ops.write_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	err = mbx->ops.read_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	msgbuf[0] &= ~TXGBE_VT_MSGTYPE_CTS;
	if (msgbuf[0] == (TXGBE_VF_UPDATE_XCAST_MODE | TXGBE_VT_MSGTYPE_NACK))
		return TXGBE_ERR_FEATURE_NOT_SUPPORTED;
	return 0;
}

s32 txgbe_get_link_state_vf(struct txgbe_hw *hw, bool *link_state)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;
	s32 ret_val;

	msgbuf[0] = TXGBE_VF_GET_LINK_STATE;
	msgbuf[1] = 0x0;

	err = mbx->ops.write_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	err = mbx->ops.read_posted(hw, msgbuf, 2, 0);
	if (err)
		return err;

	if (err || (msgbuf[0] & TXGBE_VT_MSGTYPE_NACK)) {
		ret_val = TXGBE_ERR_MBX;
	} else {
		ret_val = 0;
		*link_state = msgbuf[1];
	}

	return ret_val;
}

s32 txgbe_set_vfta_vf(struct txgbe_hw *hw, u32 vlan, u32 vind,
		      bool __always_unused vlan_on, bool __always_unused vlvf_bypass)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;

	msgbuf[0] = TXGBE_VF_SET_VLAN;
	msgbuf[1] = vlan;
	/* Setting the 8 bit field MSG INFO to TRUE indicates "add" */
	msgbuf[0] |= vlan_on << TXGBE_VT_MSGINFO_SHIFT;

	err = mbx->ops.write_posted(hw, msgbuf, 2, 0);
	if (!err)
		err = mbx->ops.read_posted(hw, msgbuf, 1, 0);

	if (!err && (msgbuf[0] & TXGBE_VT_MSGTYPE_ACK))
		return 0;

	return err | (msgbuf[0] & TXGBE_VT_MSGTYPE_NACK);
}

/**
 *  txgbe_negotiate_api_version - Negotiate supported API version
 *  @hw: pointer to the HW structure
 *  @api: integer containing requested API version
 **/
int txgbe_negotiate_api_version(struct txgbe_hw *hw, int api)
{
	int err;
	u32 msg[3];

	/* Negotiate the mailbox API version */
	msg[0] = TXGBE_VF_API_NEGOTIATE;
	msg[1] = api;
	msg[2] = 0;
	err = hw->mbx.ops.write_posted(hw, msg, 3, 0);

	if (!err)
		err = hw->mbx.ops.read_posted(hw, msg, 3, 0);

	if (!err) {
		msg[0] &= ~TXGBE_VT_MSGTYPE_CTS;

		/* Store value and return 0 on success */
		if (msg[0] == (TXGBE_VF_API_NEGOTIATE | TXGBE_VT_MSGTYPE_ACK)) {
			hw->api_version = api;
			return 0;
		}

		err = TXGBE_ERR_INVALID_ARGUMENT;
	}
	return err;
}

static s32 txgbe_write_msg_read_ack(struct txgbe_hw *hw, u32 *msg,
				    u32 *retmsg, u16 size)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	s32 retval = mbx->ops.write_posted(hw, msg, size, 0);

	if (retval)
		return retval;

	return mbx->ops.read_posted(hw, retmsg, size, 0);
}

/**
 *  txgbe_rlpml_set_vf - Set the maximum receive packet length
 *  @hw: pointer to the HW structure
 *  @max_size: value to assign to max frame size
 **/
s32 txgbe_rlpml_set_vf(struct txgbe_hw *hw, u16 max_size)
{
	u32 msgbuf[2];
	s32 retval;

	msgbuf[0] = TXGBE_VF_SET_LPE;
	msgbuf[1] = max_size;

	retval = txgbe_write_msg_read_ack(hw, msgbuf, msgbuf, 2);
	if (retval)
		return retval;
	if ((msgbuf[0] & TXGBE_VF_SET_LPE) &&
	    (msgbuf[0] & TXGBE_VT_MSGTYPE_NACK))
		return TXGBE_ERR_MBX;

	return 0;
}

int txgbe_get_queues(struct txgbe_hw *hw, unsigned int *num_tcs,
		     unsigned int *default_tc)
{
	int err;
	u32 msg[5];

	/* do nothing if API doesn't support txgbe_get_queues */
	switch (hw->api_version) {
	case txgbe_mbox_api_11:
	case txgbe_mbox_api_12:
	case txgbe_mbox_api_13:
		break;
	default:
		return 0;
	}

	/* Fetch queue configuration from the PF */
	msg[0] = TXGBE_VF_GET_QUEUES;
	msg[1] = 0;
	msg[2] = 0;
	msg[3] = 0;
	msg[4] = 0;
	err = hw->mbx.ops.write_posted(hw, msg, 5, 0);

	if (!err)
		err = hw->mbx.ops.read_posted(hw, msg, 5, 0);
	if (!err) {
		msg[0] &= ~TXGBE_VT_MSGTYPE_CTS;

		/* if we didn't get an ACK there must have been
		 * some sort of mailbox error so we should treat it
		 * as such
		 */
		if (msg[0] != (TXGBE_VF_GET_QUEUES | TXGBE_VT_MSGTYPE_ACK))
			return TXGBE_ERR_MBX;

		/* record and validate values from message */
		hw->mac.max_tx_queues = msg[TXGBE_VF_TX_QUEUES];
		if (hw->mac.max_tx_queues == 0 ||
		    hw->mac.max_tx_queues > TXGBE_VF_MAX_TX_QUEUES)
			hw->mac.max_tx_queues = TXGBE_VF_MAX_TX_QUEUES;

		hw->mac.max_rx_queues = msg[TXGBE_VF_RX_QUEUES];
		if (hw->mac.max_rx_queues == 0 ||
		    hw->mac.max_rx_queues > TXGBE_VF_MAX_RX_QUEUES)
			hw->mac.max_rx_queues = TXGBE_VF_MAX_RX_QUEUES;

		*num_tcs = msg[TXGBE_VF_TRANS_VLAN];
		/* in case of unknown state assume we cannot tag frames */
		if (*num_tcs > hw->mac.max_rx_queues)
			*num_tcs = 1;

		*default_tc = msg[TXGBE_VF_DEF_QUEUE];
		/* default to queue 0 on out-of-bounds queue number */
		if (*default_tc >= hw->mac.max_tx_queues)
			*default_tc = 0;
	}

	return err;
}

void txgbe_init_ops_vf(struct txgbe_hw *hw)
{
	/* MAC */
	hw->mac.ops.init_hw = txgbe_init_hw_vf;
	hw->mac.ops.reset_hw = txgbe_reset_hw_vf;
	hw->mac.ops.start_hw = txgbe_start_hw_vf;
	/* Cannot clear stats on VF */
	hw->mac.ops.get_mac_addr = txgbe_get_mac_addr_vf;
	hw->mac.ops.get_fw_version = txgbe_get_fw_version;
	hw->mac.ops.stop_adapter = txgbe_stop_adapter_vf;

	/* Link */
	hw->mac.ops.check_link = txgbe_check_mac_link_vf;

	/* RAR, Multicast, VLAN */
	hw->mac.ops.set_rar = txgbe_set_rar_vf;
	hw->mac.ops.set_uc_addr = txgbe_set_uc_addr_vf;
	hw->mac.ops.update_mc_addr_list = txgbe_update_mc_addr_list_vf;
	hw->mac.ops.update_xcast_mode = txgbe_update_xcast_mode;
	hw->mac.ops.get_link_state = txgbe_get_link_state_vf;
	hw->mac.ops.set_vfta = txgbe_set_vfta_vf;

	hw->mac.max_tx_queues = 1;
	hw->mac.max_rx_queues = 1;

	hw->mbx.ops.init_params = txgbevf_init_mbx_params_vf;
}
