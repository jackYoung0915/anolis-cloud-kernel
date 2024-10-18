// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */
#include "txgbe_vf.h"
#include "txgbe_mbx.h"

u32 txgbevf_read_v2p_mailbox(struct txgbe_hw *hw)
{
	u32 v2p_mailbox = rd32(hw, TXGBE_VXMAILBOX);

	v2p_mailbox |= hw->mbx.v2p_mailbox;
	/* read and clear mirrored mailbox flags */
	v2p_mailbox |= rd32a(hw, TXGBE_VXMBMEM, TXGBE_VXMAILBOX_SIZE);
	wr32a(hw, TXGBE_VXMBMEM, TXGBE_VXMAILBOX_SIZE, 0);
	hw->mbx.v2p_mailbox |= v2p_mailbox & TXGBE_VXMAILBOX_R2C_BITS;

	return v2p_mailbox;
}

s32 txgbevf_check_for_bit_vf(struct txgbe_hw *hw, u32 mask)
{
	u32 mailbox = txgbevf_read_v2p_mailbox(hw);

	hw->mbx.v2p_mailbox &= ~mask;

	return (mailbox & mask ? 0 : TXGBE_ERR_MBX);
}

s32 txgbevf_obtain_mbx_lock_vf(struct txgbe_hw *hw)
{
	s32 err = TXGBE_ERR_MBX;
	u32 mailbox;

	/* Take ownership of the buffer */
	wr32(hw, TXGBE_VXMAILBOX, TXGBE_VXMAILBOX_VFU);

	/* reserve mailbox for vf use */
	mailbox = txgbevf_read_v2p_mailbox(hw);
	if (mailbox & TXGBE_VXMAILBOX_VFU)
		err = 0;
	else
		txgbevf_dbg(hw,
			    "Failed to obtain mailbox lock for VF");

	return err;
}

/**
 *  txgbevf_read_mbx_vf - Reads a message from the inbox intended for vf
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to read
 *
 *  returns SUCCESS if it successfully read message from buffer
 **/
s32 txgbevf_read_mbx_vf(struct txgbe_hw *hw, u32 *msg, u16 size,
			u16 __always_unused mbx_id)
{
	s32 err = 0;
	u16 i;

	/* lock the mailbox to prevent pf/vf race condition */
	err = txgbevf_obtain_mbx_lock_vf(hw);
	if (err)
		goto out_no_read;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = rd32a(hw, TXGBE_VXMBMEM, i);

	/* Acknowledge receipt and release mailbox, then we're done */
	wr32(hw, TXGBE_VXMAILBOX, TXGBE_VXMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return err;
}

s32 txgbevf_poll_for_msg(struct txgbe_hw *hw, u16 mbx_id)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops.check_for_msg)
		goto out;

	while (countdown && hw->mbx.ops.check_for_msg(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		udelay(mbx->udelay);
	}

	if (countdown == 0)
		txgbevf_dbg(hw, "Polling for VF%d mailbox message timedout", mbx_id);

out:
	return countdown ? 0 : TXGBE_ERR_MBX;
}

s32 txgbevf_poll_for_ack(struct txgbe_hw *hw, u16 mbx_id)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops.check_for_ack)
		goto out;

	while (countdown && hw->mbx.ops.check_for_ack(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		udelay(mbx->udelay);
	}

	if (countdown == 0)
		txgbevf_dbg(hw, "Polling for VF%d mailbox ack timedout", mbx_id);

out:
	return countdown ? 0 : TXGBE_ERR_MBX;
}

/**
 *  txgbevf_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
s32 txgbevf_read_posted_mbx(struct txgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	s32 err = TXGBE_ERR_MBX;

	if (!mbx->ops.read)
		goto out;

	err = txgbevf_poll_for_msg(hw, mbx_id);

	/* if ack received read message, otherwise we timed out */
	if (!err)
		err = hw->mbx.ops.read(hw, msg, size, mbx_id);
out:
	return err;
}

/**
 *  txgbevf_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
s32 txgbevf_write_posted_mbx(struct txgbe_hw *hw, u32 *msg, u16 size,
			     u16 mbx_id)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;
	s32 err;

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->timeout)
		return TXGBE_ERR_MBX;

	/* send msg */
	err = hw->mbx.ops.write(hw, msg, size, mbx_id);

	/* if msg sent wait until we receive an ack */
	if (!err)
		err = txgbevf_poll_for_ack(hw, mbx_id);

	return err;
}

/**
 *  txgbevf_check_for_msg_vf - checks to see if the PF has sent mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the PF has set the Status bit or else ERR_MBX
 **/
s32 txgbevf_check_for_msg_vf(struct txgbe_hw *hw, u16 __always_unused mbx_id)
{
	s32 err = TXGBE_ERR_MBX;

	/* read clear the pf sts bit */
	if (!txgbevf_check_for_bit_vf(hw, TXGBE_VXMAILBOX_PFSTS)) {
		err = 0;
		hw->mbx.stats.reqs++;
	}

	return err;
}

/**
 *  txgbevf_check_for_ack_vf - checks to see if the PF has ACK'd
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the PF has set the ACK bit or else ERR_MBX
 **/
s32 txgbevf_check_for_ack_vf(struct txgbe_hw *hw, u16 __always_unused mbx_id)
{
	s32 err = TXGBE_ERR_MBX;

	/* read clear the pf ack bit */
	if (!txgbevf_check_for_bit_vf(hw, TXGBE_VXMAILBOX_PFACK)) {
		err = 0;
		hw->mbx.stats.acks++;
	}

	return err;
}

/**
 *  txgbevf_check_for_rst_vf - checks to see if the PF has reset
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns true if the PF has set the reset done bit or else false
 **/
s32 txgbevf_check_for_rst_vf(struct txgbe_hw *hw, u16 __always_unused mbx_id)
{
	s32 err = TXGBE_ERR_MBX;

	if (!txgbevf_check_for_bit_vf(hw, (TXGBE_VXMAILBOX_RSTD |
	    TXGBE_VXMAILBOX_RSTI))) {
		err = 0;
		hw->mbx.stats.rsts++;
	}

	return err;
}

/**
 *  txgbevf_write_mbx_vf - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
s32 txgbevf_write_mbx_vf(struct txgbe_hw *hw, u32 *msg, u16 size,
			 u16 __always_unused mbx_id)
{
	s32 err;
	u16 i;

	/* lock the mailbox to prevent pf/vf race condition */
	err = txgbevf_obtain_mbx_lock_vf(hw);
	if (err)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	txgbevf_check_for_msg_vf(hw, 0);
	txgbevf_check_for_ack_vf(hw, 0);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		wr32a(hw, TXGBE_VXMBMEM, i, msg[i]);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	/* Drop VFU and interrupt the PF to tell it a message has been sent */
	wr32(hw, TXGBE_VXMAILBOX, TXGBE_VXMAILBOX_REQ);

out_no_write:
	return err;
}

void txgbevf_init_mbx_params_vf(struct txgbe_hw *hw)
{
	struct txgbe_mbx_info *mbx = &hw->mbx;

	/* start mailbox as timed out and let the reset_hw call set the timeout
	 * value to begin communications
	 */
	mbx->timeout = 0;
	mbx->udelay = TXGBE_VF_MBX_INIT_DELAY;

	mbx->size = TXGBE_VXMAILBOX_SIZE;

	mbx->ops.read = txgbevf_read_mbx_vf;
	mbx->ops.write = txgbevf_write_mbx_vf;
	mbx->ops.read_posted = txgbevf_read_posted_mbx;
	mbx->ops.write_posted = txgbevf_write_posted_mbx;
	mbx->ops.check_for_msg = txgbevf_check_for_msg_vf;
	mbx->ops.check_for_ack = txgbevf_check_for_ack_vf;
	mbx->ops.check_for_rst = txgbevf_check_for_rst_vf;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;
}
