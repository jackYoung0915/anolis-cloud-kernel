// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include "txgbe_phy.h"

/**
 * txgbe_check_reset_blocked - check status of MNG FW veto bit
 * @hw: pointer to the hardware structure
 *
 * This function checks the MMNGC.MNG_VETO bit to see if there are
 * any constraints on link from manageability.  For MAC's that don't
 * have this bit just return faluse since the link can not be blocked
 * via this method.
 **/
s32 txgbe_check_reset_blocked(struct txgbe_hw *hw)
{
	u32 mmngc;

	mmngc = rd32(hw, TXGBE_MIS_ST);
	if (mmngc & TXGBE_MIS_ST_MNG_VETO)
		return true;

	return false;
}

/**
 *  txgbe_identify_module - Identifies module type
 *  @hw: pointer to hardware structure
 *
 *  Determines HW type and calls appropriate function.
 **/
s32 txgbe_identify_module(struct txgbe_hw *hw)
{
	s32 status = TXGBE_ERR_SFP_NOT_PRESENT;

	switch (TCALL(hw, mac.ops.get_media_type)) {
	case txgbe_media_type_fiber:
		status = txgbe_identify_sfp_module(hw);
		break;

	default:
		hw->phy.sfp_type = txgbe_sfp_type_not_present;
		status = TXGBE_ERR_SFP_NOT_PRESENT;
		break;
	}

	return status;
}

/**
 *  txgbe_identify_sfp_module - Identifies SFP modules
 *  @hw: pointer to hardware structure
 *
 *  Searches for and identifies the SFP module and assigns appropriate PHY type.
 **/
s32 txgbe_identify_sfp_module(struct txgbe_hw *hw)
{
	s32 status = TXGBE_ERR_PHY_ADDR_INVALID;
	u32 vendor_oui = 0;
	u8 identifier = 0;
	u8 comp_codes_1g = 0;
	u8 comp_codes_10g = 0;
	u8 oui_bytes[3] = {0, 0, 0};
	u8 cable_tech = 0;
	u8 cable_spec = 0;
	u32 swfw_mask = hw->phy.phy_semaphore_mask;

	if (TCALL(hw, mac.ops.acquire_swfw_sync, swfw_mask) != 0)
		return TXGBE_ERR_SWFW_SYNC;

	if (TCALL(hw, mac.ops.get_media_type) != txgbe_media_type_fiber) {
		hw->phy.sfp_type = txgbe_sfp_type_not_present;
		status = TXGBE_ERR_SFP_NOT_PRESENT;
		goto out;
	}

	/* LAN ID is needed for I2C access */
	txgbe_init_i2c(hw);
	status = TCALL(hw, phy.ops.read_i2c_eeprom,
		       TXGBE_SFF_IDENTIFIER,
		       &identifier);

	if (status != 0)
		goto err_read_i2c_eeprom;

	if (identifier != TXGBE_SFF_IDENTIFIER_SFP) {
		hw->phy.type = txgbe_phy_sfp_unsupported;
		status = TXGBE_ERR_SFP_NOT_SUPPORTED;
	} else {
		status = TCALL(hw, phy.ops.read_i2c_eeprom,
			       TXGBE_SFF_1GBE_COMP_CODES,
			       &comp_codes_1g);

		if (status != 0)
			goto err_read_i2c_eeprom;

		status = TCALL(hw, phy.ops.read_i2c_eeprom,
			       TXGBE_SFF_10GBE_COMP_CODES,
			       &comp_codes_10g);

		if (status != 0)
			goto err_read_i2c_eeprom;
		status = TCALL(hw, phy.ops.read_i2c_eeprom,
			       TXGBE_SFF_CABLE_TECHNOLOGY,
			       &cable_tech);

		if (status != 0)
			goto err_read_i2c_eeprom;

		 /* ID Module
		  * =========
		  * 0   SFP_DA_CU
		  * 1   SFP_SR
		  * 2   SFP_LR
		  * 3   SFP_DA_CORE0
		  * 4   SFP_DA_CORE1
		  * 5   SFP_SR/LR_CORE0
		  * 6   SFP_SR/LR_CORE1
		  * 7   SFP_act_lmt_DA_CORE0
		  * 8   SFP_act_lmt_DA_CORE1
		  * 9   SFP_1g_cu_CORE0
		  * 10  SFP_1g_cu_CORE1
		  * 11  SFP_1g_sx_CORE0
		  * 12  SFP_1g_sx_CORE1
		  */
		{
			if (cable_tech & TXGBE_SFF_DA_PASSIVE_CABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						     txgbe_sfp_type_da_cu_core0;
				else
					hw->phy.sfp_type =
						     txgbe_sfp_type_da_cu_core1;
			} else if (cable_tech & TXGBE_SFF_DA_ACTIVE_CABLE) {
				TCALL(hw, phy.ops.read_i2c_eeprom,
				      TXGBE_SFF_CABLE_SPEC_COMP,
				      &cable_spec);
				if (cable_spec &
				    TXGBE_SFF_DA_SPEC_ACTIVE_LIMITING) {
					if (hw->bus.lan_id == 0)
						hw->phy.sfp_type =
						txgbe_sfp_type_da_act_lmt_core0;
					else
						hw->phy.sfp_type =
						txgbe_sfp_type_da_act_lmt_core1;
				} else {
					hw->phy.sfp_type =
							txgbe_sfp_type_unknown;
				}
			} else if (comp_codes_10g &
				   (TXGBE_SFF_10GBASESR_CAPABLE |
				    TXGBE_SFF_10GBASELR_CAPABLE)) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						      txgbe_sfp_type_srlr_core0;
				else
					hw->phy.sfp_type =
						      txgbe_sfp_type_srlr_core1;
			} else if (comp_codes_1g & TXGBE_SFF_1GBASET_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_cu_core0;
				else
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_cu_core1;
			} else if (comp_codes_1g & TXGBE_SFF_1GBASESX_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_sx_core0;
				else
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_sx_core1;
			} else if (comp_codes_1g & TXGBE_SFF_1GBASELX_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_lx_core0;
				else
					hw->phy.sfp_type =
						txgbe_sfp_type_1g_lx_core1;
			} else {
				hw->phy.sfp_type = txgbe_sfp_type_unknown;
			}
		}

		/* Determine if the SFP+ PHY is dual speed or not. */
		hw->phy.multispeed_fiber = false;
		if (((comp_codes_1g & TXGBE_SFF_1GBASESX_CAPABLE) &&
		     (comp_codes_10g & TXGBE_SFF_10GBASESR_CAPABLE)) ||
		    ((comp_codes_1g & TXGBE_SFF_1GBASELX_CAPABLE) &&
		     (comp_codes_10g & TXGBE_SFF_10GBASELR_CAPABLE)))
			hw->phy.multispeed_fiber = true;

		/* Determine PHY vendor */
		if (hw->phy.type != txgbe_phy_nl) {
			status = TCALL(hw, phy.ops.read_i2c_eeprom,
				       TXGBE_SFF_VENDOR_OUI_BYTE0,
				       &oui_bytes[0]);

			if (status != 0)
				goto err_read_i2c_eeprom;

			status = TCALL(hw, phy.ops.read_i2c_eeprom,
				       TXGBE_SFF_VENDOR_OUI_BYTE1,
				       &oui_bytes[1]);

			if (status != 0)
				goto err_read_i2c_eeprom;

			status = TCALL(hw, phy.ops.read_i2c_eeprom,
				       TXGBE_SFF_VENDOR_OUI_BYTE2,
				       &oui_bytes[2]);

			if (status != 0)
				goto err_read_i2c_eeprom;

			vendor_oui =
			  ((oui_bytes[0] << TXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT) |
			   (oui_bytes[1] << TXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT) |
			   (oui_bytes[2] << TXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT));

			switch (vendor_oui) {
			case TXGBE_SFF_VENDOR_OUI_TYCO:
				if (cable_tech & TXGBE_SFF_DA_PASSIVE_CABLE)
					hw->phy.type =
						    txgbe_phy_sfp_passive_tyco;
				break;
			case TXGBE_SFF_VENDOR_OUI_FTL:
				if (cable_tech & TXGBE_SFF_DA_ACTIVE_CABLE)
					hw->phy.type = txgbe_phy_sfp_ftl_active;
				else
					hw->phy.type = txgbe_phy_sfp_ftl;
				break;
			case TXGBE_SFF_VENDOR_OUI_AVAGO:
				hw->phy.type = txgbe_phy_sfp_avago;
				break;
			case TXGBE_SFF_VENDOR_OUI_INTEL:
				hw->phy.type = txgbe_phy_sfp_intel;
				break;
			default:
				if (cable_tech & TXGBE_SFF_DA_PASSIVE_CABLE)
					hw->phy.type =
						 txgbe_phy_sfp_passive_unknown;
				else if (cable_tech & TXGBE_SFF_DA_ACTIVE_CABLE)
					hw->phy.type =
						txgbe_phy_sfp_active_unknown;
				else
					hw->phy.type = txgbe_phy_sfp_unknown;
				break;
			}
		}

		/* Allow any DA cable vendor */
		if (cable_tech & (TXGBE_SFF_DA_PASSIVE_CABLE |
		    TXGBE_SFF_DA_ACTIVE_CABLE)) {
			status = 0;
			goto out;
		}

		/* Verify supported 1G SFP modules */
		if (comp_codes_10g == 0 &&
		    !(hw->phy.sfp_type == txgbe_sfp_type_1g_cu_core1 ||
		      hw->phy.sfp_type == txgbe_sfp_type_1g_cu_core0 ||
		      hw->phy.sfp_type == txgbe_sfp_type_1g_lx_core0 ||
		      hw->phy.sfp_type == txgbe_sfp_type_1g_lx_core1 ||
		      hw->phy.sfp_type == txgbe_sfp_type_1g_sx_core0 ||
		      hw->phy.sfp_type == txgbe_sfp_type_1g_sx_core1)) {
			hw->phy.type = txgbe_phy_sfp_unsupported;
			status = TXGBE_ERR_SFP_NOT_SUPPORTED;
			goto out;
		}
	}

out:
	TCALL(hw, mac.ops.release_swfw_sync, swfw_mask);

	return status;

err_read_i2c_eeprom:
	TCALL(hw, mac.ops.release_swfw_sync, swfw_mask);

	hw->phy.sfp_type = txgbe_sfp_type_not_present;
	if (hw->phy.type != txgbe_phy_nl)
		hw->phy.type = txgbe_phy_unknown;

	return TXGBE_ERR_SFP_NOT_PRESENT;
}

s32 txgbe_init_i2c(struct txgbe_hw *hw)
{
	wr32(hw, TXGBE_I2C_ENABLE, 0);

	wr32(hw, TXGBE_I2C_CON,
	     (TXGBE_I2C_CON_MASTER_MODE |
	      TXGBE_I2C_CON_SPEED(1) |
	      TXGBE_I2C_CON_RESTART_EN |
	      TXGBE_I2C_CON_SLAVE_DISABLE));
	/* Default addr is 0xA0 ,bit 0 is configure for read/write! */
	wr32(hw, TXGBE_I2C_TAR, TXGBE_I2C_SLAVE_ADDR);
	wr32(hw, TXGBE_I2C_SS_SCL_HCNT, 780);
	wr32(hw, TXGBE_I2C_SS_SCL_LCNT, 780);
	wr32(hw, TXGBE_I2C_RX_TL, 0); /* 1byte for rx full signal */
	wr32(hw, TXGBE_I2C_TX_TL, 4);
	wr32(hw, TXGBE_I2C_SCL_STUCK_TIMEOUT, 0xFFFFFF);
	wr32(hw, TXGBE_I2C_SDA_STUCK_TIMEOUT, 0xFFFFFF);

	wr32(hw, TXGBE_I2C_INTR_MASK, 0);
	wr32(hw, TXGBE_I2C_ENABLE, 1);
	return 0;
}

static s32 txgbe_init_i2c_sfp_phy(struct txgbe_hw *hw)
{
	wr32(hw, TXGBE_I2C_ENABLE, 0);

	wr32(hw, TXGBE_I2C_CON,
	     (TXGBE_I2C_CON_MASTER_MODE |
		TXGBE_I2C_CON_SPEED(1) |
		TXGBE_I2C_CON_RESTART_EN |
		TXGBE_I2C_CON_SLAVE_DISABLE));
	/* Default addr is 0xA0 ,bit 0 is configure for read/write! */
	wr32(hw, TXGBE_I2C_TAR, TXGBE_I2C_SLAVE_ADDR);
	wr32(hw, TXGBE_I2C_SS_SCL_HCNT, 600);
	wr32(hw, TXGBE_I2C_SS_SCL_LCNT, 600);

	wr32(hw, TXGBE_I2C_RX_TL, 1); /* 2bytes for rx full signal */
	wr32(hw, TXGBE_I2C_TX_TL, 4);

	wr32(hw, TXGBE_I2C_SCL_STUCK_TIMEOUT, 0xFFFFFF);
	wr32(hw, TXGBE_I2C_SDA_STUCK_TIMEOUT, 0xFFFFFF);

	wr32(hw, TXGBE_I2C_INTR_MASK, 0);
	wr32(hw, TXGBE_I2C_ENABLE, 1);

	return 0;
}

/**
 *  txgbe_read_i2c_eeprom - Reads 8 bit EEPROM word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to read
 *  @eeprom_data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface.
 **/
s32 txgbe_read_i2c_eeprom(struct txgbe_hw *hw, u8 byte_offset,
			  u8 *eeprom_data)
{
	txgbe_init_i2c(hw);
	return TCALL(hw, phy.ops.read_i2c_byte, byte_offset,
		     TXGBE_I2C_EEPROM_DEV_ADDR,
		     eeprom_data);
}

/**
 *  txgbe_read_i2c_sff8472 - Reads 8 bit word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset at address 0xA2
 *  @sff8472_data: value read
 *
 *  Performs byte read operation to SFP module's SFF-8472 data over I2C
 **/
s32 txgbe_read_i2c_sff8472(struct txgbe_hw *hw, u8 byte_offset,
			   u8 *sff8472_data)
{
	txgbe_init_i2c(hw);
	return TCALL(hw, phy.ops.read_i2c_byte, byte_offset,
		     TXGBE_I2C_EEPROM_DEV_ADDR2,
		     sff8472_data);
}

/**
 *  txgbe_read_i2c_byte_int - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: device address
 *  @data: value read
 *  @lock: true if to take and release semaphore
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
static s32 txgbe_read_i2c_byte_int(struct txgbe_hw *hw, u8 byte_offset,
				   u8 __maybe_unused dev_addr, u8 *data, bool lock)
{
	s32 status = 0;

	/* wait tx empty */
	status = txgbe_po32m(hw, TXGBE_I2C_RAW_INTR_STAT,
			     TXGBE_I2C_INTR_STAT_TX_EMPTY,
			     TXGBE_I2C_INTR_STAT_TX_EMPTY,
			     TXGBE_I2C_TIMEOUT, 10);
	if (status != 0)
		goto out;

	/* read data */
	wr32(hw, TXGBE_I2C_DATA_CMD,
	     byte_offset | TXGBE_I2C_DATA_CMD_STOP);
	wr32(hw, TXGBE_I2C_DATA_CMD, TXGBE_I2C_DATA_CMD_READ);

	/* wait for read complete */
	status = txgbe_po32m(hw, TXGBE_I2C_RAW_INTR_STAT,
			     TXGBE_I2C_INTR_STAT_RX_FULL,
			     TXGBE_I2C_INTR_STAT_RX_FULL,
			     TXGBE_I2C_TIMEOUT, 10);
	if (status != 0)
		goto out;

	*data = 0xFF & rd32(hw, TXGBE_I2C_DATA_CMD);

out:
	return status;
}

/**
 *  txgbe_switch_i2c_slave_addr - Switch I2C slave address
 *  @hw: pointer to hardware structure
 *  @dev_addr: slave addr to switch
 *
 **/
s32 txgbe_switch_i2c_slave_addr(struct txgbe_hw *hw, u8 dev_addr)
{
	wr32(hw, TXGBE_I2C_ENABLE, 0);
	wr32(hw, TXGBE_I2C_TAR, dev_addr >> 1);
	wr32(hw, TXGBE_I2C_ENABLE, 1);
	return 0;
}

/**
 *  txgbe_read_i2c_byte - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: device address
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 txgbe_read_i2c_byte(struct txgbe_hw *hw, u8 byte_offset,
			u8 dev_addr, u8 *data)
{
	txgbe_switch_i2c_slave_addr(hw, dev_addr);

	return txgbe_read_i2c_byte_int(hw, byte_offset, dev_addr,
				       data, true);
}

/**
 *  txgbe_read_i2c_word_int - Reads 16 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @data: value read
 *  @lock: true if to take and release semaphore
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 txgbe_read_i2c_word_int(struct txgbe_hw *hw, u16 byte_offset,
			    u8 __always_unused dev_addr, u16 *data, bool __always_unused lock)
{
	s32 status = 0;

	if (hw->phy.sfp_type == txgbe_sfp_type_1g_cu_core0 ||
	    hw->phy.sfp_type == txgbe_sfp_type_1g_cu_core1) {
		/* reg offset format 0x000yyyyy */
		byte_offset &= 0x1f;

		/* wait tx empty */
		status = txgbe_po32m(hw, TXGBE_I2C_RAW_INTR_STAT,
				     TXGBE_I2C_INTR_STAT_TX_EMPTY, TXGBE_I2C_INTR_STAT_TX_EMPTY,
		   TXGBE_I2C_TIMEOUT, 10);
		if (status != 0)
			goto out;

		/* write reg_offset */
		wr32(hw, TXGBE_I2C_DATA_CMD, (u8)byte_offset | TXGBE_I2C_DATA_CMD_STOP);

		usec_delay(TXGBE_I2C_TIMEOUT);
		/* wait tx empty */
		status = txgbe_po32m(hw, TXGBE_I2C_RAW_INTR_STAT,
				     TXGBE_I2C_INTR_STAT_TX_EMPTY, TXGBE_I2C_INTR_STAT_TX_EMPTY,
		   TXGBE_I2C_TIMEOUT, 10);
		if (status != 0)
			goto out;

		/* read data */
		wr32(hw, TXGBE_I2C_DATA_CMD, TXGBE_I2C_DATA_CMD_READ);
		wr32(hw, TXGBE_I2C_DATA_CMD, TXGBE_I2C_DATA_CMD_READ | TXGBE_I2C_DATA_CMD_STOP);

		/* wait for read complete */
		status = txgbe_po32m(hw, TXGBE_I2C_RAW_INTR_STAT,
				     TXGBE_I2C_INTR_STAT_RX_FULL, TXGBE_I2C_INTR_STAT_RX_FULL,
		   TXGBE_I2C_TIMEOUT, 10);
		if (status != 0)
			goto out;

		*data = 0xFF & rd32(hw, TXGBE_I2C_DATA_CMD);
		*data <<= 8;
		*data += 0xFF & rd32(hw, TXGBE_I2C_DATA_CMD);
	}

out:
	return status;
}

s32 txgbe_read_i2c_word(struct txgbe_hw *hw, u16 byte_offset,
			u8 dev_addr, u16 *data)
{
	txgbe_switch_i2c_slave_addr(hw, dev_addr);

	return txgbe_read_i2c_word_int(hw, byte_offset, dev_addr,
						   data, true);
}

s32 txgbe_read_i2c_sfp_phy(struct txgbe_hw *hw, u16 byte_offset,
			   u16 *data)
{
	txgbe_init_i2c_sfp_phy(hw);

	return txgbe_read_i2c_word(hw, byte_offset,
					 TXGBE_I2C_EEPROM_DEV_ADDR3,
					 data);
}

/**
 *  txgbe_tn_check_overtemp - Checks if an overtemp occurred.
 *  @hw: pointer to hardware structure
 *
 *  Checks if the LASI temp alarm status was triggered due to overtemp
 **/
s32 txgbe_check_overtemp(struct txgbe_hw *hw)
{
	s32 status = 0;
	u32 ts_state;

	/* Check that the LASI temp alarm status was triggered */
	ts_state = rd32(hw, TXGBE_TS_ALARM_ST);

	if (ts_state & TXGBE_TS_ALARM_ST_DALARM)
		status = TXGBE_ERR_UNDERTEMP;
	else if (ts_state & TXGBE_TS_ALARM_ST_ALARM)
		status = TXGBE_ERR_OVERTEMP;

	return status;
}
