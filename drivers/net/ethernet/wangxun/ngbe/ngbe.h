/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _NGBE_H_
#define _NGBE_H_

#include <net/ip.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/if_vlan.h>
#include "ngbe_type.h"
#include <linux/timecounter.h>
#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#define NGBE_MAX_FDIR_INDICES               7
#define NGBE_MAX_RSS_INDICES                8
#define NGBE_MAX_VMDQ_INDICES               8
#define NGBE_MAX_MSIX_Q_VECTORS_EMERALD     9
#define NGBE_MAX_MSIX_VECTORS_EMERALD       9
#define NGBE_RSS_DISABLED_MASK              0

#define NGBE_MAX_RX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)

#define NGBE_INTR_ALL                  0x1FF

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD               512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK           256
#define NGBE_DEFAULT_RXD               512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK           256

#define NGBE_RSS_KEY_SIZE              40
#define NGBE_MAX_RETA_ENTRIES          128

/* ngbe_adapter.flag */
#define NGBE_FLAG_MSI_CAPABLE                  BIT(0)
#define NGBE_FLAG_MSI_ENABLED                  BIT(1)
#define NGBE_FLAG_MSIX_CAPABLE                 BIT(2)
#define NGBE_FLAG_MSIX_ENABLED                 BIT(3)
#define NGBE_FLAG_LLI_PUSH                     BIT(4)

#define NGBE_FLAG_IPSEC_ENABLED                BIT(5)

#define NGBE_FLAG_TPH_ENABLED                  BIT(6)
#define NGBE_FLAG_TPH_CAPABLE                  BIT(7)
#define NGBE_FLAG_TPH_ENABLED_DATA             BIT(8)

#define NGBE_FLAG_MQ_CAPABLE                   BIT(9)
#define NGBE_FLAG_DCB_ENABLED                  BIT(10)
#define NGBE_FLAG_VMDQ_ENABLED                 BIT(11)
#define NGBE_FLAG_FAN_FAIL_CAPABLE             BIT(12)
#define NGBE_FLAG_NEED_LINK_UPDATE             BIT(13)
#define NGBE_FLAG_NEED_ANC_CHECK               BIT(14)
#define NGBE_FLAG_FDIR_HASH_CAPABLE            BIT(15)
#define NGBE_FLAG_FDIR_PERFECT_CAPABLE         BIT(16)
#define NGBE_FLAG_SRIOV_CAPABLE                BIT(19)
#define NGBE_FLAG_SRIOV_ENABLED                BIT(20)
#define NGBE_FLAG_SRIOV_REPLICATION_ENABLE     BIT(21)
#define NGBE_FLAG_SRIOV_L2SWITCH_ENABLE        BIT(22)
#define NGBE_FLAG_SRIOV_VEPA_BRIDGE_MODE       BIT(23)
#define NGBE_FLAG_RX_HWTSTAMP_ENABLED          BIT(24)
#define NGBE_FLAG_VXLAN_OFFLOAD_CAPABLE        BIT(25)
#define NGBE_FLAG_VXLAN_OFFLOAD_ENABLE         BIT(26)
#define NGBE_FLAG_RX_HWTSTAMP_IN_REGISTER      BIT(27)
#define NGBE_FLAG_NEED_ETH_PHY_RESET           BIT(28)
#define NGBE_FLAG_RX_HS_ENABLED                BIT(30)
#define NGBE_FLAG_LINKSEC_ENABLED              BIT(31)

/**
 * ngbe_adapter.flag2
 **/
#define NGBE_FLAG2_RSC_CAPABLE                 BIT(0)
#define NGBE_FLAG2_RSC_ENABLED                 BIT(1)
#define NGBE_FLAG2_TEMP_SENSOR_CAPABLE         BIT(3)
#define NGBE_FLAG2_TEMP_SENSOR_EVENT           BIT(4)
#define NGBE_FLAG2_SEARCH_FOR_SFP              BIT(5)
#define NGBE_FLAG2_SFP_NEEDS_RESET             BIT(6)
#define NGBE_FLAG2_PF_RESET_REQUESTED          BIT(7)
#define NGBE_FLAG2_FDIR_REQUIRES_REINIT        BIT(8)
#define NGBE_FLAG2_RSS_FIELD_IPV4_UDP          BIT(9)
#define NGBE_FLAG2_RSS_FIELD_IPV6_UDP          BIT(10)
#define NGBE_FLAG2_PTP_PPS_ENABLED             BIT(11)
#define NGBE_FLAG2_RSS_ENABLED                 BIT(12)
#define NGBE_FLAG2_EEE_CAPABLE                 BIT(14)
#define NGBE_FLAG2_EEE_ENABLED                 BIT(15)
#define NGBE_FLAG2_VXLAN_REREG_NEEDED          BIT(16)
#define NGBE_FLAG2_DEV_RESET_REQUESTED         BIT(18)
#define NGBE_FLAG2_RESET_INTR_RECEIVED         BIT(19)
#define NGBE_FLAG2_GLOBAL_RESET_REQUESTED      BIT(20)
#define NGBE_FLAG2_MNG_REG_ACCESS_DISABLED     BIT(22)
#define NGBE_FLAG2_SRIOV_MISC_IRQ_REMAP        BIT(23)
#define NGBE_FLAG2_PCIE_NEED_RECOVER           BIT(31)

/* preset defaults */
#define NGBE_FLAGS_SP_INIT (NGBE_FLAG_MSI_CAPABLE |	\
							NGBE_FLAG_MSIX_CAPABLE |\
							NGBE_FLAG_MQ_CAPABLE |	\
							NGBE_FLAG_SRIOV_CAPABLE)

#define NGBE_MAX_JUMBO_FRAME_SIZE      9432 /* max payload 9414 */

#define NGBE_CPU_TO_BE16(_x)                   cpu_to_be16(_x)
#define NGBE_BE16_TO_CPU(_x)                   be16_to_cpu(_x)
#define NGBE_CPU_TO_BE32(_x)                   cpu_to_be32(_x)
#define NGBE_BE32_TO_CPU(_x)                   be32_to_cpu(_x)
#define NGBE_EEPROM_GRANT_ATTEMPTS             100
#define NGBE_HTONL(_i)                         htonl(_i)
#define NGBE_NTOHL(_i)                         ntohl(_i)
#define NGBE_NTOHS(_i)                         ntohs(_i)
#define NGBE_CPU_TO_LE32(_i)                   cpu_to_le32(_i)
#define NGBE_LE32_TO_CPUS(_i)                  le32_to_cpus(_i)

#define NGBE_MAC_STATE_DEFAULT                 0x1
#define NGBE_MAC_STATE_MODIFIED                0x2
#define NGBE_MAC_STATE_IN_USE                  0x4

#define NGBE_MAX_RX_DESC_POLL                  10

#define NGBE_RXBUFFER_2K                       2048
#define NGBE_MAX_RXBUFFER                      16384  /* largest size for single descriptor */
#define NGBE_ETH_FRAMING                       20
#define NGBE_RXBUFFER_256                      256  /* Used for skb receive header */

#define NGBE_RX_HDR_SIZE                       NGBE_RXBUFFER_256
#define NGBE_RX_BUFFER_WRITE                   16      /* Must be power of 2 */

#define NGBE_MAX_VF_MC_ENTRIES                 30
#define NGBE_MAX_VF_FUNCTIONS                  8
#define MAX_RX_QUEUES                          8
#define MAX_TX_QUEUES                          8

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD               512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK           256
#define NGBE_MAX_TXD                   8192
#define NGBE_MIN_TXD                   128

#define NGBE_DEFAULT_RXD               512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK           256
#define NGBE_MAX_RXD                   8192
#define NGBE_MIN_RXD                   128

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define NGBE_REQ_TX_DESCRIPTOR_MULTIPLE        8
#define NGBE_REQ_RX_DESCRIPTOR_MULTIPLE        8

/* Only for array allocations in our adapter struct.
 * we can actually assign 64 queue vectors based on our extended-extended
 * interrupt registers.
 */
#define MAX_MSIX_Q_VECTORS      NGBE_MAX_MSIX_Q_VECTORS_EMERALD
#define MAX_MSIX_COUNT          NGBE_MAX_MSIX_VECTORS_EMERALD

#define MIN_MSIX_Q_VECTORS      1
#define MIN_MSIX_COUNT          (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

#define NGBE_INTR_MISC(A) BIT((A)->num_q_vectors)
#define NGBE_INTR_MISC_VMDQ(A) BIT(((A)->num_q_vectors + (A)->ring_feature[RING_F_VMDQ].offset))
#define NGBE_MAX_MACVLANS      8

#define VMDQ_P(p)       ((p) + adapter->ring_feature[RING_F_VMDQ].offset)

/* VLAN info */
#define NGBE_TX_FLAGS_VLAN_SHIFT       16
#define NGBE_TX_FLAGS_VLAN_MASK        0xffff0000
#define NGBE_MAX_PF_MACVLANS           15

/* Ether Types */
#define NGBE_ETH_P_LLDP                        0x88CC
#define NGBE_ETH_P_CNM                         0x22E7

#define NGBE_DEFAULT_FCPAUSE   0xFFFF

/* iterator for handling rings in ring container */
#define ngbe_for_each_ring(pos, head) \
	for (pos = (head).ring; pos; pos = pos->next)

#define NGBE_RING_SIZE(R) ((R)->count < NGBE_MAX_TXD ? (R)->count / 128 : 0)

#define ring_uses_build_skb(ring) \
	test_bit(__NGBE_RX_BUILD_SKB_ENABLED, &(ring)->state)

#define set_ring_hs_enabled(ring) \
	set_bit(__NGBE_RX_HS_ENABLED, &(ring)->state)
#define clear_ring_hs_enabled(ring) \
	clear_bit(__NGBE_RX_HS_ENABLED, &(ring)->state)
#define ring_is_hs_enabled(ring) \
	test_bit(__NGBE_RX_HS_ENABLED, &(ring)->state)
#define check_for_tx_hang(ring) \
	test_bit(__NGBE_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__NGBE_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__NGBE_TX_DETECT_HANG, &(ring)->state)

#define NGBE_RX_DESC(R, i)     \
	(&(((union ngbe_rx_desc *)((R)->desc))[i]))
#define NGBE_TX_DESC(R, i)     \
	(&(((union ngbe_tx_desc *)((R)->desc))[i]))
#define NGBE_TX_CTXTDESC(R, i) \
	(&(((struct ngbe_tx_context_desc *)((R)->desc))[i]))

#define NGBE_SET_FLAG(_input, _flag, _result) \
	(((_flag) <= (_result)) ? \
	 ((u32)((_input) & (_flag)) * ((_result) / (_flag))) : \
	 ((u32)((_input) & (_flag)) / ((_flag) / (_result))))

#define TCP_TIMER_VECTOR       0
#define OTHER_VECTOR           1
#define NON_Q_VECTORS          (OTHER_VECTOR + TCP_TIMER_VECTOR)
#define NGBE_7K_ITR            595

#define NGBE_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

#define NGBE_FAILED_READ_CFG_WORD  0xffffU

#define DESC_NEEDED     (MAX_SKB_FRAGS + 4)

#define NGBE_INTR_Q(i) (1ULL << (i))

/* macro to make the table lines short */
#define NGBE_PTT(ptype, mac, ip, etype, eip, proto, layer)\
	{       ptype, \
		1, \
		/* mac     */ NGBE_DEC_PTYPE_MAC_##mac, \
		/* ip      */ NGBE_DEC_PTYPE_IP_##ip, \
		/* etype   */ NGBE_DEC_PTYPE_ETYPE_##etype, \
		/* eip     */ NGBE_DEC_PTYPE_IP_##eip, \
		/* proto   */ NGBE_DEC_PTYPE_PROT_##proto, \
		/* layer   */ NGBE_DEC_PTYPE_LAYER_##layer }

#define NGBE_UKN(ptype) \
		{ ptype, 0, 0, 0, 0, 0, 0, 0 }

/* microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define NGBE_70K_ITR          57
#define NGBE_20K_ITR           200
#define NGBE_4K_ITR            1024
#define NGBE_7K_ITR            595

enum ngbe_state_t {
	__NGBE_TESTING,
	__NGBE_RESETTING,
	__NGBE_DOWN,
	__NGBE_HANGING,
	__NGBE_DISABLED,
	__NGBE_REMOVING,
	__NGBE_SERVICE_SCHED,
	__NGBE_SERVICE_INITED,
	__NGBE_IN_SFP_INIT,
	__NGBE_PTP_RUNNING,
	__NGBE_PTP_TX_IN_PROGRESS,
};

enum ngbe_isb_idx {
	NGBE_ISB_HEADER,
	NGBE_ISB_MISC,
	NGBE_ISB_VEC0,
	NGBE_ISB_VEC1,
	NGBE_ISB_MAX
};

enum ngbe_ring_f_enum {
	RING_F_NONE = 0,
	RING_F_VMDQ,  /* SR-IOV uses the same ring feature */
	RING_F_RSS,
	RING_F_ARRAY_SIZE  /* must be last in enum set */
};

enum ngbe_tx_flags {
	/* cmd_type flags */
	NGBE_TX_FLAGS_HW_VLAN  = 0x01,
	NGBE_TX_FLAGS_TSO      = 0x02,
	NGBE_TX_FLAGS_TSTAMP   = 0x04,

	/* olinfo flags */
	NGBE_TX_FLAGS_CC       = 0x08,
	NGBE_TX_FLAGS_IPV4     = 0x10,
	NGBE_TX_FLAGS_CSUM     = 0x20,
	NGBE_TX_FLAGS_OUTER_IPV4 = 0x100,
	NGBE_TX_FLAGS_LINKSEC	= 0x200,
	NGBE_TX_FLAGS_IPSEC    = 0x400,

	/* software defined flags */
	NGBE_TX_FLAGS_SW_VLAN  = 0x40,
	NGBE_TX_FLAGS_FCOE     = 0x80,
};

struct ngbe_mac_addr {
	u8 addr[ETH_ALEN];
	u16 state; /* bitmask */
	u64 pools;
};

struct ngbe_queue_stats {
	u64 packets;
	u64 bytes;
};

enum ngbe_ring_state_t {
		__NGBE_RX_3K_BUFFER,
		__NGBE_RX_BUILD_SKB_ENABLED,
	__NGBE_TX_XPS_INIT_DONE,
	__NGBE_TX_DETECT_HANG,
	__NGBE_HANG_CHECK_ARMED,
	__NGBE_RX_HS_ENABLED,
};

/* ngbe_dec_ptype.mac: outer mac */
enum ngbe_dec_ptype_mac {
	NGBE_DEC_PTYPE_MAC_IP = 0,
	NGBE_DEC_PTYPE_MAC_L2 = 2,
	NGBE_DEC_PTYPE_MAC_FCOE = 3,
};

/* ngbe_dec_ptype.[e]ip: outer&encaped ip */
#define NGBE_DEC_PTYPE_IP_FRAG (0x4)

enum ngbe_dec_ptype_ip {
	NGBE_DEC_PTYPE_IP_NONE = 0,
	NGBE_DEC_PTYPE_IP_IPV4 = 1,
	NGBE_DEC_PTYPE_IP_IPV6 = 2,
	NGBE_DEC_PTYPE_IP_FGV4 =
		(NGBE_DEC_PTYPE_IP_FRAG | NGBE_DEC_PTYPE_IP_IPV4),
	NGBE_DEC_PTYPE_IP_FGV6 =
		(NGBE_DEC_PTYPE_IP_FRAG | NGBE_DEC_PTYPE_IP_IPV6),
};

/* ngbe_dec_ptype.layer: payload layer */
enum ngbe_dec_ptype_layer {
	NGBE_DEC_PTYPE_LAYER_NONE = 0,
	NGBE_DEC_PTYPE_LAYER_PAY2 = 1,
	NGBE_DEC_PTYPE_LAYER_PAY3 = 2,
	NGBE_DEC_PTYPE_LAYER_PAY4 = 3,
};

struct ngbe_dec_ptype {
	u32 ptype:8;
	u32 known:1;
	u32 mac:2; /* outer mac */
	u32 ip:3; /* outer ip*/
	u32 etype:3; /* encaped type */
	u32 eip:3; /* encaped ip */
	u32 prot:4; /* payload proto */
	u32 layer:3; /* payload layer */
};

/* ngbe_dec_ptype.proto: payload proto */
enum ngbe_dec_ptype_prot {
	NGBE_DEC_PTYPE_PROT_NONE = 0,
	NGBE_DEC_PTYPE_PROT_UDP = 1,
	NGBE_DEC_PTYPE_PROT_TCP = 2,
	NGBE_DEC_PTYPE_PROT_SCTP = 3,
	NGBE_DEC_PTYPE_PROT_ICMP = 4,
	NGBE_DEC_PTYPE_PROT_TS = 5, /* time sync */
};

/* ngbe_dec_ptype.etype: encaped type */
enum ngbe_dec_ptype_etype {
	NGBE_DEC_PTYPE_ETYPE_NONE = 0,
	NGBE_DEC_PTYPE_ETYPE_IPIP = 1, /* IP+IP */
	NGBE_DEC_PTYPE_ETYPE_IG = 2, /* IP+GRE */
	NGBE_DEC_PTYPE_ETYPE_IGM = 3, /* IP+GRE+MAC */
	NGBE_DEC_PTYPE_ETYPE_IGMV = 4, /* IP+GRE+MAC+VLAN */
};

struct ngbe_fwd_adapter {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct net_device *vdev;
	struct ngbe_adapter *adapter;
	unsigned int tx_base_queue;
	unsigned int rx_base_queue;
	int index; /* pool index on PF */
};

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct ngbe_tx_buffer {
	union ngbe_tx_desc *next_to_watch;
	unsigned long time_stamp;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct ngbe_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	dma_addr_t page_dma;
	struct page *page;
	unsigned int page_offset;
};

struct ngbe_ring_container {
	struct ngbe_ring *ring;        /* pointer to linked list of rings */
	unsigned int total_bytes;       /* total bytes processed this int */
	unsigned int total_packets;     /* total packets processed this int */
	u16 work_limit;                 /* total work allowed per interrupt */
	u8 count;                       /* total number of rings in vector */
	u8 itr;                         /* current ITR setting for ring */
};

struct ngbe_ring;

struct ngbe_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct ngbe_rx_queue_stats {
	u64 non_eop_descs;
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 csum_good_cnt;
	u64 csum_err;
};

struct ngbe_ring {
	struct ngbe_ring *next;        /* pointer to next ring in q_vector */
	struct ngbe_q_vector *q_vector; /* backpointer to host q_vector */
	struct net_device *netdev;      /* netdev ring belongs to */
	struct device *dev;             /* device for DMA mapping */
	struct ngbe_fwd_adapter *accel;
	void *desc;                     /* descriptor ring memory */
	union {
		struct ngbe_tx_buffer *tx_buffer_info;
		struct ngbe_rx_buffer *rx_buffer_info;
	};
	unsigned long state;
	u8 __iomem *tail;
	dma_addr_t dma;                 /* phys. address of descriptor ring */
	unsigned int size;              /* length in bytes */

	u16 count;                      /* amount of descriptors */

	u8 queue_index; /* needed for multiqueue queue management */

	/* holds the special value that gets
	 * the hardware register offset
	 * associated with this ring, which is
	 * different for DCB and RSS modes
	 */
	u8 reg_idx;

	u16 next_to_use;
	u16 next_to_clean;

	unsigned long last_rx_timestamp;
	u16 rx_buf_len;
	union {
		u16 next_to_alloc;
		struct {
			u8 atr_sample_rate;
			u8 atr_count;
		};
	};

	u8 dcb_tc;
	struct ngbe_queue_stats stats;
	struct u64_stats_sync syncp;

	union {
		struct ngbe_tx_queue_stats tx_stats;
		struct ngbe_rx_queue_stats rx_stats;
	};
} ____cacheline_aligned_in_smp;

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ngbe_q_vector {
	struct ngbe_adapter *adapter;
	int cpu;        /* CPU for DCA */

	struct rcu_head rcu;    /* to avoid race with update stats on free */
	/* index of q_vector within array, also used for
	 * finding the bit in EICR and friends that
	 * represents the vector for this ring
	 */
	u16 v_idx;
	u16 itr;        /* Interrupt throttle rate written to EITR */
	struct ngbe_ring_container rx, tx;

	struct napi_struct napi;
	struct net_device poll_dev;
	cpumask_t affinity_mask;

	int numa_node;
	char name[IFNAMSIZ + 17];
	bool netpoll_rx;

	/* for dynamic allocation of rings associated with this q_vector */
	struct ngbe_ring ring[0] ____cacheline_aligned_in_smp;
};

struct ngbe_ring_feature {
	u16 limit;      /* upper limit on feature indices */
	u16 indices;    /* current value of indices */
	u16 mask;       /* Mask used for feature to ring mapping */
	u16 offset;     /* offset to start of feature */
};

#ifdef CONFIG_HWMON
#define NGBE_HWMON_TYPE_TEMP           0
#define NGBE_HWMON_TYPE_ALARMTHRESH    1
#define NGBE_HWMON_TYPE_DALARMTHRESH   2

struct hwmon_attr {
	struct device_attribute dev_attr;
	struct ngbe_hw *hw;
	struct ngbe_thermal_diode_data *sensor;
	char name[19];
};

struct hwmon_buff {
	struct device *device;
	struct hwmon_attr *hwmon_list;
	unsigned int n_hwmon;
};
#endif /* CONFIG_HWMON */

struct vf_stats {
	u64 gprc;
	u64 gorc;
	u64 gptc;
	u64 gotc;
	u64 mprc;
};

struct vf_data_storage {
	struct pci_dev *vfdev;
	u8 __iomem *b4_addr;
	u32 b4_buf[16];
	unsigned char vf_mac_addresses[ETH_ALEN];
	u16 vf_mc_hashes[NGBE_MAX_VF_MC_ENTRIES];
	u16 num_vf_mc_hashes;
	u16 default_vf_vlan_id;
	u16 vlans_enabled;
	bool clear_to_send;
	struct vf_stats vfstats;
	struct vf_stats last_vfstats;
	struct vf_stats saved_rst_vfstats;
	bool pf_set_mac;
	u16 pf_vlan; /* When set, guest VLAN config not allowed. */
	u16 pf_qos;
	u16 min_tx_rate;
	u16 max_tx_rate;
	u16 vlan_count;
	u8 spoofchk_enabled;

	u8 trusted;
	int xcast_mode;
	unsigned int vf_api;
};

struct vf_macvlans {
	struct list_head l;
	int vf;
	bool free;
	bool is_macvlan;
	u8 vf_macvlan[ETH_ALEN];
};

/* board specific private data structure */
struct ngbe_adapter {
	u8 __iomem *io_addr;    /* Mainly for iounmap use */
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	int max_q_vectors;      /* upper limit of q_vectors for device */
	int num_q_vectors;      /* current number of q_vectors for device */
	struct ngbe_ring_feature ring_feature[RING_F_ARRAY_SIZE];
	struct msix_entry *msix_entries;
	struct ngbe_q_vector *q_vector[MAX_MSIX_Q_VECTORS];
	u32 atr_sample_rate;

	enum ngbe_fc_mode last_lfc_mode;

	u32 flags;
	u32 flags2;
	unsigned long state;

	unsigned int tx_ring_count;
	unsigned int rx_ring_count;
	u64 tx_busy;

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_work_limit;
	u16 tx_itr_setting;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_work_limit;
	u16 rx_itr_setting;

	/* structs defined in ngbe_hw.h */
	struct ngbe_hw hw;
	u16 msg_enable;
	struct ngbe_hw_stats stats;

	struct ngbe_mac_addr *mac_table;
	u8 rss_indir_tbl[NGBE_MAX_RETA_ENTRIES];
	u32 rss_key[NGBE_RSS_KEY_SIZE / sizeof(u32)];

	struct timer_list service_timer;
	struct work_struct service_task;

	/* TX */
	struct ngbe_ring *tx_ring[MAX_TX_QUEUES] ____cacheline_aligned_in_smp;
	u32 tx_timeout_count;
	u64 restart_queue;
	u16 irq_remap_offset;

	/* RX */
	struct ngbe_ring *rx_ring[MAX_RX_QUEUES];
	u64 non_eop_descs;
	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;
	u64 hw_csum_rx_error;
	u64 hw_csum_rx_good;
	u64 hw_rx_no_dma_resources;

	u64 test_icr;
	struct ngbe_ring test_tx_ring;
	struct ngbe_ring test_rx_ring;

	u32 tx_timeout_recovery_level;

	u32 interrupt_event;

	u32 link_speed;
	bool link_up;
	unsigned long link_check_timeout;
	u64 lsc_int;

	DECLARE_BITMAP(active_vfs, NGBE_MAX_VF_FUNCTIONS);
	unsigned int num_vfs;
	struct vf_data_storage *vfinfo;
	struct vf_macvlans vf_mvs;
	struct vf_macvlans *mv_list;
	u32 timer_event_accumulator;
	u32 vferr_refcount;

	u8 default_up;

	unsigned int num_vmdqs;
	unsigned int queues_per_pool;
	u32 wol;
	u32 led_reg;
	char eeprom_id[32];
	u16 eeprom_cap;
	bool netdev_registered;

	/* misc interrupt status block */
	dma_addr_t isb_dma;
	u32 *isb_mem;
	u32 isb_tag[NGBE_ISB_MAX];
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	unsigned long fwd_bitmask; /* bitmask indicating in use pools */

	u32 hang_cnt;
	u32 gphy_efuse[2];

	/* ptp block */
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	struct work_struct ptp_tx_work;
	struct sk_buff *ptp_tx_skb;
	struct hwtstamp_config tstamp_config;
	unsigned long ptp_tx_start;
	unsigned long last_overflow_check;
	unsigned long last_rx_ptp_check;
	spinlock_t tmreg_lock;			/* Used to protect timestamp registers. */
	struct cyclecounter hw_cc;
	struct timecounter hw_tc;
	u32 base_incval;
	u32 tx_hwtstamp_timeouts;
	u32 tx_hwtstamp_skipped;
	u32 rx_hwtstamp_cleared;

#ifdef CONFIG_DEBUG_FS
	struct dentry *ngbe_dbg_adapter;
#endif
#ifdef CONFIG_HWMON
	struct hwmon_buff ngbe_hwmon_buff;
#endif /* CONFIG_HWMON */
	u64 eth_priv_flags;
#define NGBE_ETH_PRIV_FLAG_LLDP		BIT(0)
};

struct ngbe_cb {
	dma_addr_t dma;
	u16     vid;                    /* VLAN tag */
	u16     append_cnt;             /* number of skb's appended */
	bool    page_released;
	bool    dma_released;
};

#define NGBE_CB(skb) ((struct ngbe_cb *)(skb)->cb)

struct ngbe_msg {
	u16 msg_enable;
};

extern char ngbe_driver_name[];
extern const char ngbe_driver_version[];

static inline struct net_device *ngbe_hw_to_netdev(const struct ngbe_hw *hw)
{
	return ((struct ngbe_adapter *)hw->back)->netdev;
}

static inline struct ngbe_msg *ngbe_hw_to_msg(const struct ngbe_hw *hw)
{
	struct ngbe_adapter *adapter =
		container_of(hw, struct ngbe_adapter, hw);
	return (struct ngbe_msg *)&adapter->msg_enable;
}

static inline void ngbe_intr_enable(struct ngbe_hw *hw, u64 qmask)
{
	u32 mask;

	mask = (qmask & 0xFFFFFFFF);
	if (mask)
		wr32(hw, NGBE_PX_IMC, mask);
}

static inline void ngbe_intr_disable(struct ngbe_hw *hw, u64 qmask)
{
	u32 mask;

	mask = (qmask & 0xFFFFFFFF);
	if (mask)
		wr32(hw, NGBE_PX_IMS, mask);
}

static inline void ngbe_intr_trigger(struct ngbe_hw *hw, u64 qmask)
{
	u32 mask;

	mask = (qmask & 0xFFFFFFFF);
	if (mask)
		wr32(hw, NGBE_PX_ICS, mask);
}

static inline struct device *pci_dev_to_dev(struct pci_dev *pdev)
{
	return &pdev->dev;
}

/* FCoE requires that all Rx buffers be over 2200 bytes in length.  Since
 * this is twice the size of a half page we need to double the page order
 * for FCoE enabled Rx queues.
 */
static inline unsigned int ngbe_rx_bufsz(struct ngbe_ring __maybe_unused *ring)
{
#if MAX_SKB_FRAGS < 8
	return ALIGN(NGBE_MAX_RXBUFFER / MAX_SKB_FRAGS, 1024);
#else
	return NGBE_RXBUFFER_2K;
#endif
}

static inline unsigned int ngbe_rx_pg_order(struct ngbe_ring __maybe_unused *ring)
{
	return 0;
}

#define ngbe_rx_pg_size(_ring) (PAGE_SIZE << ngbe_rx_pg_order(_ring))

/* ngbe_desc_unused - calculate if we have unused descriptors */
static inline u16 ngbe_desc_unused(struct ngbe_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

/* ngbe_test_staterr - tests bits in Rx descriptor status and error fields */
static inline __le32 ngbe_test_staterr(union ngbe_rx_desc *rx_desc,
				       const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

static inline u8 ngbe_max_rss_indices(struct ngbe_adapter *adapter)
{
	return NGBE_MAX_RSS_INDICES;
}

static inline u32 ngbe_misc_isb(struct ngbe_adapter *adapter,
				enum ngbe_isb_idx idx)
{
	u32 cur_tag = 0;
	u32 cur_diff = 0;

	cur_tag = adapter->isb_mem[NGBE_ISB_HEADER];
	cur_diff = cur_tag - adapter->isb_tag[idx];

	adapter->isb_tag[idx] = cur_tag;

	return cpu_to_le32(adapter->isb_mem[idx]);
}

enum {
	NGBE_ERROR_SOFTWARE,
	NGBE_ERROR_POLLING,
	NGBE_ERROR_INVALID_STATE,
	NGBE_ERROR_UNSUPPORTED,
	NGBE_ERROR_ARGUMENT,
	NGBE_ERROR_CAUTION,
};

#define ERROR_REPORT(hw, level, format, arg...) do {                      \
	switch (level) {                                                      \
	case NGBE_ERROR_SOFTWARE:                                             \
		fallthrough;                                                \
	case NGBE_ERROR_CAUTION:                                              \
		fallthrough;                                                \
	case NGBE_ERROR_POLLING:                                              \
		netif_warn(ngbe_hw_to_msg(hw), drv, ngbe_hw_to_netdev(hw),        \
				format, ## arg);                                          \
		break;                                                            \
	case NGBE_ERROR_INVALID_STATE:                                        \
		fallthrough;                                                \
	case NGBE_ERROR_UNSUPPORTED:                                          \
		fallthrough;                                                \
	case NGBE_ERROR_ARGUMENT:                                             \
		netif_err(ngbe_hw_to_msg(hw), hw, ngbe_hw_to_netdev(hw),          \
				format, ## arg);                                          \
		break;                                                            \
	default:                                                              \
		break;                                                            \
	}                                                                     \
} while (0)

#define ERROR_REPORT1 ERROR_REPORT
#define ERROR_REPORT2 ERROR_REPORT
#define ERROR_REPORT3 ERROR_REPORT

#define hw_dbg(hw, format, arg...) \
	netdev_dbg(ngbe_hw_to_netdev(hw), format, ## arg)
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

/* needed by ngbe_ethtool.c */
extern char ngbe_driver_name[];

void ngbe_unmap_and_free_tx_res(struct ngbe_ring *ring, struct ngbe_tx_buffer *tx_buffer);
void ngbe_disable_device(struct ngbe_adapter *adapter);
void ngbe_set_interrupt_capability(struct ngbe_adapter *adapter);
void ngbe_set_interrupt_capability(struct ngbe_adapter *adapter);
void ngbe_reset_interrupt_capability(struct ngbe_adapter *adapter);
int ngbe_init_interrupt_scheme(struct ngbe_adapter *adapter);
void ngbe_clear_interrupt_scheme(struct ngbe_adapter *adapter);
int ngbe_poll(struct napi_struct *napi, int budget);
void ngbe_alloc_rx_buffers(struct ngbe_ring *rx_ring, u16 cleaned_count);
int ngbe_del_mac_filter(struct ngbe_adapter *adapter, u8 *addr, u16 pool);
void ngbe_vlan_strip_disable(struct ngbe_adapter *adapter);
void ngbe_set_rx_mode(struct net_device *netdev);
void ngbe_reinit_locked(struct ngbe_adapter *adapter);
void ngbe_reset(struct ngbe_adapter *adapter);
u32 ngbe_read_reg(struct ngbe_hw *hw, u32 reg, bool quiet);
void ngbe_down(struct ngbe_adapter *adapter);
int ngbe_setup_tx_resources(struct ngbe_ring *tx_ring);
void ngbe_free_tx_resources(struct ngbe_ring *tx_ring);
int ngbe_setup_rx_resources(struct ngbe_ring *rx_ring);
void ngbe_free_rx_resources(struct ngbe_ring *rx_ring);
void ngbe_up(struct ngbe_adapter *adapter);
void ngbe_update_stats(struct ngbe_adapter *adapter);
void ngbe_irq_disable(struct ngbe_adapter *adapter);
void ngbe_disable_rx_queue(struct ngbe_adapter *adapter, struct ngbe_ring *ring);
void ngbe_configure_tx_ring(struct ngbe_adapter *adapter, struct ngbe_ring *ring);
void ngbe_configure_rx_ring(struct ngbe_adapter *adapter, struct ngbe_ring *ring);
void ngbe_unmap_and_free_tx_resource(struct ngbe_ring *ring,
				     struct ngbe_tx_buffer *tx_buffer);
netdev_tx_t ngbe_xmit_frame_ring(struct sk_buff *skb,
				 struct ngbe_adapter __maybe_unused *adapter,
				  struct ngbe_ring *tx_ring);
int ngbe_close(struct net_device *netdev);
int ngbe_open(struct net_device *netdev);
int ngbe_wol_supported(struct ngbe_adapter *adapter);
void ngbe_write_eitr(struct ngbe_q_vector *q_vector);
void ngbe_store_reta(struct ngbe_adapter *adapter);
int ngbe_setup_tc(struct net_device *dev, u8 tc);
void ngbe_do_reset(struct net_device *netdev);
u32 ngbe_rss_indir_tbl_entries(struct ngbe_adapter *adapter);

void ngbe_vlan_strip_enable(struct ngbe_adapter *adapter);
void ngbe_ptp_overflow_chqueues_per_pooleck(struct ngbe_adapter *adapter);
void ngbe_ptp_rx_hang(struct ngbe_adapter *adapter);
int ngbe_ptp_get_ts_config(struct ngbe_adapter *adapter, struct ifreq *ifr);
int ngbe_ptp_set_ts_config(struct ngbe_adapter *adapter, struct ifreq *ifr);
void ngbe_ptp_init(struct ngbe_adapter *adapter);
void ngbe_ptp_reset(struct ngbe_adapter *adapter);
void ngbe_ptp_stop(struct ngbe_adapter *adapter);
void ngbe_ptp_check_pps_event(struct ngbe_adapter *adapter);
void ngbe_ptp_rx_hwtstamp(struct ngbe_adapter *adapter, struct sk_buff *skb);
void ngbe_ptp_start_cyclecounter(struct ngbe_adapter *adapter);
void ngbe_ptp_suspend(struct ngbe_adapter *adapter);
void ngbe_ptp_overflow_check(struct ngbe_adapter *adapter);
void ngbe_set_ethtool_ops(struct net_device *netdev);

void ngbe_full_sync_mac_table(struct ngbe_adapter *adapter);
int ngbe_add_mac_filter(struct ngbe_adapter *adapter, u8 *addr, u16 pool);
int ngbe_del_mac_filter(struct ngbe_adapter *adapter, u8 *addr, u16 pool);
void ngbe_sriov_reinit(struct ngbe_adapter *adapter);
int ngbe_sysfs_init(struct ngbe_adapter *adapter);
void ngbe_sysfs_exit(struct ngbe_adapter *adapter);
void ngbe_dbg_adapter_init(struct ngbe_adapter *adapter);
void ngbe_dbg_adapter_exit(struct ngbe_adapter *adapter);
void ngbe_dbg_init(void);
void ngbe_dbg_exit(void);

#endif /* _NGBE_H_ */
