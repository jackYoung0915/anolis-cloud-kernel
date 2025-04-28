// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_PHY_BOOTIS_H_
#define _NBL_PHY_BOOTIS_H_

#include "nbl_core.h"
#include "nbl_hw.h"
#include "nbl_phy.h"

#define NBL_MV_GET_RESULT_TIMES 10
#define INIT_TRY_TIME(t) ((t) = 5)
/* default vlan id */
#define NBL_VLAN_DEFAULT_VID  0

/* direction flag bit tbl */
#define NBL_MV_DIRECTION_DOWN		(1)

#define NBL_MV_DN_VSI_SEL_FROM_MACTBL 1
#define NBL_STATIC_PRJ_ID			(0x00000000)
#define NBL_STATIC_VERSION_ID			(0x00000004)
#define NBL_STATIC_INIT_DONE			(0x00000010)

enum nbl_static_done {
	NBL_STATIC_INIT_DONE_RSV0,
	NBL_STATIC_INIT_DONE_RSV1,
	NBL_STATIC_INIT_DONE_PTLP,
	NBL_STATIC_INIT_DONE_H_PCOMPLETE,
	NBL_STATIC_INIT_DONE_E_PCOMPLETE,
	NBL_STATIC_INIT_DONE_H_PCIE,
	NBL_STATIC_INIT_DONE_E_PCIE,
};

#define NBL_STATIC_INIT_DONE_VALUE	(BIT(NBL_STATIC_INIT_DONE_RSV0) | \
					 BIT(NBL_STATIC_INIT_DONE_RSV1) | \
					 BIT(NBL_STATIC_INIT_DONE_PTLP) | \
					 BIT(NBL_STATIC_INIT_DONE_H_PCOMPLETE) | \
					 BIT(NBL_STATIC_INIT_DONE_E_PCOMPLETE) | \
					 BIT(NBL_STATIC_INIT_DONE_E_PCIE))

/* eth */
static __maybe_unused u64 eth_base_addr[] = {
	0x01000000,
	0x01800000,
};

#define ETH_PORT_PKT_MAX_LENGTH (0x3ffc)
#define ETH_PORT_PKT_MIN_LENGTH (0x3c)

enum nbl_bootis_fwd_type {
	NBL_BOOTIS_FWD_DROP		= 0,
	NBL_BOOTIS_FWD_NORMAL		= 1,
	NBL_BOOTIS_FWD_UPCALL		= 2,
	NBL_BOOTIS_FWD_CPU_ASSIGNED	= 3,
};

enum nbl_bootis_dport_type {
	NBL_BOOTIS_DPORT_ETH		= 0,
	NBL_BOOTIS_DPORT_HOST		= 1,
	NBL_BOOTIS_DPORT_ECPU		= 2,
	NBL_BOOTIS_DPORT_ICPU		= 3,
};

/* n20x eth reg */
union nbl_eth_ip_reset {
	struct ip_reset {
		u32 rx_rst:1;
		u32 tx_rst:1;
		u32 csr_rst:1;
		u32 rsv0:1;
		u32 stats_snapshot:1;
		u32 rsv1:27;
	} info;
#define NBL_ETH_IP_RESET_SIZE		(sizeof(struct ip_reset))
	u8 data[NBL_ETH_IP_RESET_SIZE];
};

#define	DF200_ETH_RESET_REG(eth)	(eth_base_addr[(eth)] + 0x00000010) /* ETH reset register */

union nbl_eth_ip_status {
	struct ip_status {
		u32 linkup:1;
		u32 cdr_lock:1;
		u32 rx_pcs_ready:1;
		u32 rx_block_lock:1;
		u32 ehip_ready:1;
		u32 rx_am_lock:1;
		u32 rx_hi_ber:1;
		u32 tx_pll_locked:1;
		u32 tx_lanes_stable:1;
		u32 rx_pcs_fully_aligned:1;
		u32 remote_fault_status:1;
		u32 local_fault_status:1;
		u32 rsv1:20;
	} info;
#define NBL_ETH_IP_STATUS_SIZE		(sizeof(struct ip_status))
	u8 data[NBL_ETH_IP_STATUS_SIZE];
};

#define DF200_ETH_IP_STAT_REG(eth)		\
	(eth_base_addr[(eth)] + 0x00000020) /* ETH IP state */
#define DF200_LOGIC_MAC_TX_EN_REG(eth)		\
	(eth_base_addr[(eth)] + 0x00000180) /* ETH MAC TX enable register */
#define DF200_LOGIC_MAC_RX_EN_REG(eth)		\
	(eth_base_addr[(eth)] + 0x00000100) /* ETH MAC RX enable register */

union nbl_eth_rx_pkt_len_reg {
	struct eth_rx_pkt_len_reg {
		u32 min_pkt_len:16;
		u32 max_pkt_len:16;
	} info;
#define NBL_ETH_RX_PKT_LEN_REG_SIZE		(sizeof(struct eth_rx_pkt_len_reg))
	u8 data[NBL_ETH_RX_PKT_LEN_REG_SIZE];
};

#define DF200_ETH_RX_PKT_LEN_REG(eth)	\
	(eth_base_addr[(eth)] + 0x00000104) /* ETH rx pkt length limit register */

union nbl_eth_cfg_led_act_level {
	struct {
		u32 link_led_level:1;
		u32 g_r_led_leve:1;
		u32 force_led_level:1;
		u32 force_led_en:1;
#define NBL_FORCE_LED_BLINKING_FREQUENCE	(2) /* blinking times per second */
		u32 rsv:28;
	};
	u32 data;
};

#define DF200_ETH_LED_CTRL_REG(eth)	\
	(eth_base_addr[(eth)] + 0x00000014)

static __maybe_unused u64 eth_xcvr_base_addr[] = {
	0x000000a0,
	0x000000b0,
	0x000000c0,
	0x000000d0,
};

union nbl_eth_xcvr_avmm_ctrl_reg {
	struct eth_xcvr_avmm_ctrl_reg {
		u32 rw:1;
		u32 rsv0:3;
		u32 rdy:1;
		u32 rsv1:3;
		u32 success:1;
		u32 rsv2:23;
	} info;
#define NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE		(sizeof(struct eth_xcvr_avmm_ctrl_reg))
	u8 data[NBL_ETH_XCVR_AVMM_CTRL_REG_SIZE];
};

#define NBL_ETH_XCVR_AVMM_CTRL_REG(eth, l)	(eth_base_addr[(eth)] + eth_xcvr_base_addr[(l)])

struct nbl_eth_xcvr_avmm_data_reg {
	u32 data:8;
	u32 rsv0:24;
};

#define NBL_ETH_XCVR_AVMM_WDATA_REG(eth, l)	\
	(eth_base_addr[(eth)] + eth_xcvr_base_addr[(l)] + 0x4)
#define NBL_ETH_XCVR_AVMM_RDATA_REG(eth, l)	\
	(eth_base_addr[(eth)] + eth_xcvr_base_addr[(l)] + 0xc)

struct nbl_eth_xcvr_avmm_addr_reg {
	u32 addr:19;
	u32 rsv0:13;
};

/* bit[7:0] PMA attribute data, for loopback setting,
 * bit[0]=1/0 indicates to select/disable internal serial loopback
 * bit[4]=1/0 indicates to select/disable reverse parallel loopback
 */
#define NBL_PMA_ATTRIBUTE_DATA_L	(0x84)
/* bit[7:0] PMA attribute data, for loopback setting,
 * bit[0]=1 indicates to change internal serial loopback settings
 * bit[1]=1 indicates to change reverse parallel loopback settings
 */
#define NBL_PMA_ATTRIBUTE_DATA_H	(0x85)

enum pma_op_data {
	OP_DATA_DISABLE_ALL		= 0,
	OP_DATA_ENABLE_TX		= BIT(0),
	OP_DATA_ENABLE_RX		= BIT(1),
	OP_DATA_ENABLE_TX_OUTPUT	= BIT(2),
	OP_DATA_INIT_ADAPTATION		= 0x0001,
	OP_DATA_CONTINUOUS_ADAPTATION	= 0x0006,
	OP_DATA_READ_RECEIVEER_TUNING	= 0x0b00,
	OP_DATA_INTERNAL_LOOPBACK	= 0x0100,
	OP_DATA_READ_EFFORT_LEVEL	= 0x0118,
	OP_DATA_FULL_EFFORT_LEVEL	= 0x0001,
};

/* bit[7:0] PMA attribute code lower, code=0x0008 indicates internal serial loopback
 * and reverse parallel loopback control
 */
#define NBL_PMA_ATTRIBUTE_CODE_L	(0x86)
/* bit[7:0] PMA attribute code high */
#define NBL_PMA_ATTRIBUTE_CODE_H	(0x87)

enum pma_op_code {
	OP_CODE_ENABLE_DISABLE			= 0x0001,
	OP_CODE_PRBS_SETTINGS			= 0x0002,
	OP_CODE_LOOPBACK_SETTING		= 0x0008,
	OP_CODE_RECEIVER_TUNING_CONTROL		= 0x000a,
	OP_CODE_READ_RECEIVER_TUNING		= 0x0126,
	OP_CODE_READ_PMA_ANALOG_PARAMETERS	= 0x002c,
	OP_CODE_WRITE_PMA_ANALOG_PARAMETERS	= 0x006c,
};

/* bit[7:0] lower byte of the PMA attribute code return value */
#define NBL_PMA_ATTRIBUTE_CODE_RETURN_L	(0x88)

enum pma_op_result {
	OP_RESULT_PMA_ADAPTATION_FINISHED	= 0x80,
};

/* bit[7:0] upper byte of the PMA attribute code return value */
#define NBL_PMA_ATTRIBUTE_CODE_RETURN_H	(0x89)
/* bit[7]=1 indicates the PMA attribute has been transmitted to PMA successfully */
#define NBL_PMA_ATTRIBUTE_SEND_OK	(0x8A)
/* bit[0]=0 indicates the PMA has finished acting on the PMA attribute
 * and the PMA attribute code return value is available on registers 0x88/0x89
 */
#define NBL_PMA_ATTRIBUTE_FINISHED	(0x8B)
/* bit[0]=1 to start load the contents of registers 0x84 to 0x87 to the PMA */
#define NBL_PMA_ATTRIBUTE_LOAD_START	(0x90)
#define NBL_PMA_ANALOG_RESET_REG0	(0x200)
#define NBL_PMA_ANALOG_RESET_REG1	(0x201)
#define NBL_PMA_ANALOG_RESET_REG2	(0x202)
#define NBL_PMA_ANALOG_RESET_REG3	(0x203)
#define NBL_PMA_LOAD_INITIAL_SETTING	(0x91)
#define NBL_PMA_ENABLE_CALIBRATION	(0x95)
#define NBL_PMA_ANALOG_RESET_STATUS	(0x207)
#define PMA_ANALOG_RESET_DONE_VALUE	(0x80)

#define NBL_ETH_XCVR_AVMM_ADDR_REG(eth, l)	\
	(eth_base_addr[(eth)] + eth_xcvr_base_addr[(l)] + 0x8)

#define NBL_ETH_AVMM_REGISTER_MAP(eth)	(eth_base_addr[(eth)] + 0x00200000)

/* intel eth ip registers address */
enum eth_ip_reg_map {
	ETH_IP_DR_STATUS			= 0x0,
	ETH_IP_DR_CONTROL			= 0x09,
	ETH_IP_DR_RESET				= 0x0e,
	ETH_IP_DR_CFG_CH_EN			= 0x13,
	ETH_IP_DR_CFG_CH_MODE			= 0x14,
	ETH_IP_DR_CFG_FEC			= 0x15,
	ETH_IP_PHY_CONFIGURATION		= 0x310,
};

union eth_ip_dr_ch_en_cfg {
	struct {
		u32 en_lane0_25G:1;
		u32 en_lane1_25G:1;
		u32 en_lane2_25G:1;
		u32 en_lane3_25G:1;
		u32 rsv0:12;
		u32 en_lane0_100G:1;
		u32 rsv1:15;
	};
	u32 data;
};

union eth_ip_dr_ch_mode_cfg {
	struct {
#define CHANNEL_MODE_MAC_PCS		(0x5)
		u32 ch0_mode_sel:3;
		u32 rsv0:5;
		u32 ch1_mode_sel:3;
		u32 rsv1:5;
		u32 ch2_mode_sel:3;
		u32 rsv2:5;
		u32 ch3_mode_sel:3;
		u32 rsv3:5;
	};
	u32 data;
};

union eth_ip_dr_fec_cfg {
	struct {
		u32 fec_en_ch0:1;
		u32 fec_en_ch1:1;
		u32 fec_en_ch2:1;
		u32 fec_en_ch3:1;
		u32 rsv0:4;
		u32 fec_mode:1;
#define FEC_MODE_KR_FEC_528	0
#define FEC_MODE_KR_FEC_544	1
		u32 fec_protocol:1;
#define FEC_PROTOCOL_NRZ	0
#define FEC_PROTOCOL_PAM4	1
		u32 rsv1:22;
	};
	u32 data;
};

#define PHY_CONFIGURATION_SOFT_TX_RESET		BIT(1)
#define PHY_CONFIGURATION_SOFT_RX_RESET		BIT(2)

/* pcompleter_ecpu */
#define PCOMPLETER_ECPU_BASE (0x0000000000100000)

// padpt ecpu base
#define PADAPT_ECPU_BASE (0x0000000000840000)
#define PADAPT_ECPU_TX_ENABLE (PADAPT_ECPU_BASE + 0x00000100)
#define PADAPT_ECPU_RX_ENABLE (PADAPT_ECPU_BASE + 0x00000104)

/* urmux module */
#define NBL_URMUX_BASE (0x0000000000A00000)
#define NBL_URMUX_DPORT_MAP_TABLE(t) (NBL_URMUX_BASE + 0x00010000 + 4 * (t))
#define NBL_URMUX_DPORT_MAP_NUM (32)
#define NBL_URMUX_DPORT_MAP_VALID_SHIFT (31)
#define NBL_URMUX_DPORT_MAP_DPORT_SHIFT (11)
#define NBL_URMUX_DPORT_MAP_DPORT_INFO_SHIFT (0)
#define NBL_URMUX_DPORT_MAP_DPORT_HOST (0x1 << NBL_URMUX_DPORT_MAP_DPORT_SHIFT)
#define NBL_URMUX_DPORT_MAP_DPORT_ECPU (0x2 << NBL_URMUX_DPORT_MAP_DPORT_SHIFT)

/* dvn module */
#define NBL_DVN_BASE (0x0000000005900000)
#define NBL_DVN_ECPU_QUEUE_RESET_TABLE (NBL_DVN_BASE + 0x00000100)

/* uvn module */
#define NBL_UVN_BASE (0x0000000005C00000)
#define NBL_UVN_ECPU_QUEUE_RESET_TABLE (NBL_UVN_BASE + 0x00000100)

/* upa module */
#define NBL_UPA_BASE (0x0000000002810000)
#define NBL_UPA_CAPTURE_INFO_REG(r)	(NBL_UPA_BASE + 0x00000524 + 4 * (r))
#define NBL_UPA_NO_RECOG_EN_REG		(NBL_UPA_BASE + 0x00000534)

union nbl_pa_pcmrt_key_tbl {
	struct pcmrt_key_tbl {
		u32 dmac_type:4;
#define PCMRT_DMAC_TYPE_UCAST		0
#define PCMRT_DMAC_TYPE_MCAST		1
#define PCMRT_DMAC_TYPE_BCAST		2
#define PCMRT_DMAC_TYPE_L3_MCAST	3
#define PCMRT_DMAC_TYPE_LLDP		4
#define PCMRT_DMAC_TYPE_V6_MCAST	5
		u32 etype:4;
#define PCMRT_ETYPE_IPV4		0
#define PCMRT_ETYPE_ARP			1
#define PCMRT_ETYPE_RARP		2
#define PCMRT_ETYPE_IPV6		3
#define PCMRT_ETYPE_EXT_0		4
#define PCMRT_ETYPE_EXT_1		5
#define PCMRT_ETYPE_EXT_2		6
#define PCMRT_ETYPE_EXT_3		7
#define PCMRT_ETYPE_EXT_4		8
#define PCMRT_ETYPE_EXT_5		9
#define PCMRT_ETYPE_EXT_6		10
#define PCMRT_ETYPE_EXT_7		11
		u32 ip_protocol_type:4;
		u32 dport_type:4;
		u32 tcp_ctrl_bits_type:3;
		u32 valid:1;
		u32 rsv:12;
	} info;
#define NBL_PA_PCMRT_KEY_TBL_SIZE	(sizeof(struct pcmrt_key_tbl))
	u8 data[NBL_PA_PCMRT_KEY_TBL_SIZE];
};

#define NBL_UPA_PCMRT_KEY_TABLE(t)	(NBL_UPA_BASE + 0x00001000 + 4 * (t))

union nbl_pa_pcmrt_mask_tbl {
	struct pcmrt_mask_tbl {
		u32 mask_dmac_type:1;
		u32 mask_etype:1;
		u32 mask_ip_protocol_type:1;
		u32 mask_dport_type:1;
		u32 mask_tcp_ctrl_bits_type:1;
		u32 rsv:27;
	} info;
#define NBL_PA_PCMRT_MASK_TBL_SIZE	(sizeof(struct pcmrt_mask_tbl))
	u8 data[NBL_PA_PCMRT_MASK_TBL_SIZE];
};

#define NBL_UPA_PCMRT_MASK_TABLE(t)	(NBL_UPA_BASE + 0x00002000 + 4 * (t))

union nbl_pa_pcmrt_action_tbl {
	struct pcmrt_action_tbl {
		u32 fwd:2;
		u32 mcc_idx:13;
		u32 mcc_en:1;
		u32 rsv:16;
	} info;
#define NBL_PA_PCMRT_ACTION_TBL_SIZE	(sizeof(struct pcmrt_action_tbl))
	u8 data[NBL_PA_PCMRT_ACTION_TBL_SIZE];
};

#define NBL_UPA_PCMRT_ACTION_TABLE(t)	(NBL_UPA_BASE + 0x00003000 + 4 * (t))

union nbl_pa_pcmrt_eth_action_tbl {
	struct pcmrt_eth_action_tbl {
		u32 dport:3;
		u32 dport_id:11;
		u32 dport_info:11;
		u32 rss_en:1;
		u32 valid:1;
		u32 rsv:5;
	} info;
#define NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE	(sizeof(struct pcmrt_eth_action_tbl))
	u8 data[NBL_PA_PCMRT_ETH_ACTION_TBL_SIZE];
};

static __maybe_unused u64 upa_pcmrt_action_addr[] = {
	0x00003100,
	0x00003200,
};

#define NBL_UPA_PCMRT_ETH_ACTION_TABLE(eth, t)	\
	(NBL_UPA_BASE + upa_pcmrt_action_addr[(eth)] + 4 * (t))

#define NBL_UPA_EXT_ETYPE_REG(t)	(NBL_UPA_BASE + 0x0000050c + 4 * (t))

union nbl_umcc_table_wdata {
	struct umcc_table_wdata {
		u64 next_pntr:13;
		u64 tail:1;
		u64 dport:3;
		u64 dport_id:11;
		u64 dport_info:22;
		u64 rss:1;
		u64 valid:1;
		u64 rsv:12;
	} info;
#define NBL_UMCC_TABLE_WDATA_SIZE	(sizeof(struct umcc_table_wdata))
	u8 data[NBL_UMCC_TABLE_WDATA_SIZE];
};

#define NBL_UMCC_TABLE_CTRL_REG		(0x02830000 + 0x00001000)
#define NBL_UMCC_TABLE_WDATA_REG		(0x02830000 + 0x00001004)

/* dpa module */
#define NBL_DPA_BASE (0x0000000003010000)
#define NBL_DPA_CAPTURE_INFO_REG(r)	(NBL_DPA_BASE + 0x00000524 + 4 * (r))
#define NBL_DPA_NO_RECOG_EN_REG		(NBL_DPA_BASE + 0x00000534)
#define NBL_DPA_PCMRT_TABLE(t)		(NBL_DPA_BASE + 0x00001000 + 4 * (t))
#define NBL_DPA_PCMRT_MASK_TABLE(r)	(NBL_DPA_BASE + 0x00002000 + 4 * (r))
#define NBL_DPA_PCMRT_ACTION_TABLE(r)	(NBL_DPA_BASE + 0x00003000 + 4 * (r))

/* uflow module */
#define NBL_UFLOW_BASE (0x0000000002840000)
#define NBL_UFLOW_CTRL_REG (NBL_UFLOW_BASE + 0x00000804)

/* virtio notify bar addr */
#define NET_CQ_NOTIFY_ADDR (0x00100004)
#define BLK_CQ_NOTIFY_ADDR (0x00100008)
#define BLK_DMAP_NOTIFY_ADDR (0x0010000C)
#define PF2_NET_NOTIFY_ADDR (0x00101008)
#define PF3_NET_NOTIFY_ADDR (0x0010100c)

/* TXRX queue configure reg */
#define NBL_QUEUE_CONFIGURE_TABLE(t) (VIRTIO_NET_BASE + (32 * (t)))
#define NBL_QUEUE_CONFIGURE_TABLE_DW(t, w) \
	(NBL_QUEUE_CONFIGURE_TABLE(t) + (4 * (w)))
#define NBL_QUEUE_CONFIGURE_ENABLE_QUEUE BIT(16)
#define NBL_VIRTIO_NET_QUEUE_REST (VIRTIO_NET_BASE + 0x00024000)

/* uipro */
#define HW_UIPRO_BASE			0x02820000
#define UIPRO_CTRL_REG			(HW_UIPRO_BASE + 0x00000100)
#define UIPRO_MISMATCH_INFO_REG		(HW_UIPRO_BASE + 0x00000104)
#define UIPRO_ETH_PORT_REG(n)		(HW_UIPRO_BASE + 0x0000010c + 4 * (n))

/* uiflt */
#define HW_UIFLT_BASE			0x02850000
#define UIFLT_MKEY_TABLE(t)		(HW_UIFLT_BASE + 0x00001000 + ((1024 / 8) * (t)))
#define UIFLT_MASK_TABLE(t)		(HW_UIFLT_BASE + 0x00002000 + ((1024 / 8) * (t)))
#define UIFLT_ACTION_TABLE(t)		(HW_UIFLT_BASE + 0x00003000 + ((64 / 8) * (t)))

/* uepro */
#define HW_UEPRO_BASE			0x02860000
#define UP_VSI_ACTION_TABLE(i)		(HW_UEPRO_BASE + 0x00001000 + 8 * (i))

/* dipro */
#define HW_DIPRO_BASE			0x03020000
#define DIPRO_CTRL_REG			(HW_DIPRO_BASE + 0x00000100)
#define DIPRO_MISMATCH_INFO_REG		(HW_DIPRO_BASE + 0x00000104)
#define DOWN_VSI_ACTION_TABLE(i)	(HW_DIPRO_BASE + 0x00001000 + 4 * (i))
#define DIPRO_QUE_VSI_MAP_TABLE(i)	(HW_DIPRO_BASE + 0x00002000 + 4 * (i))

/* dflow */
#define HW_DFLOW_BASE			0x03040000
#define DFLOW_LAG_HASH_REG		(HW_DFLOW_BASE + 0x00000818)

/* depro */
#define HW_DEPRO_BASE			0x03060000
#define DEPRO_ETH_PORT_REG		(HW_DEPRO_BASE + 0x00000110)
#define DEPRO_LAG_REG			(HW_DEPRO_BASE + 0x00000134)

/* memt */
#define HW_MEMT_ECPU_BASE		0x03810000

enum nbl_dn_mv_fwd {
	NBL_DN_MV_FWD_DISCARD = 0,
	NBL_DN_MV_FWD_NORMAL,
	NBL_DN_MV_FWD_CAPTURE,
	NBL_DN_MV_FWD_RSV,
};

/* macvlan tbl add/del/modify/check */
enum nbl_macvlan_op {
	NBL_MEMT_OP_TYPE_SEARCH,
	NBL_MEMT_OP_TYPE_ADD,
	NBL_MEMT_OP_TYPE_MODIFY,
	NBL_MEMT_OP_TYPE_DEL
};

union nbl_memt_op {
	struct memt_op {
		u32 memt_op_type:2;
		u32 rsv0:6;
		u32 memt_op_start:1;
		u32 rsv1:7;
		u32 memt_op_flush_en:1;
		u32 rsv2:15;
	} info;
#define CPU_MEMT_OP_SIZE (sizeof(struct memt_op))
	u8 data[CPU_MEMT_OP_SIZE];
};

static __maybe_unused u64 memt_op_addr[] = {
	0x00001004,
	0x0000100c,
};

#define NBL_MEMT_OP_REG(eth)		(HW_MEMT_ECPU_BASE + memt_op_addr[(eth)])

union nbl_memt_status_op {
	struct nbl_memt_status {
		u32 memt_op_type: 2;
		u32 memt_op_success: 1;
		u32 memt_op_done: 1;
		u32 memt_op_errtype: 4;
		u32 rsv: 24;
	} info;
#define CPU_MEMT_STATUS_SIZE (sizeof(struct nbl_memt_status))
	u8 data[CPU_MEMT_STATUS_SIZE];
};

static __maybe_unused u64 memt_status_addr[] = {
	0x00001008,
	0x00001010,
};

#define NBL_MEMT_STATUS_REG(eth)	(HW_MEMT_ECPU_BASE + memt_status_addr[(eth)])

union nbl_cpu_memt_key_reg {
	struct cpu_memt_key {
		u32 vid: 12;
		u32 mac5: 8;
		u32 mac4: 8;
		u32 mac3_l: 4;
		u32 mac3_h: 4;
		u32 mac2: 8;
		u32 mac1: 8;
		u32 mac0: 8;
		u32 direction_down: 1;
		u32 eth_id: 1;
		u32 rsv: 2;
	} key;
#define CPU_MEMT_KEY_REG_SIZE (sizeof(struct cpu_memt_key))
	u8 data[CPU_MEMT_KEY_REG_SIZE];
};

static __maybe_unused u64 memt_key_addr[] = {
	0x00001100,
	0x00001120,
};

#define NBL_MEMT_KEY_REG(eth)	(HW_MEMT_ECPU_BASE + memt_key_addr[(eth)])

union nbl_mac_result_reg {
	union nbl_mac_result_u {
		struct up_mac_result_tbl {
			u64 straight: 1;
			u64 dport: 3;
			u64 dport_id: 9;
			u64 dport_info: 21;
			u64 rdma_bypass: 1;
			u64 rss: 1;
			u64 dport_id_high:2;
			u64 rsv: 26;
		} up_result;
		struct dn_mac_result_tbl {
			u64 src_vsi_idx: 9;
			u64 fwd: 2;
			u64 vsi_sel: 1;
			u64 dport: 3;
			u64 dport_id: 9;
			u64 dport_info: 11;
			u64 rss: 1;
			u64 dport_id_high:2;
			u64 src_vsi_idx_high:2;
			u64 rsv_high: 24;
		} dn_result;
	} info;
#define MAC_RESULT_TBL_SIZE (sizeof(union nbl_mac_result_u))
	u8 data[MAC_RESULT_TBL_SIZE];
};

static __maybe_unused u64 memt_result_idx_addr[] = {
	0x00001108,
	0x00001128,
};

#define NBL_MEMT_RESULT_TBL_IDX_REG(eth)	(HW_MEMT_ECPU_BASE + memt_result_idx_addr[(eth)])

static __maybe_unused u64 memt_result_tbl_addr[] = {
	0x0000110c,
	0x0000112c,
};

#define NBL_MEMT_RESULT_REG(eth)	(HW_MEMT_ECPU_BASE + memt_result_tbl_addr[(eth)])

/* PPE register */
/* dynamic_greg */
#define NBL_INIT_DONE_BIT			(0xffffffff)
#define NBL_DYNAMIC_REG_BASE			(0x06800000)
#define NBL_DYNAMIC_INIT_DONE			(NBL_DYNAMIC_REG_BASE + 0x00000010)
#define NBL_DYNAMIC_CLR_CNT			(NBL_DYNAMIC_REG_BASE + 0x0000001C)

union nbl_sfp28_tx_disable {
	struct sfp28_tx_disable_reg {
		/* DW0 */
		u32 tx_disable_lane0:1;
		u32 tx_disable_lane1:1;
		u32 tx_disable_lane2:1;
		u32 tx_disable_lane3:1;
		u32 rsv:28;
	} __packed info;
#define NBL_SFP28_TX_DISABLE_REG_WIDTH (sizeof(struct sfp28_tx_disable_reg))
	u8 data[NBL_SFP28_TX_DISABLE_REG_WIDTH];
};

#define NBL_LSP_BASE (0x001C0000)

static __maybe_unused u64 sfp_tx_disable[] = {
	0x00000164,
	0x000001a4,
};

#define NBL_ETH_SFP_TX_DISABLE(eth)	(NBL_LSP_BASE + sfp_tx_disable[(eth)])
#define NBL_SFP_I2C_ENDIAN_CFG	(NBL_LSP_BASE + 0x00000100)
#define NBL_SFP_I2C_ENDIAN_BIG	0x1

static __maybe_unused u64 sfp_modpresl_base[] = {
	0x0000015c,
	0x0000019c,
};

#define NBL_SFP_MODPRESL(eth)	(NBL_LSP_BASE + sfp_modpresl_base[(eth)])
#define NBL_SFP_PRESENT_BIT	(BIT(0))

union nbl_sfp_ctrl {
	struct sfp_ctrl_reg {
		/* DW0 */
		u32 sfp_wdata:8;
		u32 sfp_addr:8;
		u32 sfp_rw:1;
#define SFP_READ	1
#define SFP_WRITE	0
		u32 sfp_slave_addr:7;
#define NBL_SFF_8636_SLAVE_ADDR		0x50
		u32 sfp_byte_num:4;
#define NBL_I2C_READ_MAXLEN		4
		u32 sfp_chn:4;
	} __packed info;
#define NBL_SFP_CTRL_REG_WIDTH (sizeof(struct sfp_ctrl_reg))
	u8 data[NBL_SFP_CTRL_REG_WIDTH];
};

enum nbl_sff8636_offset {
	NBL_SFF8636_IDENTIFIER_OFFSET		= 128,
	NBL_SFF8636_DEVICE_TECH_OFFSET		= 147,
};

enum nbl_sfp_type {
	SFF8024_IDENTIFIER_VALUE_QSFP28	 = 0x11,
};

static __maybe_unused u64 sfp_ctrl_base[] = {
	0x00000140,
	0x00000180,
};

#define NBL_SFP_CTRL(eth)	(NBL_LSP_BASE + sfp_ctrl_base[(eth)])

static __maybe_unused u64 sfp_rdata_base[] = {
	0x00000144,
	0x00000184,
};

#define NBL_SFP_RDATA(eth, chl)	(NBL_LSP_BASE + sfp_rdata_base[(eth)] + ((chl) * 4))

static __maybe_unused u64 sfp_done_base[] = {
	0x00000158,
	0x00000198,
};

#define NBL_SFP_DONE(eth)	(NBL_LSP_BASE + sfp_done_base[(eth)])

union nbl_board_info {
	struct {
		u8 data[508];
		u32 crc;
	};
	struct {
		u8 version;
		u8 magic[7];
		u8 sn[32];
		u8 pn[16];
		u8 board_extra3;
		u8 product_version;
		u8 rsv1[6];
		u8 mac1[6];
		u8 mac2[6];
		u8 mac3[6];
		u8 mac4[6]; /* kernel pf2, LSB */
		u8 mac5[6]; /* kernel pf3, LSB */
		u8 mac6[6];
		u8 mac7[6];
		u8 mac8[6];
	};
};

#define NBL_BOARD_INFO_REG	(NBL_LSP_BASE + 0x00000600)

union nbl_up_vsi_action {
	struct up_action {
		u32 fwd: 1;
		u32 mirror: 1;
		u32 mirror_id: 2;
		u32 car_en: 1;
		u32 car_idx: 9;
		u32 push_ovlan: 16;
		u32 vlan_pop_cnt: 2;
		u32 vlan_push_cnt: 2;
		u32 vf_id: 8;
		u32 rsv: 22;
	} action;
#define UP_VSI_ACTION_TBL_SIZE (sizeof(struct up_action))
	u8 data[UP_VSI_ACTION_TBL_SIZE];
};

union nbl_down_vsi_action {
	struct dn_action {
		u32 default_vlanid: 12;
		u32 vlan_type: 2;
		u32 vlan_check_en: 1;
		u32 mirror: 1;
		u32 mirror_id: 2;
		u32 cos_map_mode: 3;
		u32 default_pri: 3;
		u32 straight: 1;
		u32 lag: 1;
		u32 dport_id: 2;
		u32 mac_lut_en: 1;
		u32 default_vlan_en: 1;
		u32 rsv: 2;
	} action;
#define DN_VSI_ACTION_TBL_SIZE (sizeof(struct dn_action))
	u8 data[DN_VSI_ACTION_TBL_SIZE];
};

union nbl_ecpu_qid_map_table {
	struct qid_map_table {
		u32 max_qid:16;
		u32 base_qid:16;
		u32 rsv0:8;
		u32 device_type:3;
		u32 rsv1:5;
		u32 notify_addr_l:16;
		u32 notify_addr_h:32;
		u32 rsv2:31;
		u32 valid:1;
	} info;
#define NBL_ECPU_QID_MAP_TBL_SIZE	(sizeof(struct qid_map_table))
	u8 data[NBL_ECPU_QID_MAP_TBL_SIZE];
};

#define NBL_NOTIFY_CONFIGURE_TABLE(t) (PCOMPLETER_ECPU_BASE + 0x00010000 + \
						NBL_ECPU_QID_MAP_TBL_SIZE * (t))

#define NBL_MSIX_BASE_IDX(f)	(PCOMPLETER_ECPU_BASE + 0x00008200 + 4 * (f))

union nbl_uflow_rss_reg {
	struct uflow_rss_reg {
		u32 hash_field_type_v4 : 1;
		u32 hash_field_type_v6 : 1;
		u32 hash_field_mask_dport : 1;
		u32 hash_field_mask_sport : 1;
		u32 hash_field_mask_dip : 1;
		u32 hash_field_mask_sip : 1;
		u32 rsv : 26;
	} info;
#define NBL_UFLOW_RSS_REG_SIZE		(sizeof(struct uflow_rss_reg))
	u8 data[NBL_UFLOW_RSS_REG_SIZE];
};

#define NBL_UFLOW_RSS_REG	(NBL_UFLOW_BASE + 0x00000810)

union nbl_uflow_rss_hash_func {
	struct uflow_rss_hash_func {
		u32 vsi_hash_func;
	} info;
#define NBL_UFLOW_RSS_HASH_FUNC_SIZE		(sizeof(struct uflow_rss_hash_func))
	u8 data[NBL_UFLOW_RSS_HASH_FUNC_SIZE];
};

#define NBL_UFLOW_RSS_HASH_FUNC_REG(t)	(NBL_UFLOW_BASE + 0x00001000 + \
		(t) * NBL_UFLOW_RSS_HASH_FUNC_SIZE)

union nbl_uflow_rss_group_ctrl_reg {
	struct uflow_rss_group_ctrl_reg {
		u32 rss_group_table_addr : 15;
		u32 rsv1 : 1;
		u32 rss_group_table_rw : 1;
		u32 rss_group_table_start : 1;
		u32 rsv2 : 14;
	} info;
#define NBL_UFLOW_RSS_GROUP_CTRL_REG_SIZE	(sizeof(struct uflow_rss_group_ctrl_reg))
	u8 data[NBL_UFLOW_RSS_GROUP_CTRL_REG_SIZE];
};

static __maybe_unused u64 rss_group_table_ctrl_func_addr[] = {
	0x00000180,
	0x000001a0,
	0x000001c0,
	0x000001e0,
};

#define NBL_UEPRO_RSS_GROUP_TABLE_CTRL_REG(f) (HW_UEPRO_BASE + rss_group_table_ctrl_func_addr[(f)])
#define NBL_UEPRO_RSS_GROUP_TABLE_CTRL_READ (1)
#define NBL_UEPRO_RSS_GROUP_TABLE_CTRL_WRITE (0)

union nbl_uflow_rss_group_table {
	struct uflow_rss_group_table {
		u32 rss_que_index0 : 12;
		u32 rsv1 : 4;
		u32 rss_que_index1 : 12;
		u32 rsv2 : 4;
		u32 rss_que_index2 : 12;
		u32 rsv3 : 4;
		u32 rss_que_index3 : 12;
		u32 rsv4 : 4;
	} info;
#define NBL_UFLOW_RSS_GROUP_TABLE_SIZE		(sizeof(struct uflow_rss_group_table))
	u8 data[NBL_UFLOW_RSS_GROUP_TABLE_SIZE];
};

static __maybe_unused u64 rss_group_table_wdata_func_addr[] = {
	0x00000188,
	0x000001a8,
	0x000001c8,
	0x000001e8,
};

#define NBL_UEPRO_RSS_GROUP_TABLE_WDATA_REG(f) (HW_UEPRO_BASE + \
		rss_group_table_wdata_func_addr[(f)])
#define NBL_UEPRO_RSS_TABLES_PER_GROUP 16
#define NBL_UEPRO_RSS_QUEUES_PER_GROUP 64
#define NBL_UEPRO_RSS_ECPU_MAX_GROUPS 2048

union nbl_uepro_vsi_tbl {
	struct uepro_vsi_tbl {
		u32 fwd:1;
#define NBL_UEPRO_FWD_TYPE_DROP		(0)
#define NBL_UEPRO_FWD_TYPE_NORMAL	(1)
		u32 mirror:1;
		u32 mirror_id:2;
		u32 car_en:1;
		u32 car_id:11;
		u32 vf_id:8;
		u32 rsv:8;
	} info;
#define NBL_UEPRO_VSI_TBL_SIZE	(sizeof(struct uepro_vsi_tbl))
	u8 data[NBL_UEPRO_VSI_TBL_SIZE];
};

#define NBL_UEPRO_VSI_TABLE(t)	(HW_UEPRO_BASE + 0x00002000 + (t) * NBL_UEPRO_VSI_TBL_SIZE)

union nbl_dvn_queue_table {
	struct dvn_queue_table {
		u64 used_baddr:48;
		u64 rsv0:16;
		u64 avail_baddr:48;
		u64 rsv1:16;
		u64 queue_baddr:48;
		u64 rsv2:16;
		u32 queue_size_mask_pow:4;
		u32 queue_type:1;
		u32 queue_enable:1;
		u32 extend_header_en:1;
		u32 interleave_seg_disable:1;
		u32 seg_disable:1;
		u32 rsv3:23;
		u32 rsv4;
	} info;
#define NBL_DVN_QUEUE_TABLE_SIZE	(sizeof(struct dvn_queue_table))
	u8 data[NBL_DVN_QUEUE_TABLE_SIZE];
};

#define NBL_DVN_ECPU_QUEUE_TABLE(t) (NBL_DVN_BASE + 0x00010000 + NBL_DVN_QUEUE_TABLE_SIZE * (t))

union nbl_dvn_queue_stat {
	struct dvn_queue_stat {
		u64 desc_fwd_cnt:16;
		u64 rsv0:16;
		u64 desc_drop_cnt:16;
		u64 rsv1:16;
		u64 pkt_fwd_cnt:16;
		u64 rsv2:16;
		u64 pkt_drop_cnt:16;
		u64 rsv3:16;
		u32 rsv4[4];
	} info;
#define NBL_DVN_QUEUE_STAT_SIZE	(sizeof(struct dvn_queue_stat))
	u8 data[NBL_DVN_QUEUE_STAT_SIZE];
};

#define NBL_DVN_ECPU_QUEUE_STAT(t) (NBL_DVN_BASE + 0x00040000 + NBL_DVN_QUEUE_STAT_SIZE * (t))

union nbl_uvn_queue_table {
	struct uvn_queue_table {
		u64 used_baddr;
		u64 avail_baddr;
		u64 queue_baddr;
		u32 queue_size_mask_pow:4;
		u32 queue_type:1;
		u32 queue_enable:1;
		u32 extend_header_en:1;
		u32 guest_csum_en:1;
		u32 half_offload_en:1;
		u32 host_id:1;
		u32 rsv0:22;
		u32 rsv1;
	} info;
#define NBL_UVN_QUEUE_TABLE_SIZE	(sizeof(struct uvn_queue_table))
	u8 data[NBL_UVN_QUEUE_TABLE_SIZE];
};

#define NBL_UVN_ECPU_QUEUE_TABLE(t) (NBL_UVN_BASE + 0x00010000 + NBL_UVN_QUEUE_TABLE_SIZE * (t))

union nbl_uvn_queue_stat {
	struct uvn_queue_stat {
		u32 desc_pre_cnt;
		u32 desc_wb_cnt;
		u32 pkt_in_cnt;
		u32 pkt_out_cnt;
		u32 pkt_drop_cnt;
		u32 rsv4[3];
	} info;
#define NBL_UVN_QUEUE_STAT_SIZE	(sizeof(struct uvn_queue_stat))
	u8 data[NBL_UVN_QUEUE_STAT_SIZE];
};

#define NBL_UVN_ECPU_QUEUE_STAT(t) (NBL_UVN_BASE + 0x00030000 + NBL_UVN_QUEUE_STAT_SIZE * (t))

/* dsch module */
#define NBL_DSCH_BASE (0x0000000006100000)
union nbl_dsch_vn_q2tc_tbl {
	struct dsch_vn_q2tc_tbl {
		u32 tc_id : 14;
		u32 rsv : 17;
		u32 vld : 1;
	} info;
#define NBL_DSCH_VN_Q2TC_TBL_SIZE	(sizeof(struct dsch_vn_q2tc_tbl))
	u8 data[NBL_DSCH_VN_Q2TC_TBL_SIZE];
};

#define NBL_DSCH_VN_Q2TC_TBL(t)	(NBL_DSCH_BASE + 0x00010000 + NBL_DSCH_VN_Q2TC_TBL_SIZE * (t))

union nbl_dsch_vn_n2g_tbl {
	struct dsch_vn_n2g_tbl {
		u32 grpid : 8;
		u32 rsv : 23;
		u32 vld : 1;
	} info;
#define NBL_DSCH_VN_N2G_TBL_SIZE	(sizeof(struct dsch_vn_n2g_tbl))
	u8 data[NBL_DSCH_VN_N2G_TBL_SIZE];
};

#define NBL_DSCH_VN_N2G_TBL(t)	(NBL_DSCH_BASE + 0x00060000 + NBL_DSCH_VN_N2G_TBL_SIZE * (t))

union nbl_dsch_vn_g2p_tbl {
	struct dsch_vn_g2p_tbl {
		u32 sport : 1;
#define NBL_DSCH_N2G_SPORT_HOST		(0)
#define NBL_DSCH_N2G_SPORT_ECPU		(1)
		u32 dport : 1;
		u32 rsv : 29;
		u32 vld : 1;
	} info;
#define NBL_DSCH_VN_G2P_TBL_SIZE	(sizeof(struct dsch_vn_g2p_tbl))
	u8 data[NBL_DSCH_VN_G2P_TBL_SIZE];
};

#define NBL_DSCH_VN_G2P_TBL(t)	(NBL_DSCH_BASE + 0x00064000 + NBL_DSCH_VN_G2P_TBL_SIZE * (t))

union nbl_dsch_vn_tc_wgt_tbl {
	struct dsch_vn_tc_wgt_tbl {
		u32 tc0_wgt:8;
		u32 tc1_wgt:8;
		u32 tc2_wgt:8;
		u32 tc3_wgt:8;
		u32 tc4_wgt:8;
		u32 tc5_wgt:8;
		u32 tc6_wgt:8;
		u32 tc7_wgt:8;
	} info;
#define NBL_DSCH_VN_TC_WGT_TBL_SIZE	(sizeof(struct dsch_vn_tc_wgt_tbl))
	u8 data[NBL_DSCH_VN_TC_WGT_TBL_SIZE];
};

#define NBL_DSCH_VN_TC_WGT_TBL(t)	\
	(NBL_DSCH_BASE + 0x00068000 + NBL_DSCH_VN_TC_WGT_TBL_SIZE * (t))

union nbl_dsch_vn_tc_spwrr_tbl {
	struct dsch_vn_tc_spwrr_tbl {
		u32 tc_sprr : 8;
		u32 rsv : 24;
	} info;
#define NBL_DSCH_VN_TC_SPWRR_TBL_SIZE	(sizeof(struct dsch_vn_tc_spwrr_tbl))
	u8 data[NBL_DSCH_VN_TC_SPWRR_TBL_SIZE];
};

#define NBL_DSCH_VN_TC_SPWRR_TBL(t)	\
	(NBL_DSCH_BASE + 0x0006C000 + NBL_DSCH_VN_TC_SPWRR_TBL_SIZE * (t))

#define NBL_DSCH_HOST_QID_MAX	(NBL_DSCH_BASE + 0x00000208)

union nbl_ecpu_msix_tbl {
	struct ecpu_msix_tbl {
		u32 lower_address;
		u32 upper_address;
		u32 message_data;
#define NBL_ECPU_MSIX_TBL_MASK_OFFSET	(12)
		u32 vector_control_mask:1;
		u32 rsv:31;
	} info;
#define NBL_ECPU_MSIX_TBL_SIZE		(sizeof(struct ecpu_msix_tbl))
	u8 data[NBL_ECPU_MSIX_TBL_SIZE];
};

#define NBL_ECPU_MSIX_TBL(t)	\
	(PADAPT_ECPU_BASE + 0x00010000 + NBL_ECPU_MSIX_TBL_SIZE * (t))

union nbl_ecpu_msix_info_tbl {
	struct ecpu_msix_info_tbl {
		u32 intrl_pnum:16;
		u32 intrl_rate:16;
		/* the hw rate unit is 2.5us */
#define NBL_ECPU_MSIX_HW_RATE(rate)	(((rate) * 2 + 4) / 5)
#define NBL_ECPU_MSIX_SOFT_RATE(rate)	(((rate) * 5 + 1) / 2)
		u32 function:3;
		u32 devid:5;
		u32 bus:8;
		u32 mask_en:1;
		u32 rsv:14;
		u32 valid:1;
	} info;
#define NBL_ECPU_MSIX_INFO_TBL_SIZE		(sizeof(struct ecpu_msix_info_tbl))
	u8 data[NBL_ECPU_MSIX_INFO_TBL_SIZE];
};

#define NBL_ECPU_MSIX_INFO_TBL(t)	\
	(PADAPT_ECPU_BASE + 0x00020000 + NBL_ECPU_MSIX_INFO_TBL_SIZE * (t))

union nbl_ecpu_vnet_map_tbl {
	struct ecpu_vnet_map_tbl {
		u32 function:3;
		u32 devid:5;
		u32 bus:8;
		u32 msix_idx:12;
		u32 rsv0:1;
		u32 msix_idx_valid:1;
		u32 log_en:1;
		u32 valid:1;
		u32 tph_en:1;
		u32 ido_en:1;
		u32 rlo_en:1;
		u32 rsv1:29;
	} info;
#define NBL_ECPU_VNET_MAP_TBL_SIZE		(sizeof(struct ecpu_vnet_map_tbl))
	u8 data[NBL_ECPU_VNET_MAP_TBL_SIZE];
};

#define NBL_ECPU_VNET_MAP_TBL(t)	\
	(PADAPT_ECPU_BASE + 0x00030000 + NBL_ECPU_VNET_MAP_TBL_SIZE * (t))

enum nbl_lag_hash_type {
	NBL_LAG_HASH_TYPE_L2		= 0,
	NBL_LAG_HASH_TYPE_L23		= 1,
	NBL_LAG_HASH_TYPE_L34_LINUX	= 2,
	NBL_LAG_HASH_TYPE_L34_DPDK	= 3,
	NBL_LAG_HASH_TYPE_L2_KERNEL	= 4,
	NBL_LAG_HASH_TYPE_L23_KERNEL	= 5,
	NBL_LAG_HASH_TYPE_L34_KERNEL	= 6,
};

union nbl_dflow_lag_hash_reg {
	struct dflow_lag_hash_reg {
		/* DW0 */
		u32 lag0_hash_type:3;	//defined as enum nbl_lag_hash_type
		u32 l23_hash_shift_en:1;
		u32 rsv:28;
	} __packed info;
#define NBL_DFLOW_LAG_HASH_REG_WIDTH (sizeof(struct dflow_lag_hash_reg))
	u8 data[NBL_DFLOW_LAG_HASH_REG_WIDTH];
};

union nbl_uipro_eth_port_reg {
	struct uipro_eth_port_reg {
		/* DW0 */
		u32 default_vlanid:12;
		u32 vlan_type:2;
		u32 vlan_check_en:1;
		u32 lag_en:1;
		u32 lag_id:2;
		u32 cos_map_mode:3;
		u32 default_pri:3;
		u32 rsv0:5;
		u32 default_vlan_en:1;
		u32 rsv1:2;
	} __packed info;
#define NBL_UIPRO_ETH_PORT_REG_WIDTH (sizeof(struct uipro_eth_port_reg))
	u8 data[NBL_UIPRO_ETH_PORT_REG_WIDTH];
};

union nbl_uiflt_mkey_mask {
	struct uiflt_mkey_mask_table {
		/* DW0 */
		u32 valid:1;
		u32 rsv:7;
		u32 dport:16;
		u32 sport:16;
		u32 ip_protocol:8;
		u32 dip_low_16:16;
		/* DW2 */
		u32 dip_mid[3];
		/* DW5 */
		u32 dip_high_16:16;
		u32 sip_low_16:16;
		/* DW6 */
		u32 sip_mid[3];
		/* DW9 */
		u32 sip_high_16:16;
		u32 etype:16;
		/* DW10 */
		u32 vlan1;
		/* DW11 */
		u32 dmac_low_32;
		/* DW12 */
		u32 dmac_high_16:16;
		u32 smac_low_16:16;
		/* DW13 */
		u32 smac_high_32;
		/* DW14 */
		u32 outer_vxlan_nvi:24;
		u32 vxlan_rsv:8;
		/* DW15 */
		u32 outer_dip[4];
		/* DW19 */
		u32 outer_sip[4];
		/* DW23 */
		u32 outer_dscp:6;
		u32 rsv1:2;
		u32 inner_dscp:6;
		u32 rsv2:2;
		u32 eth_lag_id:2;
		u32 lag_valid:1;
		u32 rsv3:13;
		/* DW24 */
		u32 rsv4[8];
	} __packed info;
#define NBL_UIFLT_MKEY_MASK_TABLE_WIDTH (sizeof(struct uiflt_mkey_mask_table))
	u8 data[NBL_UIFLT_MKEY_MASK_TABLE_WIDTH];
};

union nbl_uiflt_action {
	struct uiflt_action_table {
		/* DW0 */
		u32 valid:1;
		u32 upcall_en:1;
		u32 upcall_dport:3;
		u32 upcall_dport_id:11;
		u32 upcall_dport_info:11;
		u32 upcall_rss_en:1;
		u32 upcall_fwd:2;
		u32 upcall_straight:1;
		u32 rsv:1;
		/* DW1 */
		u32 rsv1:3;
		u32 inner_dscp_nat_en:1;
		u32 inner_dscp_value:6;
		u32 rsv2:7;
		u32 tag_en:1;
		u32 tag_id:5;
		u32 count_en:1;
		u32 count_id:5;
		u32 rsv3:3;
	} __packed info;
#define NBL_UIFLT_ACTION_TABLE_WIDTH (sizeof(struct uiflt_action_table))
	u8 data[NBL_UIFLT_ACTION_TABLE_WIDTH];
};

union nbl_depro_eth_port_reg {
	struct depro_eth_port_reg {
		/* DW0 */
		u32 eth0_fwd:1;
		u32 eth1_fwd:1;
#define	NBL_DEPRO_ETH_FWD_DROP		(0)
#define	NBL_DEPRO_ETH_FWD_NORMAL	(1)
		u32 rsv:30;
	} __packed info;
#define NBL_DEPRO_ETH_PORT_REG_WIDTH (sizeof(struct depro_eth_port_reg))
	u8 data[NBL_DEPRO_ETH_PORT_REG_WIDTH];
};

union nbl_depro_lag_reg {
	struct depro_lag_reg {
		/* DW0 */
		u32 lag0_eth0:1;
		u32 lag0_eth1:1;
		u32 rsv:30;
	} __packed info;
#define NBL_DEPRO_LAG_REG_WIDTH (sizeof(struct depro_lag_reg))
	u8 data[NBL_DEPRO_LAG_REG_WIDTH];
};

enum nbl_memt_dport {
	NBL_DPORT_TO_ETH,
	NBL_DPORT_TO_HOST,
	NBL_DPORT_TO_ECPU,
	NBL_DPORT_TO_ICPU
};

#endif
