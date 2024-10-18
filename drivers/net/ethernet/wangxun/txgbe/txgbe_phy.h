/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_PHY_H_
#define _TXGBE_PHY_H_

#include "txgbe.h"

#define TXGBE_I2C_EEPROM_DEV_ADDR       0xA0
#define TXGBE_I2C_EEPROM_DEV_ADDR2      0xA2

/*fiber to copper module inter reg i2c addr */
#define TXGBE_I2C_EEPROM_DEV_ADDR3      0xAC
#define TXGBE_I2C_10G_SFP_LINK_STATUS   BIT(10)
#define TXGBE_I2C_PHY_LOCAL_RX_STATUS   BIT(12)
#define TXGBE_I2C_PHY_REMOTE_RX_STATUS  BIT(13)

/* EEPROM byte offsets */
#define TXGBE_SFF_IDENTIFIER            0x0
#define TXGBE_SFF_IDENTIFIER_SFP        0x3
#define TXGBE_SFF_VENDOR_OUI_BYTE0      0x25
#define TXGBE_SFF_VENDOR_OUI_BYTE1      0x26
#define TXGBE_SFF_VENDOR_OUI_BYTE2      0x27
#define TXGBE_SFF_1GBE_COMP_CODES       0x6
#define TXGBE_SFF_10GBE_COMP_CODES      0x3
#define TXGBE_SFF_CABLE_TECHNOLOGY      0x8
#define TXGBE_SFF_CABLE_SPEC_COMP       0x3C
#define TXGBE_SFF_DDM_IMPLEMENTED		0x40
#define TXGBE_SFF_SFF_8472_SWAP         0x5C
#define TXGBE_SFF_SFF_8472_COMP         0x5E

/* Bitmasks */
#define TXGBE_SFF_DA_PASSIVE_CABLE      0x4
#define TXGBE_SFF_DA_ACTIVE_CABLE       0x8
#define TXGBE_SFF_DA_SPEC_ACTIVE_LIMITING       0x4
#define TXGBE_SFF_1GBASESX_CAPABLE      0x1
#define TXGBE_SFF_1GBASELX_CAPABLE      0x2
#define TXGBE_SFF_1GBASET_CAPABLE       0x8
#define TXGBE_SFF_10GBASESR_CAPABLE     0x10
#define TXGBE_SFF_10GBASELR_CAPABLE     0x20
#define TXGBE_SFF_ADDRESSING_MODE       0x4
/* Bit-shift macros */
#define TXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT        24
#define TXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT        16
#define TXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT        8

/* Vendor OUIs: format of OUI is 0x[byte0][byte1][byte2][00] */
#define TXGBE_SFF_VENDOR_OUI_TYCO       0x00407600
#define TXGBE_SFF_VENDOR_OUI_FTL        0x00906500
#define TXGBE_SFF_VENDOR_OUI_AVAGO      0x00176A00
#define TXGBE_SFF_VENDOR_OUI_INTEL      0x001B2100

/* SFP+ SFF-8472 Compliance */
#define TXGBE_SFF_SFF_8472_UNSUP        0x00

s32 txgbe_check_reset_blocked(struct txgbe_hw *hw);

s32 txgbe_identify_module(struct txgbe_hw *hw);
s32 txgbe_identify_sfp_module(struct txgbe_hw *hw);
s32 txgbe_check_overtemp(struct txgbe_hw *hw);
s32 txgbe_init_i2c(struct txgbe_hw *hw);
s32 txgbe_switch_i2c_slave_addr(struct txgbe_hw *hw, u8 dev_addr);
s32 txgbe_read_i2c_byte(struct txgbe_hw *hw, u8 byte_offset,
			u8 dev_addr, u8 *data);

s32 txgbe_read_i2c_eeprom(struct txgbe_hw *hw, u8 byte_offset,
			  u8 *eeprom_data);
s32 txgbe_read_i2c_sff8472(struct txgbe_hw *hw, u8 byte_offset,
			   u8 *sff8472_data);
s32 txgbe_read_i2c_word(struct txgbe_hw *hw, u16 byte_offset,
			u8 dev_addr, u16 *data);
s32 txgbe_read_i2c_sfp_phy(struct txgbe_hw *hw, u16 byte_offset,
			   u16 *data);

#endif /* _TXGBE_PHY_H_ */
