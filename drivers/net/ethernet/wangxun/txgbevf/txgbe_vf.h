/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd. */

#ifndef __TXGBE_VF_H__
#define __TXGBE_VF_H__

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>

#ifndef PCI_VENDOR_ID_WANGXUN
#define PCI_VENDOR_ID_WANGXUN                   0x8088
#endif

#define TXGBE_DEV_ID_SP1000_VF                  0x1000
#define TXGBE_DEV_ID_WX1820_VF                  0x2000

#define TXGBE_VF_MAX_TX_QUEUES  4
#define TXGBE_VF_MAX_RX_QUEUES  4
#define TXGBE_MAX_RSS_QUEUES	4
#define TXGBE_RX_BUFFER_WRITE   16

#define MAX_RX_QUEUES (TXGBE_VF_MAX_RX_QUEUES)
#define MAX_TX_QUEUES (TXGBE_VF_MAX_TX_QUEUES)

#define TXGBE_VFRSSRK_REGS		10	/* 10 registers for RSS key */

#define TXGBE_DEFAULT_TXD   128
#define TXGBE_DEFAULT_RXD   128
#define TXGBE_MAX_TXD       4096
#define TXGBE_MIN_TXD       64
#define TXGBE_MAX_RXD       4096
#define TXGBE_MIN_RXD       64

#define TXGBE_MAX_TXD_PWR       14
#define TXGBE_MAX_DATA_PER_TXD  BIT(TXGBE_MAX_TXD_PWR)

/* Number of Transmit and Receive Descriptors(*1024) */
#define TXGBE_REQ_TX_DESCRIPTOR_MULTIPLE        8
#define TXGBE_REQ_RX_DESCRIPTOR_MULTIPLE        8

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) DIV_ROUND_UP((S), TXGBE_MAX_DATA_PER_TXD)
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)

#define TXGBE_MAX_JUMBO_FRAME_SIZE        9432

#define TXGBE_VF_INIT_TIMEOUT           200
#define TXGBE_VF_PERMADDR_MSG_LEN       4
#define TXGBE_VF_IRQ_CLEAR_MASK         7

#define TXGBE_FW_VER_SIZE       32

/**
 * VF Registers
 * r=ring index [0,7], i=local index,
 * g=value for register, f=value for field
 **/
#define TXGBE_VXRXMEMWRAP           0x00000 /* i=[0,7] */
#define TXGBE_VXRXMEMWRAP_WRAP(g, i)   ((0x7 << 4 * (i) & (g)) >> 4 * (i))
#define TXGBE_VXRXMEMWRAP_EMPTY(g, i)  ((0x8 << 4 * (i) & (g)) >> 4 * (i))
#define TXGBE_VXSTATUS              0x00004
#define TXGBE_VXSTATUS_UP            BIT(0)
#define TXGBE_VXSTATUS_SPEED(g)      ((0x7 & (g)) >> 1)
#define TXGBE_VXSTATUS_SPEED_10G   (0x1)
#define TXGBE_VXSTATUS_SPEED_1G    (0x2)
#define TXGBE_VXSTATUS_SPEED_100M  (0x4)
#define TXGBE_VXSTATUS_BUSY          BIT(4)
#define TXGBE_VXSTATUS_LANID         BIT(8)
#define TXGBE_VXCTRL                0x00008
#define TXGBE_VXCTRL_RST      BIT(0)
#define TXGBE_VXMRQC                  0x00078
#define TXGBE_VXMRQC_RSV         BIT(0)
#define TXGBE_VXMRQC_PSR(f)      ((0x1F & (f)) << 1)
#define TXGBE_VXMRQC_PSR_L4HDR     BIT(0)
#define TXGBE_VXMRQC_PSR_L3HDR     BIT(1)
#define TXGBE_VXMRQC_PSR_L2HDR     BIT(2)
#define TXGBE_VXMRQC_PSR_TUNHDR    BIT(3)
#define TXGBE_VXMRQC_PSR_TUNMAC    BIT(4)
#define TXGBE_VXMRQC_RSS(f)      ((0xFFFF & (f)) << 16)
#define TXGBE_VXMRQC_RSS_ALG(f)     ((0xFF) & (f))
#define TXGBE_VXMRQC_RSS_ALG_IPV4_TCP   BIT(0)
#define TXGBE_VXMRQC_RSS_ALG_IPV4       BIT(1)
#define TXGBE_VXMRQC_RSS_ALG_IPV6       BIT(4)
#define TXGBE_VXMRQC_RSS_ALG_IPV6_TCP   BIT(5)
#define TXGBE_VXMRQC_RSS_ALG_IPV4_UDP   BIT(6)
#define TXGBE_VXMRQC_RSS_ALG_IPV6_UDP   BIT(7)
#define TXGBE_VXMRQC_RSS_EN         ((0x1) << 8)
#define TXGBE_VXMRQC_RSS_HASH(f)    ((0x7 & (f)) << 13)
#define TXGBE_VXRSSRK(i)        (0x00080 + ((i) * 4)) /* i=[0,9] */
#define TXGBE_VXRETA(i)         (0x000C0 + ((i) * 4)) /* i=[0,15] */
#define TXGBE_VXICR                 0x00100
#define TXGBE_VXIC_MBOX         ((0x1) << 0)
#define TXGBE_VXIC_DONE1        ((0x1) << 1)
#define TXGBE_VXIC_DONE2        ((0x1) << 2)
#define TXGBE_VXICS                 0x00104
#define TXGBE_VXIMS                 0x00108
#define TXGBE_VXIMC                 0x0010C
#define TXGBE_VXLLI                 0x00118
#define TXGBE_VXITR(i)               (0x00200 + (4 * (i))) /* i=[0,1] */
#define TXGBE_VXITR_INTERVAL(f)    ((0x1FF & (f)) << 3)
#define TXGBE_VXITR_LLI            ((0x1) << 15)
#define TXGBE_VXITR_LLI_CREDIT(f)  ((0x1F & (f)) << 16)
#define TXGBE_VXITR_CNT(f)         ((0x7F & (f)) << 21)
#define TXGBE_VXITR_CNT_WDIS       ((0x1) << 31)
#define TXGBE_VXIVAR(i)            (0x00240 + (4 * (i))) /* i=[0,3] */
#define TXGBE_VXIVAR_ALLOC(i, f)   ((0x1 & (f)) << 8 * (i))
#define TXGBE_VXIVAR_VALID(i, f)   ((0x80 & (f)) << 8 * (i))
#define TXGBE_VXIVAR_MISC            0x00260
#define TXGBE_VXIVAR_MISC_ALLOC(f) ((0x3 & (f)))
#define TXGBE_VXIVAR_MISC_VALID    ((0x80))

#define TXGBE_VXITR(i)             (0x00200 + (4 * (i))) /* i=[0,1] */
#define TXGBE_VXITR_INTERVAL(f)    ((0x1FF & (f)) << 3)
#define TXGBE_VXITR_LLI            ((0x1) << 15)
#define TXGBE_VXITR_LLI_CREDIT(f)  ((0x1F & (f)) << 16)
#define TXGBE_VXITR_CNT(f)         ((0x7F & (f)) << 21)
#define TXGBE_VXITR_CNT_WDIS       ((0x1) << 31)
#define TXGBE_VXIVAR(i)            (0x00240 + (4 * (i))) /* i=[0,3] */
#define TXGBE_VXIVAR_ALLOC(i, f)   ((0x1 & (f)) << 8 * (i))
#define TXGBE_VXIVAR_VALID(i, f)   ((0x80 & (f)) << 8 * (i))
#define TXGBE_VXIVAR_MISC          0x00260
#define TXGBE_VXIVAR_MISC_ALLOC(f) ((0x3 & (f)))
#define TXGBE_VXIVAR_MISC_VALID    ((0x80))
#define NON_Q_VECTORS (1)
#define MAX_Q_VECTORS (5)
#define MIN_MSIX_COUNT (1 + NON_Q_VECTORS)

/*** @txgbe_rx_desc.rd.lower.pkt_addr ***/
#define TXGBE_RXD_PKTADDR(v)       cpu_to_le64((v))

/*** @txgbe_rx_desc.rd.lower.hdr_addr ***/
#define TXGBE_RXD_HDRADDR(v)       cpu_to_le64((v))

#define TXGBE_RX_DESC(R, i)       \
	(&(((union txgbe_rx_desc *)((R)->desc))[i]))
#define TXGBE_TX_DESC(R, i)       \
	(&(((struct txgbe_tx_desc *)((R)->desc))[i]))
#define TXGBE_TX_CTXTDESC(R, i)           \
	(&(((struct txgbe_adv_tx_context_desc *)((R)->desc))[i]))

/*** @txgbe_rx_desc.wb.lower.lo_dword ***/
/* RSS Hash results */
#define TXGBE_RXD_RSSTYPE(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data)) & 0xF)
#define   TXGBE_RSSTYPE_NONE        (0)
#define   TXGBE_RSSTYPE_IPV4_TCP    (1)
#define   TXGBE_RSSTYPE_IPV4        (2)
#define   TXGBE_RSSTYPE_IPV6_TCP    (3)
#define   TXGBE_RSSTYPE_IPV4_SCTP   (4)
#define   TXGBE_RSSTYPE_IPV6        (5)
#define   TXGBE_RSSTYPE_IPV6_SCTP   (6)
#define   TXGBE_RSSTYPE_IPV4_UDP    (7)
#define   TXGBE_RSSTYPE_IPV6_UDP    (8)
#define   TXGBE_RSSTYPE_FDIR        (15)
#define TXGBE_RXD_SECTYPE(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data) >> 4) & 0x3)
#define   TXGBE_SECTYPE_NONE          (0)
#define   TXGBE_SECTYPE_LINKSEC       (1)
#define   TXGBE_SECTYPE_IPSECESP      (2)
#define   TXGBE_SECTYPE_IPSECAH       (3)
#define TXGBE_RXD_TPID_SEL(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data) >> 6) & 0x7)
#define TXGBE_RXD_PKTTYPE(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data) >> 9) & 0xFF)
#define TXGBE_RXD_RSCCNT(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data) >> 17) & 0xF)
#define TXGBE_RXD_HDRLEN(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.lo_dword.data) >> 21) & 0x3FF)
#define TXGBE_RXD_SPH              ((0x1) << 31)

/*** @txgbe_rx_desc.wb.lower.hi_dword ***/
/** bit 0-31, as rss hash when  **/
#define TXGBE_RXD_RSS_HASH(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.hi_dword.data)))

/** bit 0-31, as ip csum when  **/
#define TXGBE_RXD_IPCSUM(rxd) \
	    ((le16_to_cpu((rxd)->wb.lower.hi_dword.ip_csum.ipid)))
#define TXGBE_RXD_IPCSUM_CSUM(rxd) \
	    ((le16_to_cpu((rxd)->wb.lower.hi_dword.ip_csum.csum)))

/** bit 0-31, as fdir id when  **/
#define TXGBE_RXD_FDIR_ID(rxd) \
	    ((le32_to_cpu((rxd)->wb.lower.hi_dword.data)))

/*** @txgbe_rx_desc.wb.upper.status ***/
#define TXGBE_RXD_STATUS(rxd) \
	    (le32_to_cpu((rxd)->wb.upper.status)) /* All Status */
/** bit 0-1 **/
#define TXGBE_RXD_STAT_DD       ((0x1) << 0) /* Descriptor Done */
#define TXGBE_RXD_STAT_EOP      ((0x1) << 1) /* End of Packet */
/** bit 2-31, when EOP=1 **/
#define TXGBE_RXD_NEXTP_RESV(v) ((0x3 & (v)) << 2)
#define TXGBE_RXD_NEXTP(v)      ((0xFFFF & (v)) << 4) /* Next Descriptor Index */
/** bit 2-31, when EOP=0 **/
#define TXGBE_RXD_STAT_CLASS(v)       ((0x7 & (v)) << 2) /* Packet Class */
#define   TXGBE_PKT_CLASS(r)      (((r) >> 2) & 0x7)
#define   TXGBE_PKT_CLASS_TC_RSS  (0) /* RSS Hash */
#define   TXGBE_PKT_CLASS_FLM     (1) /* FDir Match */
#define   TXGBE_PKT_CLASS_SYN     (2) /* TCP Sync */
#define   TXGBE_PKT_CLASS_5TUPLE  (3) /* 5 Tuple */
#define   TXGBE_PKT_CLASS_L2ETYPE (4) /* L2 Ethertype */
#define TXGBE_RXD_STAT_VP       ((0x1) << 5) /* IEEE VLAN Packet */
#define TXGBE_RXD_STAT_UDPCS    ((0x1) << 6) /* UDP xsum calculated */
#define TXGBE_RXD_STAT_TPCS     ((0x1) << 7) /* L4 xsum calculated */
#define TXGBE_RXD_STAT_IPCS     ((0x1) << 8) /* IP xsum calculated */
#define TXGBE_RXD_STAT_PIF      ((0x1) << 9) /* Non-unicast address */
#define TXGBE_RXD_STAT_EIPCS    ((0x1) << 10) /* Encap IP xsum calculated */
#define TXGBE_RXD_STAT_VEXT     ((0x1) << 11) /* Multi-VLAN */
#define TXGBE_RXD_STAT_IPV6EX   ((0x1) << 12) /* IPv6 with option header */
#define TXGBE_RXD_STAT_LLINT    ((0x1) << 13) /* Pkt caused Low Latency Interrupt */
#define TXGBE_RXD_STAT_TS       ((0x1) << 14) /* IEEE1588 Time Stamp */
#define TXGBE_RXD_STAT_SECP     ((0x1) << 15) /* Security Processing */
#define TXGBE_RXD_STAT_LB       ((0x1) << 16) /* Loopback Status */
/* bit 17-30, when PKTTYPE=IP */
#define TXGBE_RXD_STAT_BMC      ((0x1) << 17) /* PKTTYPE=IP, BMC status */
#define TXGBE_RXD_ERR_FDIRERR(v) ((0x7 & (v)) << 20) /* FDIRERR */
#define   TXGBE_RXD_ERR_FDIR_LEN   ((0x1) << 20) /* FDIR Length error */
#define   TXGBE_RXD_ERR_FDIR_DROP  ((0x1) << 21) /* FDIR Drop error */
#define   TXGBE_RXD_ERR_FDIR_COLL  ((0x1) << 22) /* FDIR Collision error */
#define TXGBE_RXD_ERR_HBO      ((0x1) << 23) /*Header Buffer Overflow */
#define TXGBE_RXD_ERR_EIPERR   ((0x1) << 26) /* Encap IP header error */
#define TXGBE_RXD_ERR_SECERR(v)   ((0x3 & (v)) << 27)
#define   TXGBE_IP_SECERR_0    (0)
#define   TXGBE_IP_SECERR_1    (1)
#define   TXGBE_IP_SECERR_2    (2)
#define   TXGBE_IP_SECERR_3    (3)
#define TXGBE_RXD_ERR_RXE      ((0x1) << 29) /* Any MAC Error */
#define TXGBE_RXD_ERR_TPE      ((0x1) << 30) /* TCP/UDP Checksum Error */
#define TXGBE_RXD_ERR_IPE      ((0x1) << 31) /* IP Checksum Error */
/* bit 17-30, when PKTTYPE=FCOE */
#define TXGBE_RXD_STAT_FCOEFS   ((0x1) << 17) /* PKTTYPE=FCOE, FCoE EOF/SOF Stat */
#define TXGBE_RXD_STAT_FCSTAT(v)     ((0x3 & (v)) << 18) /* FCoE Pkt Stat */
#define   TXGBE_FCOE_FCSTAT(r)      (((r) >> 18) & 0x7)
#define   TXGBE_FCOE_FCSTAT_NOMTCH  (0) /* No Ctxt Match */
#define   TXGBE_FCOE_FCSTAT_NODDP   (1) /* Ctxt w/o DDP */
#define   TXGBE_FCOE_FCSTAT_FCPRSP  (2) /* Recv. FCP_RSP */
#define   TXGBE_FCOE_FCSTAT_DDP     (3) /* Ctxt w/ DDP */
#define TXGBE_RXD_ERR_FCERR(v)  ((0x7 & (v)) << 20) /* FCERR */
#define   TXGBE_FCOE_FCERR_0  (0)
#define   TXGBE_FCOE_FCERR_1  (1)
#define   TXGBE_FCOE_FCERR_2  (2)
#define   TXGBE_FCOE_FCERR_3  (3)
#define   TXGBE_FCOE_FCERR_4  (4)
#define   TXGBE_FCOE_FCERR_5  (5)
#define   TXGBE_FCOE_FCERR_6  (6)
#define   TXGBE_FCOE_FCERR_7  (7)

#define TXGBE_TXD_DTYP_DATA             0x00000000U /* Adv Data Descriptor */
//#define TXGBE_TXD_EOP                   0x01000000U  /* End of Packet */
#define TXGBE_TXD_IFCS                  0x02000000U /* Insert FCS */

#define TXGBE_TXD_IDX_SHIFT             4 /* Adv desc Index shift */
//#define TXGBE_TXD_CC                    0x00000080U /* Check Context */
//#define TXGBE_TXD_IPSEC                 0x00000100U /* enable ipsec esp */
#define TXGBE_TXD_IIPCS                 0x00000400U
//#define TXGBE_TXD_EIPCS                 0x00000800U
#define TXGBE_TXD_L4CS                  0x00000200U
#define TXGBE_TXD_PAYLEN_SHIFT          13 /* Adv desc PAYLEN shift */
#define TXGBE_TXD_MACLEN_SHIFT          9  /* Adv ctxt desc mac len shift */
#define TXGBE_TXD_VLAN_SHIFT            16  /* Adv ctxt vlan tag shift */
#define TXGBE_TXD_TAG_TPID_SEL_SHIFT    11
#define TXGBE_TXD_IPSEC_TYPE_SHIFT      14
#define TXGBE_TXD_ENC_SHIFT             15

#define TXGBE_TXD_TUCMD_IPSEC_TYPE_ESP  0x00004000U /* IPSec Type ESP */
#define TXGBE_TXD_TUCMD_IPSEC_ENCRYPT_EN 0x00008000/* ESP Encrypt Enable */
#define TXGBE_TXD_TUCMD_FCOE            0x00010000U /* FCoE Frame Type */
#define TXGBE_TXD_FCOEF_EOF_MASK        (0x3 << 10) /* FC EOF index */
#define TXGBE_TXD_FCOEF_EOF_N           (0x0 << 10) /* 00: EOFn */
#define TXGBE_TXD_FCOEF_EOF_T           (0x1 << 10) /* 01: EOFt */
#define TXGBE_TXD_FCOEF_EOF_NI          (0x2 << 10) /* 10: EOFni */
#define TXGBE_TXD_FCOEF_EOF_A           (0x3 << 10) /* 11: EOFa */
#define TXGBE_TXD_L4LEN_SHIFT           8  /* Adv ctxt L4LEN shift */
#define TXGBE_TXD_MSS_SHIFT             16  /* Adv ctxt MSS shift */

#define TXGBE_TXD_OUTER_IPLEN_SHIFT     12 /* Adv ctxt OUTERIPLEN shift */
#define TXGBE_TXD_TUNNEL_LEN_SHIFT      21 /* Adv ctxt TUNNELLEN shift */

#define TXGBE_TXD_TUNNEL_TYPE_SHIFT     11 /* Adv Tx Desc Tunnel Type shift */
#define TXGBE_TXD_TUNNEL_DECTTL_SHIFT   27 /* Adv ctxt DECTTL shift */
#define TXGBE_TXD_TUNNEL_UDP            (0x0ULL << TXGBE_TXD_TUNNEL_TYPE_SHIFT)
#define TXGBE_TXD_TUNNEL_GRE            (0x1ULL << TXGBE_TXD_TUNNEL_TYPE_SHIFT)

/*** @txgbe_tx_ctxt_desc.rd.type_tucmd_mlhl ***/
#define TXGBE_TXD_IPSEC_ESPLEN(v)     (((v) & 0x1FF)) /* IPSec ESP length */
#define TXGBE_TXD_SNAP                ((0x1) << 10) /* SNAP indication */
#define TXGBE_TXD_TPID_SEL(v)         (((v) & 0x7) << 10) /* VLAN TPID index */
#define TXGBE_TXD_IPSEC_TYPE(v)       (((v) & 0x1) << 14) /* IPSec Type */
#define TXGBE_IPSEC_TYPE_AH        (0)
#define TXGBE_IPSEC_TYPE_ESP       (1)
#define TXGBE_TXD_IPSEC_ESPENC(v)     (((v) & 0x1) << 15) /* ESP encrypt */
#define TXGBE_TXD_DTYP_CTXT           ((0x1) << 20) /* CTXT/DATA descriptor */
#define TXGBE_TXD_PKTTYPE(v)          (((v) & 0xFF) << 24) /* packet type */
/*** @txgbe_tx_ctxt_desc.rd.mss_l4len_idx ***/
#define TXGBE_TXD_CTX_DD              ((0x1)) /* Descriptor Done */
#define TXGBE_TXD_TPLEN(v)            (((v) & 0xFF) << 8) /* transport header length */
#define TXGBE_TXD_MSS(v)              (((v) & 0xFFFF) << 16) /* transport maximum segment size */
/*** @txgbe_rx_desc.wb.upper.length ***/
#define TXGBE_RXD_LENGTH(rxd) \
	    ((le16_to_cpu((rxd)->wb.upper.length)))

/*** @txgbe_rx_desc.wb.upper.vlan ***/
#define TXGBE_RXD_VLAN(rxd) \
	    ((le16_to_cpu((rxd)->wb.upper.vlan)))

/* Receive Path */
#define TXGBE_VXRDBAL(r)          (0x01000 + (0x40 * (r)))
#define TXGBE_VXRDBAH(r)          (0x01004 + (0x40 * (r)))
#define TXGBE_VXRDT(r)            (0x01008 + (0x40 * (r)))
#define TXGBE_VXRDH(r)            (0x0100C + (0x40 * (r)))
#define TXGBE_VXRXDCTL(r)         (0x01010 + (0x40 * (r)))
#define TXGBE_VXRXDCTL_ENABLE     ((0x1) << 0)
#define TXGBE_VXRXDCTL_BUFSZ(f)   ((0xF & (f)) << 8)
#define TXGBE_VXRXDCTL_BUFLEN(f)  ((0x3F & (f)) << 1)
#define TXGBE_VXRXDCTL_HDRSZ(f)   ((0xF & (f)) << 12)
#define TXGBE_VXRXDCTL_WTHRESH(f) ((0x7 & (f)) << 16)
#define TXGBE_VXRXDCTL_ETAG       ((0x1) << 22)
#define TXGBE_VXRXDCTL_RSCMAX(f)  ((0x3 & (f)) << 23)
#define TXGBE_RSCMAX_1        (0)
#define TXGBE_RSCMAX_4        (1)
#define TXGBE_RSCMAX_8        (2)
#define TXGBE_RSCMAX_16       (3)
#define TXGBE_VXRXDCTL_STALL      ((0x1) << 25)
#define TXGBE_VXRXDCTL_SPLIT      ((0x1) << 26)
#define TXGBE_VXRXDCTL_RSCMODE    ((0x1) << 27)
#define TXGBE_VXRXDCTL_CNTAG      ((0x1) << 28)
#define TXGBE_VXRXDCTL_RSCEN      ((0x1) << 29)
#define TXGBE_VXRXDCTL_DROP       ((0x1) << 30)
#define TXGBE_VXRXDCTL_VLAN       ((0x1) << 31)

/* Transmit Path */
#define TXGBE_VXTDBAL(r)          (0x03000 + (0x40 * (r)))
#define TXGBE_VXTDBAH(r)          (0x03004 + (0x40 * (r)))
#define TXGBE_VXTDT(r)            (0x03008 + (0x40 * (r)))
#define TXGBE_VXTDH(r)            (0x0300C + (0x40 * (r)))
#define TXGBE_VXTXDCTL(r)         (0x03010 + (0x40 * (r)))
#define TXGBE_VXTXDCTL_ENABLE     ((0x1) << 0)
#define TXGBE_VXTXDCTL_BUFLEN(f)  ((0x3F & (f)) << 1)
#define TXGBE_VXTXDCTL_PTHRESH(f) ((0xF & (f)) << 8)
#define TXGBE_VXTXDCTL_WTHRESH(f) ((0x7F & (f)) << 16)
#define TXGBE_VXTXDCTL_FLUSH      ((0x1) << 26)

/* board specific private data structure */
#define TXGBE_F_CAP_RX_CSUM             BIT(0)
#define TXGBE_F_CAP_LRO	                BIT(1)
#define TXGBE_F_REQ_RESET               BIT(2)
#define TXGBE_F_REQ_QUEUE_RESET         BIT(3)
#define TXGBE_F_ENA_RSS_IPV4UDP         BIT(4)
#define TXGBE_F_ENA_RSS_IPV6UDP         BIT(5)

#define TXGBE_VXGPRC(r)            (0x01014 + (0x40 * (r)))
#define TXGBE_VXGORC_LSB(r)        (0x01018 + (0x40 * (r)))
#define TXGBE_VXGORC_MSB(r)        (0x0101C + (0x40 * (r)))
#define TXGBE_VXMPRC(r)            (0x01020 + (0x40 * (r)))
#define TXGBE_VXGPTC(r)            (0x03014 + (0x40 * (r)))
#define TXGBE_VXGOTC_LSB(r)        (0x03018 + (0x40 * (r)))
#define TXGBE_VXGOTC_MSB(r)        (0x0301C + (0x40 * (r)))

#define TXGBE_VXSTATUS              0x00004
#define TXGBE_VXSTATUS_UP           BIT(0)
#define TXGBE_VXSTATUS_SPEED(g)     ((0x7 & (g)) >> 1)
#define TXGBE_VXSTATUS_SPEED_10G   (0x1)
#define TXGBE_VXSTATUS_SPEED_1G    (0x2)
#define TXGBE_VXSTATUS_SPEED_100M  (0x4)

#define TXGBE_VXMAILBOX         0x00600
#define TXGBE_VXMAILBOX_REQ     ((0x1) << 0) /* Request for PF Ready bit */
#define TXGBE_VXMAILBOX_ACK     ((0x1) << 1) /* Ack PF message received */
#define TXGBE_VXMAILBOX_VFU     ((0x1) << 2) /* VF owns the mailbox buffer */
#define TXGBE_VXMAILBOX_PFU     ((0x1) << 3) /* PF owns the mailbox buffer */
#define TXGBE_VXMAILBOX_PFSTS   ((0x1) << 4) /* PF wrote a message in the MB */
#define TXGBE_VXMAILBOX_PFACK   ((0x1) << 5) /* PF ack the previous VF msg */
#define TXGBE_VXMAILBOX_RSTI    ((0x1) << 6) /* PF has reset indication */
#define TXGBE_VXMAILBOX_RSTD    ((0x1) << 7) /* PF has indicated reset done */
#define TXGBE_VXMAILBOX_R2C_BITS (TXGBE_VXMAILBOX_RSTD |\
						TXGBE_VXMAILBOX_PFSTS | TXGBE_VXMAILBOX_PFACK)
#define TXGBE_VXMAILBOX_SIZE    (16 - 1)

#define TXGBE_VXMBMEM           0x00C00 /* 16*4B */

#define TXGBE_LINK_SPEED_100_FULL       0x0008
#define TXGBE_LINK_SPEED_1GB_FULL       0x0020
#define TXGBE_LINK_SPEED_10GB_FULL      0x0080

__maybe_unused static int txgbe_conf_size(int v, int mwidth, int uwidth)
{
	int _v = v;

	return (_v) == 2 << (mwidth) ? 0 : (_v) >> (uwidth);
}

#define txgbe_buf_len(v)    txgbe_conf_size(v, 13, 7)
#define txgbe_hdr_sz(v)     txgbe_conf_size(v, 10, 6)
#define txgbe_buf_sz(v)     txgbe_conf_size(v, 14, 10)
#define txgbe_pkt_thresh(v) txgbe_conf_size(v, 4, 0)

/* Supported Rx Buffer Sizes */
#define TXGBE_RXBUFFER_256    (256)    /* Used for packet split */
#define TXGBE_RXBUFFER_2048   (2048)
#define TXGBE_RXBUFFER_3072   (3072)
#define TXGBE_RX_HDR_SIZE TXGBE_RXBUFFER_256
#define TXGBE_RX_BUF_SIZE TXGBE_RXBUFFER_2048

#define TXGBE_SKB_PAD		(NET_SKB_PAD + NET_IP_ALIGN)
#if (PAGE_SIZE < 8192)
#define TXGBE_MAX_FRAME_BUILD_SKB \
	(SKB_WITH_OVERHEAD(TXGBE_RXBUFFER_2048) - TXGBE_SKB_PAD)
#else
#define TXGBE_MAX_FRAME_BUILD_SKB	TXGBE_RXBUFFER_2048
#endif

#define TXGBE_100K_ITR          (0x005)
#define TXGBE_20K_ITR           (0x019)
#define TXGBE_12K_ITR           (0x02A)

/*#define TXGBE_VFRETA_SIZE	64	 64 entries */
#define TXGBE_VFRETA_SIZE	128	/* 128 entries */

#define TXGBE_RSS_HASH_KEY_SIZE	40
#define TXGBE_VFRSSRK_REGS		10	/* 10 registers for RSS key */

enum txgbe_xcast_modes {
	TXGBE_XCAST_MODE_NONE = 0,
	TXGBE_XCAST_MODE_MULTI,
	TXGBE_XCAST_MODE_ALLMULTI,
	TXGBE_XCAST_MODE_PROMISC,
};

/* Error Codes:
 * (-256, 256): reserved for non-txgbe defined error code
 */
#define TXGBE_ERR_BASE (0x100)
enum txgbe_error {
	TXGBE_ERR_NULL = TXGBE_ERR_BASE, /* errline=__LINE__+errno-256 */
	TXGBE_ERR_NOSUPP,
	TXGBE_ERR_EEPROM,
	TXGBE_ERR_EEPROM_CHECKSUM,
	TXGBE_ERR_PHY,
	TXGBE_ERR_CONFIG,
	TXGBE_ERR_PARAM,
	TXGBE_ERR_MAC_TYPE,
	TXGBE_ERR_UNKNOWN_PHY,
	TXGBE_ERR_LINK_SETUP,
	TXGBE_ERR_ADAPTER_STOPPED,
	TXGBE_ERR_INVALID_MAC_ADDR,
	TXGBE_ERR_DEVICE_NOT_SUPPORTED,
	TXGBE_ERR_MASTER_REQUESTS_PENDING,
	TXGBE_ERR_INVALID_LINK_SETTINGS,
	TXGBE_ERR_AUTONEG_NOT_COMPLETE,
	TXGBE_ERR_RESET_FAILED,
	TXGBE_ERR_SWFW_SYNC,
	TXGBE_ERR_PHY_ADDR_INVALID,
	TXGBE_ERR_I2C,
	TXGBE_ERR_SFP_NOT_SUPPORTED,
	TXGBE_ERR_SFP_NOT_PRESENT,
	TXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT,
	TXGBE_ERR_NO_SAN_ADDR_PTR,
	TXGBE_ERR_FDIR_REINIT_FAILED,
	TXGBE_ERR_EEPROM_VERSION,
	TXGBE_ERR_NO_SPACE,
	TXGBE_ERR_OVERTEMP,
	TXGBE_ERR_UNDERTEMP,
	TXGBE_ERR_FC_NOT_NEGOTIATED,
	TXGBE_ERR_FC_NOT_SUPPORTED,
	TXGBE_ERR_SFP_SETUP_NOT_COMPLETE,
	TXGBE_ERR_PBA_SECTION,
	TXGBE_ERR_INVALID_ARGUMENT,
	TXGBE_ERR_HOST_INTERFACE_COMMAND,
	TXGBE_ERR_OUT_OF_MEM,
	TXGBE_ERR_FEATURE_NOT_SUPPORTED,
	TXGBE_ERR_EEPROM_PROTECTED_REGION,
	TXGBE_ERR_FDIR_CMD_INCOMPLETE,
	TXGBE_ERR_FLASH_LOADING_FAILED,
	TXGBE_ERR_XPCS_POWER_UP_FAILED,
	TXGBE_ERR_FW_RESP_INVALID,
	TXGBE_ERR_PHY_INIT_NOT_DONE,
	TXGBE_ERR_TOKEN_RETRY,
	TXGBE_ERR_REG_TMOUT,
	TXGBE_ERR_REG_ACCESS,
	TXGBE_ERR_MBX,
};

#define TXGBE_ERR_NOSUPP                      (-TXGBE_ERR_NOSUPP)
#define TXGBE_ERR_EEPROM                      (-TXGBE_ERR_EEPROM)
#define TXGBE_ERR_EEPROM_CHECKSUM             (-TXGBE_ERR_EEPROM_CHECKSUM)
#define TXGBE_ERR_PHY                         (-TXGBE_ERR_PHY)
#define TXGBE_ERR_CONFIG                      (-TXGBE_ERR_CONFIG)
#define TXGBE_ERR_PARAM                       (-TXGBE_ERR_PARAM)
#define TXGBE_ERR_MAC_TYPE                    (-TXGBE_ERR_MAC_TYPE)
#define TXGBE_ERR_UNKNOWN_PHY                 (-TXGBE_ERR_UNKNOWN_PHY)
#define TXGBE_ERR_LINK_SETUP                  (-TXGBE_ERR_LINK_SETUP)
#define TXGBE_ERR_ADAPTER_STOPPED             (-TXGBE_ERR_ADAPTER_STOPPED)
#define TXGBE_ERR_INVALID_MAC_ADDR            (-TXGBE_ERR_INVALID_MAC_ADDR)
#define TXGBE_ERR_DEVICE_NOT_SUPPORTED        (-TXGBE_ERR_DEVICE_NOT_SUPPORTED)
#define TXGBE_ERR_MASTER_REQUESTS_PENDING     (-TXGBE_ERR_MASTER_REQUESTS_PENDING)
#define TXGBE_ERR_INVALID_LINK_SETTINGS       (-TXGBE_ERR_INVALID_LINK_SETTINGS)
#define TXGBE_ERR_AUTONEG_NOT_COMPLETE        (-TXGBE_ERR_AUTONEG_NOT_COMPLETE)
#define TXGBE_ERR_RESET_FAILED                (-TXGBE_ERR_RESET_FAILED)
#define TXGBE_ERR_SWFW_SYNC                   (-TXGBE_ERR_SWFW_SYNC)
#define TXGBE_ERR_PHY_ADDR_INVALID            (-TXGBE_ERR_PHY_ADDR_INVALID)
#define TXGBE_ERR_I2C                         (-TXGBE_ERR_I2C)
#define TXGBE_ERR_SFP_NOT_SUPPORTED           (-TXGBE_ERR_SFP_NOT_SUPPORTED)
#define TXGBE_ERR_SFP_NOT_PRESENT             (-TXGBE_ERR_SFP_NOT_PRESENT)
#define TXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT     (-TXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT)
#define TXGBE_ERR_NO_SAN_ADDR_PTR             (-TXGBE_ERR_NO_SAN_ADDR_PTR)
#define TXGBE_ERR_FDIR_REINIT_FAILED          (-TXGBE_ERR_FDIR_REINIT_FAILED)
#define TXGBE_ERR_EEPROM_VERSION              (-TXGBE_ERR_EEPROM_VERSION)
#define TXGBE_ERR_NO_SPACE                    (-TXGBE_ERR_NO_SPACE)
#define TXGBE_ERR_OVERTEMP                    (-TXGBE_ERR_OVERTEMP)
#define TXGBE_ERR_UNDERTEMP                   (-TXGBE_ERR_UNDERTEMP)
#define TXGBE_ERR_FC_NOT_NEGOTIATED           (-TXGBE_ERR_FC_NOT_NEGOTIATED)
#define TXGBE_ERR_FC_NOT_SUPPORTED            (-TXGBE_ERR_FC_NOT_SUPPORTED)
#define TXGBE_ERR_SFP_SETUP_NOT_COMPLETE      (-TXGBE_ERR_SFP_SETUP_NOT_COMPLETE)
#define TXGBE_ERR_PBA_SECTION                 (-TXGBE_ERR_PBA_SECTION)
#define TXGBE_ERR_INVALID_ARGUMENT            (-TXGBE_ERR_INVALID_ARGUMENT)
#define TXGBE_ERR_HOST_INTERFACE_COMMAND      (-TXGBE_ERR_HOST_INTERFACE_COMMAND)
#define TXGBE_ERR_OUT_OF_MEM                  (-TXGBE_ERR_OUT_OF_MEM)
#define TXGBE_ERR_FEATURE_NOT_SUPPORTED       (-TXGBE_ERR_FEATURE_NOT_SUPPORTED)
#define TXGBE_ERR_EEPROM_PROTECTED_REGION     (-TXGBE_ERR_EEPROM_PROTECTED_REGION)
#define TXGBE_ERR_FDIR_CMD_INCOMPLETE         (-TXGBE_ERR_FDIR_CMD_INCOMPLETE)
#define TXGBE_ERR_FLASH_LOADING_FAILED        (-TXGBE_ERR_FLASH_LOADING_FAILED)
#define TXGBE_ERR_XPCS_POWER_UP_FAILED        (-TXGBE_ERR_XPCS_POWER_UP_FAILED)
#define TXGBE_ERR_FW_RESP_INVALID             (-TXGBE_ERR_FW_RESP_INVALID)
#define TXGBE_ERR_PHY_INIT_NOT_DONE           (-TXGBE_ERR_PHY_INIT_NOT_DONE)
#define TXGBE_ERR_TOKEN_RETRY                 (-TXGBE_ERR_TOKEN_RETRY)
#define TXGBE_ERR_REG_TMOUT                   (-TXGBE_ERR_REG_TMOUT)
#define TXGBE_ERR_REG_ACCESS                  (-TXGBE_ERR_REG_ACCESS)
#define TXGBE_ERR_MBX                         (-TXGBE_ERR_MBX)

extern char txgbe_firmware_version[];
extern char txgbevf_driver_name[];
extern const char txgbevf_driver_version[];

typedef u32 txgbe_link_speed;

struct txgbe_hw;

struct txgbe_q_vector;

typedef u8* (*txgbe_mc_addr_itr) (struct txgbe_hw *hw, u8 **mc_addr_ptr, u32 *vmdq);

enum txbgevf_state_t {
	__TXGBE_TESTING,
	__TXGBE_RESETTING,
	__TXGBE_DOWN,
	__TXGBE_DISABLED,
	__TXGBE_REMOVING,
	__TXGBE_SERVICE_SCHED,
	__TXGBE_SERVICE_INITED,
	__TXGBE_RESET_REQUESTED,
	__TXGBE_QUEUE_RESET_REQUESTED,
};

enum txgbe_mac_type {
	txgbe_mac_unknown = 0,
	txgbe_mac_sp,
	txgbe_mac_sp_vf,
	txgbe_num_macs
};

struct txgbe_info {
	enum txgbe_mac_type     mac;
	unsigned int            flags;
};

enum txgbe_boards {
	board_sp_vf,
};

enum txgbe_ring_state_t {
	__TXGBE_RX_3K_BUFFER,
	__TXGBE_RX_BUILD_SKB_ENABLED,
	__TXGBE_TX_DETECT_HANG,
	__TXGBE_HANG_CHECK_ARMED,
	__TXGBE_RX_CSUM_UDP_ZERO_ERR,
	__TXGBE_TX_XDP_RING,
	__TXGBE_TX_XDP_RING_PRIMED,
};

enum txgbe_tx_flags {
	/* cmd_type flags */
	TXGBE_TX_FLAGS_VLAN  = 0x01,
	TXGBE_TX_FLAGS_TSO      = 0x02,
	TXGBE_TX_FLAGS_TSTAMP   = 0x04,

	/* olinfo flags */
	TXGBE_TX_FLAGS_CC       = 0x08,
	TXGBE_TX_FLAGS_IPV4     = 0x10,
	TXGBE_TX_FLAGS_CSUM     = 0x20,
	TXGBE_TX_FLAGS_OUTER_IPV4 = 0x100,
	TXGBE_TX_FLAGS_LINKSEC	= 0x200,
	TXGBE_TX_FLAGS_IPSEC    = 0x400,

	/* software defined flags */
	TXGBE_TX_FLAGS_FCOE     = 0x80,
};

#define TXGBE_TX_FLAGS_VLAN_MASK        0xffff0000
#define TXGBE_TX_FLAGS_VLAN_PRIO_MASK   0x0000e000
#define TXGBE_TX_FLAGS_VLAN_SHIFT       16

#define TXGBE_SET_FLAG(_input, _flag, _result) \
	(((_flag) <= (_result)) ? \
	 ((u32)((_input) & (_flag)) * ((_result) / (_flag))) : \
	 ((u32)((_input) & (_flag)) / ((_flag) / (_result))))

struct txgbe_mac_operations {
	s32 (*init_hw)(struct txgbe_hw *hw);
	s32 (*reset_hw)(struct txgbe_hw *hw);
	s32 (*start_hw)(struct txgbe_hw *hw);
	s32 (*get_mac_addr)(struct txgbe_hw *hw, u8 *mac_addr);
	s32 (*get_fw_version)(struct txgbe_hw *hw);

	/* Link */
	s32 (*check_link)(struct txgbe_hw *hw,
			  txgbe_link_speed *speed, bool *link_up, bool autoneg_wait_to_complete);

	/* RAR, Multicast, VLAN */
	s32 (*set_rar)(struct txgbe_hw *hw, u32 index, u8 *addr, u32 vmdq, u32 enable_addr);
	s32 (*set_uc_addr)(struct txgbe_hw *hw, u32 index, u8 *addr);

	s32 (*update_mc_addr_list)(struct txgbe_hw *hw, u8 *mc_addr_list,
				   u32 mc_addr_count, txgbe_mc_addr_itr next,
				 bool clear);
	s32 (*update_xcast_mode)(struct txgbe_hw *hw, int xcast_mode);
	s32 (*get_link_state)(struct txgbe_hw *hw, bool *link_state);
	s32 (*set_vfta)(struct txgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on, bool vlvf_bypass);
	s32 (*stop_adapter)(struct txgbe_hw *hw);
};

struct txgbe_mac_info {
	struct txgbe_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum txgbe_mac_type type;

	s32  mc_filter_type;

	bool get_link_status;
	u32  max_tx_queues;
	u32  max_rx_queues;
	u32  max_msix_vectors;
};

struct txgbe_mbx_operations {
	void (*init_params)(struct txgbe_hw *hw);
	s32  (*read)(struct txgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id);
	s32  (*write)(struct txgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id);
	s32  (*read_posted)(struct txgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id);
	s32  (*write_posted)(struct txgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id);
	s32  (*check_for_msg)(struct txgbe_hw *hw, u16 mbx_id);
	s32  (*check_for_ack)(struct txgbe_hw *hw, u16 mbx_id);
	s32  (*check_for_rst)(struct txgbe_hw *hw, u16 mbx_id);
};

struct txgbe_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct txgbe_mbx_info {
	struct txgbe_mbx_operations ops;
	struct txgbe_mbx_stats stats;
	u32 timeout;
	u32 udelay;
	u32 v2p_mailbox; /* buffered r2c bits */
	u16 size;
};

struct txgbe_hw {
	void *back;
	u16 *msg_enable;
	struct pci_dev *pdev;

	u8 __iomem *hw_addr;
	u8 __iomem *b4_addr;

	struct txgbe_mac_info mac;
	struct txgbe_mbx_info mbx;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
	bool adapter_stopped;

	int api_version;

	u32 b4_buf[16];
};

struct txgbe_sw_stats {
	u64 tx_busy;
	u64 tx_restart_queue;
	u64 tx_timeout_count;
	u64 rx_csum_bad;
	u64 rx_no_dma_resources;
	u64 rx_alloc_page_failed;
	u64 rx_alloc_buff_failed;
};

struct txgbe_hw_stats {
	u64 gprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 mprc;
};

struct txgbe_ring_stats {
	u64 packets;
	u64 bytes;
};

struct txgbe_tx_queue_stats {
	u64 tx_restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct txgbe_rx_queue_stats {
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 alloc_rx_page;
	u64 csum_err;
};

/* Context descriptors */
struct txgbe_adv_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 seqnum_seed;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

struct txgbe_tx_desc {
	__le64 pkt_addr;
	__le32 cmd_type_len;
	__le32 status;
};

/*** @txgbe_tx_desc.cmd_type_len ***/
#define TXGBE_TXD_DTALEN(v)          (((v) & 0xFFFF)) /* data buffer length */
#define TXGBE_TXD_TSTAMP             (0x1 << 19) /* IEEE1588 time stamp */
#define TXGBE_TXD_EOP                (0x1 << 24) /* End of Packet */
#define TXGBE_TXD_FCS                (0x1 << 25) /* Insert FCS */
#define TXGBE_TXD_LINKSEC            (0x1 << 26) /* Insert LinkSec */
#define TXGBE_TXD_RS                 (0x1 << 27) /* Report Status */
#define TXGBE_TXD_ECU                (0x1 << 28) /* forward to ECU */
#define TXGBE_TXD_CNTAG              (0x1 << 29) /* insert CN tag */
#define TXGBE_TXD_VLE                (0x1 << 30) /* insert VLAN tag */
#define TXGBE_TXD_TSE                (0x1 << 31) /* enable transmit segmentation */

/*** @txgbe_tx_desc.status ***/
#define TXGBE_TXD_STAT_DD            TXGBE_TXD_CTX_DD /* Descriptor Done */
#define TXGBE_TXD_BAK_DESC           ((0x1) << 4) /* use backup descriptor */
#define TXGBE_TXD_CC                 ((0x1) << 7) /* check context */
#define TXGBE_TXD_IPSEC              ((0x1) << 8) /* request IPSec offload */
#define TXGBE_TXD_TPCS               ((0x1) << 9) /* insert TCP/UDP checksum */
#define TXGBE_TXD_IPCS               ((0x1) << 10) /* insert IP checksum */
#define TXGBE_TXD_EIPCS              ((0x1) << 11) /* insert outer IP checksum */
#define TXGBE_TXD_MNGFLT             ((0x1) << 12) /* enable management filter */
#define TXGBE_TXD_PAYLEN(v)          (((v) & 0x7FFFF) << 13) /* payload length */
struct txgbe_tx_buffer {
	struct txgbe_tx_desc *next_to_watch;
	unsigned long time_stamp;
	union {
		struct sk_buff *skb;
		/* XDP uses address ptr on irq_clean */
		void *data;
	};
	unsigned int bytecount;
	unsigned short gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct txgbe_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	dma_addr_t page_dma;
	struct page *page;
	u32 page_offset;
	u16 pagecnt_bias;
};

struct txgbe_ring;
struct txgbe_ring_container {
	struct txgbe_ring *ring;      /* pointer to linked list of rings */
	unsigned int total_bytes;       /* total bytes processed this int */
	unsigned int total_packets;     /* total packets processed this int */
	u8 count;                       /* total number of rings in vector */
	u16 itr;                        /* current ITR setting for ring */
};

struct txgbe_ring {
	struct txgbe_ring *next;
	struct txgbe_q_vector *q_vector; /* backpointer to host q_vector */
	struct net_device *netdev; /* netdev ring belongs to */
	struct bpf_prog *xdp_prog;
	struct device *dev; /* device for DMA mapping */
	void *desc; /* descriptor ring memory */
	union {
		struct txgbe_tx_buffer *tx_buffer_info;
		struct txgbe_rx_buffer *rx_buffer_info;
	};
	unsigned long state;
	u8 __iomem *tail;
	dma_addr_t dma_addr; /* phys. address of descriptor ring */
	unsigned int size; /* length in bytes */

	u16 count; /* amount of descriptors */

	u8 que_idx; /* software netdev-relative queue offset */
	u8 reg_idx; /* hardware global-absolute ring offset */
	struct sk_buff *skb;
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;

	struct txgbe_ring_stats stats;
	struct u64_stats_sync syncp;

	union {
		struct txgbe_tx_queue_stats tx_stats;
		struct txgbe_rx_queue_stats rx_stats;
	};
} ____cacheline_internodealigned_in_smp;

struct txgbe_adapter {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u8 __iomem *io_addr;
	u8 __iomem *b4_addr;
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct txgbe_hw hw;
	unsigned long state;
	u32 *rss_key;
	u8 rss_indir_tbl[128];
	u32 flags;
	bool link_state;
#define TXGBE_FLAG_RX_CSUM_ENABLED		BIT(1)
#define TXGBE_FLAGS_LEGACY_RX			BIT(2)
#define TXGBE_FLAG_RSS_FIELD_IPV4_UDP		BIT(4)
#define TXGBE_FLAG_RSS_FIELD_IPV6_UDP		BIT(5)

	/* statistic states */
	struct rtnl_link_stats64 net_stats;
	struct txgbe_sw_stats sw_stats;
	struct txgbe_hw_stats stats, last_stats, base_stats, reset_stats;
	struct txgbe_hw_stats reg_stats[MAX_TX_QUEUES], last_reg_stats[MAX_TX_QUEUES];

	/* interrupt vector accounting */
	struct txgbe_q_vector *q_vector[MAX_Q_VECTORS];
	int num_q_vectors;
	struct msix_entry *msix_entries;

	/* Rings, Tx first since it is accessed in hotpath */
	struct txgbe_ring *tx_ring[MAX_TX_QUEUES]; /* One per active queue */
	struct txgbe_ring *rx_ring[MAX_RX_QUEUES]; /* One per active queue */

#define DEFAULT_DEBUG_LEVEL (0x7)
	u16 msg_enable;

	u32 flagsd; /* flags define: CAP */
	u16 bd_number;

	/* mailbox spin lock */
	spinlock_t mbx_lock;

	/* pf statstic spin lock */
	spinlock_t pf_count_lock;

	u32 link_speed;
	bool link_up;

	/* Tx hotpath */
	u16 tx_ring_count;
	u16 num_tx_queues;
	u16 tx_itr_setting;

	/* Rx hotpath */
	u16 rx_ring_count;
	u16 num_rx_queues;
	u16 rx_itr_setting;

	unsigned long last_reset;

	u32 eims_enable_mask;
	u32 eims_other;

	struct timer_list service_timer;
	struct work_struct service_task;
};

struct txgbe_q_vector {
	struct txgbe_adapter *adapter;
	u16 v_idx;
	u16 itr;
	struct napi_struct napi;
	struct txgbe_ring_container rx;
	struct txgbe_ring_container tx;
	struct rcu_head rcu;    /* to avoid race with update stats on free */
	char name[IFNAMSIZ + 17];
	bool netpoll_rx;

	/* for dynamic allocation of rings associated with this q_vector */
	struct txgbe_ring ring[0] ____cacheline_internodealigned_in_smp;
};

union txgbe_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} rd;
	struct {
		struct {
			union {
				__le32 data;
				struct {
					__le16 pkt_info; /* RSS, Pkt type */
					__le16 hdr_info; /* Splithdr, hdrlen */
				} hs_rss;
			} lo_dword;
			union {
				__le32 data; /* RSS Hash */
				struct {
					__le16 ipid; /* IP id */
					__le16 csum; /* Packet Checksum */
				} ip_csum;
			} hi_dword;
		} lower;
		struct {
			__le32 status; /* ext status/error */
			__le16 length; /* Packet length */
			__le16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define ring_uses_large_buffer(ring) \
	test_bit(__TXGBE_RX_3K_BUFFER, &(ring)->state)
#define set_ring_uses_large_buffer(ring) \
	set_bit(__TXGBE_RX_3K_BUFFER, &(ring)->state)
#define clear_ring_uses_large_buffer(ring) \
	clear_bit(__TXGBE_RX_3K_BUFFER, &(ring)->state)

#define ring_uses_build_skb(ring) \
	test_bit(__TXGBE_RX_BUILD_SKB_ENABLED, &(ring)->state)
#define set_ring_build_skb_enabled(ring) \
	set_bit(__TXGBE_RX_BUILD_SKB_ENABLED, &(ring)->state)
#define clear_ring_build_skb_enabled(ring) \
	clear_bit(__TXGBE_RX_BUILD_SKB_ENABLED, &(ring)->state)

#define check_for_tx_hang(ring) \
	test_bit(__TXGBE_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__TXGBE_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__TXGBE_TX_DETECT_HANG, &(ring)->state)

static inline unsigned int txgbe_rx_pg_order(struct txgbe_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return 1;
#endif
	return 0;
}

#define txgbe_rx_pg_size(_ring) (PAGE_SIZE << txgbe_rx_pg_order(_ring))

__maybe_unused static struct net_device *txgbe_hw_to_netdev(const struct txgbe_hw *hw)
{
	struct txgbe_adapter *adapter =
		container_of(hw, struct txgbe_adapter, hw);
	return adapter->netdev;
}

static inline unsigned int txgbe_rx_bufsz(struct txgbe_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return TXGBE_RXBUFFER_3072;

	if (ring_uses_build_skb(ring))
		return TXGBE_MAX_FRAME_BUILD_SKB;
#endif
	return TXGBE_RXBUFFER_2048;
}

#define  txgbevf_dbg(hw, fmt, arg...) \
	netdev_dbg(txgbe_hw_to_netdev(hw), fmt, ##arg)

#define e_dev_info(format, arg...) \
	dev_info(pci_dev_to_dev(adapter->pdev), format, ## arg)
#define e_dev_warn(format, arg...) \
	dev_warn(pci_dev_to_dev(adapter->pdev), format, ## arg)
#define e_dev_err(format, arg...) \
	dev_err(pci_dev_to_dev(adapter->pdev), format, ## arg)
#define e_dev_notice(format, arg...) \
	dev_notice(pci_dev_to_dev(adapter->pdev), format, ## arg)
#define e_dbg(msglvl, format, arg...) \
	netif_dbg(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_info(msglvl, format, arg...) \
	netif_info(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_err(msglvl, format, arg...) \
	netif_err(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_warn(msglvl, format, arg...) \
	netif_warn(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_crit(msglvl, format, arg...) \
	netif_crit(adapter, msglvl, adapter->netdev, format, ## arg)

/* iterator for handling rings in ring container */
#define txgbe_for_each_ring(pos, head) \
	for (pos = (head).ring; pos; pos = pos->next)

/* read register */
#define TXGBE_DEAD_READ_RETRIES     10
#define TXGBE_DEAD_READ_REG         0xdeadbeefU
#define TXGBE_DEAD_READ_REG64       0xdeadbeefdeadbeefULL
#define TXGBE_FAILED_READ_REG       0xffffffffU
#define TXGBE_FAILED_READ_REG64     0xffffffffffffffffULL

#define TXGBE_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

static inline bool TXGBE_REMOVED(void __iomem *addr)
{
	return unlikely(!addr);
}

static inline u32
txgbe_rd32(u8 __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline u32
rd32(struct txgbe_hw *hw, u32 reg)
{
	u8 __iomem *base = READ_ONCE(hw->hw_addr);
	u32 val = TXGBE_FAILED_READ_REG;

	if (unlikely(!base))
		return val;

	val = txgbe_rd32(base, reg);

	return val;
}

#define rd32a(a, reg, offset) ( \
	rd32((a), (reg) + ((offset) << 2)))

static inline u32
rd32m(struct txgbe_hw *hw, u32 reg, u32 mask)
{
	u8 __iomem *base = READ_ONCE(hw->hw_addr);
	u32 val = TXGBE_FAILED_READ_REG;

	if (unlikely(!base))
		return val;

	val = txgbe_rd32(base, reg);
	if (unlikely(val == TXGBE_FAILED_READ_REG))
		return val;

	return val & mask;
}

/* write register */
static inline void
txgbe_wr32(u8 __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
}

static inline void
wr32(struct txgbe_hw *hw, u32 reg, u32 val)
{
	u8 __iomem *base = READ_ONCE(hw->hw_addr);

	if (unlikely(!base))
		return;

	txgbe_wr32(base, reg, val);
}

#define wr32a(a, reg, off, val) \
	wr32((a), (reg) + ((off) << 2), (val))

static inline void
wr32m(struct txgbe_hw *hw, u32 reg, u32 mask, u32 field)
{
	u8 __iomem *base = READ_ONCE(hw->hw_addr);
	u32 val;

	if (unlikely(!base))
		return;

	val = txgbe_rd32(base, reg);
	if (unlikely(val == TXGBE_FAILED_READ_REG))
		return;

	val = ((val & ~mask) | (field & mask));
	txgbe_wr32(base, reg, val);
}

/* poll register */
#define TXGBE_MDIO_TIMEOUT 1000
#define TXGBE_I2C_TIMEOUT  1000
#define TXGBE_SPI_TIMEOUT  1000
static inline s32
po32m(struct txgbe_hw *hw, u32 reg, u32 mask, u32 field, u16 time, u16 loop)
{
	bool msec = false;

	if (time / loop > 1000 * MAX_UDELAY_MS) {
		msec = true;
		time /= 1000;
	}

	do {
		u32 val = rd32(hw, reg);

		if (val == TXGBE_FAILED_READ_REG)
			return TXGBE_ERR_REG_ACCESS;

		if (val != TXGBE_DEAD_READ_REG &&
		    (val & mask) == (field & mask))
			break;
		else if (--loop == 0)
			break;

		if (msec)
			mdelay(time);
		else
			udelay(time);
	} while (true);

	return (loop > 0 ? 0 : -TXGBE_ERR_REG_TMOUT);
}

static inline struct netdev_queue *txring_txq(const struct txgbe_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->que_idx);
}

static inline u16 txgbe_desc_unused(struct txgbe_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

static inline unsigned int txgbe_rx_offset(struct txgbe_ring *rx_ring)
{
	return ring_uses_build_skb(rx_ring) ? TXGBE_SKB_PAD : 0;
}

#define txgbe_flush(a) rd32(a, TXGBE_VXSTATUS)

int txgbevf_open(struct net_device *netdev);
int txgbevf_close(struct net_device *netdev);
int txgbe_negotiate_api_version(struct txgbe_hw *hw, int api);
void txgbe_init_ops_vf(struct txgbe_hw *hw);
s32 txgbe_rlpml_set_vf(struct txgbe_hw *hw, u16 max_size);
int txgbe_get_queues(struct txgbe_hw *hw, unsigned int *num_tcs,
		     unsigned int *default_tc);
void txgbevf_set_rx_mode(struct net_device *netdev);
void txgbe_init_last_counter_stats(struct txgbe_adapter *adapter);
int txgbevf_poll(struct napi_struct *napi, int budget);
void txgbevf_free_rx_resources(struct txgbe_ring *rx_ring);
int txgbe_vlan_rx_add_vid(struct net_device *netdev,
			  __always_unused __be16 proto, u16 vid);
void txgbevf_service_event_schedule(struct txgbe_adapter *adapter);
void txgbevf_write_eitr(struct txgbe_q_vector *q_vector);
void txgbevf_disable_rx_queue(struct txgbe_adapter *adapter,
			      struct txgbe_ring *ring);
void txgbevf_alloc_rx_buffers(struct txgbe_ring *rx_ring,
			      u16 cleaned_count);
void txgbevf_set_ethtool_ops(struct net_device *netdev);
void txgbevf_reinit_locked(struct txgbe_adapter *adapter);
int txgbevf_setup_tx_resources(struct txgbe_ring *tx_ring);
void txgbevf_free_tx_resources(struct txgbe_ring *tx_ring);
int txgbevf_setup_rx_resources(struct txgbe_adapter *adapter,
			       struct txgbe_ring *rx_ring);
void txgbevf_down(struct txgbe_adapter *adapter);
void txgbe_free_irq(struct txgbe_adapter *adapter);
void txgbe_configure(struct txgbe_adapter *adapter);
int txgbe_request_irq(struct txgbe_adapter *adapter);
void txgbe_up_complete(struct txgbe_adapter *adapter);
void txgbevf_reset(struct txgbe_adapter *adapter);
void txgbevf_update_stats(struct txgbe_adapter *adapter);

#endif
