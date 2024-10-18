/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */
#ifndef _TXGBE_MBX_H_
#define _TXGBE_MBX_H_

#define TXGBE_VF_GET_FW_VERSION    0x11

#define TXGBE_VF_MBX_INIT_TIMEOUT  2000
#define TXGBE_VT_MSGTYPE_ACK       0x80000000

/* mailbox API, legacy requests */
#define TXGBE_VF_RESET             0x01
#define TXGBE_VF_SET_MAC_ADDR      0x02
#define TXGBE_VF_SET_MULTICAST     0x03
#define TXGBE_VF_SET_VLAN          0x04
#define TXGBE_VF_SET_LPE           0x05
#define TXGBE_VF_SET_MACVLAN       0x06
#define TXGBE_VF_API_NEGOTIATE     0x08

#define TXGBE_VF_GET_QUEUES        0x09
#define TXGBE_VF_GET_RETA          0x0a
#define TXGBE_VF_GET_RSS_KEY       0x0b
#define TXGBE_VF_UPDATE_XCAST_MODE 0x0c
#define TXGBE_VF_GET_LINK_STATE    0x10

#define TXGBE_VF_BACKUP            0x8001

#define TXGBE_VT_MSGTYPE_ACK    0x80000000
#define TXGBE_VT_MSGTYPE_NACK   0x40000000
#define TXGBE_VT_MSGTYPE_CTS    0x20000000
#define TXGBE_VT_MSGINFO_SHIFT  16
/* bits 23:16 are used for extra info for certain messages */
#define TXGBE_VT_MSGINFO_MASK   (0xFF << TXGBE_VT_MSGINFO_SHIFT)

#define TXGBE_VF_MC_TYPE_WORD           3

#define TXGBE_VF_MBX_INIT_DELAY         500  /* microseconds between retries */

/* GET_QUEUES return data indices within the mailbox */
#define TXGBE_VF_TX_QUEUES      1       /* number of Tx queues supported */
#define TXGBE_VF_RX_QUEUES      2       /* number of Rx queues supported */
#define TXGBE_VF_TRANS_VLAN     3       /* Indication of port vlan */
#define TXGBE_VF_DEF_QUEUE      4       /* Default queue offset */

enum txgbe_pfvf_api_rev {
	txgbe_mbox_api_null,
	txgbe_mbox_api_10,      /* API version 1.0, linux/freebsd VF driver */
	txgbe_mbox_api_11,      /* API version 1.1, linux/freebsd VF driver */
	txgbe_mbox_api_12,      /* API version 1.2, linux/freebsd VF driver */
	txgbe_mbox_api_13,      /* API version 1.3, linux/freebsd VF driver */
	txgbe_mbox_api_20,      /* API version 2.0, solaris Phase1 VF driver */
	//txgbe_mbox_api_max, /* indicates that API version is not known */
	txgbe_mbox_api_unknown /* indicates that API version is not known */
};

void txgbevf_init_mbx_params_vf(struct txgbe_hw *hw);

#endif
