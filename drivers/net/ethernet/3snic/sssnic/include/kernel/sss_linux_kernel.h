/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 3snic Technologies Co., Ltd */

#ifndef SSS_LINUX_KERNEL_H_
#define SSS_LINUX_KERNEL_H_

#include <linux/bitops.h>
#include <net/checksum.h>
#include <net/ipv6.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/if_vlan.h>
#include <linux/udp.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <linux/slab.h>

/* UTS_RELEASE is in a different header starting in kernel 2.6.18 */
#ifndef UTS_RELEASE
/* utsrelease.h changed locations in 2.6.33 */
#if (KERNEL_VERSION(2, 6, 33) > LINUX_VERSION_CODE)
#include <linux/utsrelease.h>
#else
#include <generated/utsrelease.h>
#endif
#endif

#ifndef NETIF_F_SCTP_CSUM
#define NETIF_F_SCTP_CSUM 0
#endif

#ifndef __GFP_COLD
#define __GFP_COLD 0
#endif

#ifndef __GFP_COMP
#define __GFP_COMP 0
#endif

#define HAS_PCIE_ERR_RPT_FUNC

/* ************************************************************************ */
#if (KERNEL_VERSION(2, 6, 22) <= LINUX_VERSION_CODE)
#define ETH_TYPE_TRANS_SETS_DEV
#define HAVE_NETDEV_STATS_IN_NETDEV
#endif /* >= 2.6.22 */

/* ************************************************************************ */
#if (KERNEL_VERSION(2, 6, 34) <= LINUX_VERSION_CODE)
#ifndef HAVE_SET_RX_MODE
#define HAVE_SET_RX_MODE
#endif
#define HAVE_INET6_IFADDR_LIST
#endif /* >= 2.6.34 */

/* ************************************************************************ */
#if (KERNEL_VERSION(2, 6, 36) <= LINUX_VERSION_CODE)
#define HAVE_NDO_GET_STATS64
#endif /* >= 2.6.36 */

/* ************************************************************************ */
#if (KERNEL_VERSION(2, 6, 39) <= LINUX_VERSION_CODE)
#ifndef HAVE_MQPRIO
#define HAVE_MQPRIO
#endif
#ifndef HAVE_SETUP_TC
#define HAVE_SETUP_TC
#endif

#ifndef HAVE_NDO_SET_FEATURES
#define HAVE_NDO_SET_FEATURES
#endif
#define HAVE_IRQ_AFFINITY_NOTIFY
#endif /* >= 2.6.39 */

/* ************************************************************************ */
#if (KERNEL_VERSION(2, 6, 40) <= LINUX_VERSION_CODE)
#define HAVE_ETHTOOL_SET_PHYS_ID
#endif /* >= 2.6.40 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 0, 0) <= LINUX_VERSION_CODE)
#define HAVE_NETDEV_WANTED_FEAUTES
#endif /* >= 3.0.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 2, 0) <= LINUX_VERSION_CODE)
#ifndef HAVE_PCI_DEV_FLAGS_ASSIGNED
#define HAVE_PCI_DEV_FLAGS_ASSIGNED
#define HAVE_VF_SPOOFCHK_CONFIGURE
#endif
#ifndef HAVE_SKB_L4_RXHASH
#define HAVE_SKB_L4_RXHASH
#endif
#endif /* >= 3.2.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
#define HAVE_ETHTOOL_GRXFHINDIR_SIZE
#define HAVE_INT_NDO_VLAN_RX_ADD_VID
#ifdef ETHTOOL_SRXNTUPLE
#undef ETHTOOL_SRXNTUPLE
#endif
#endif /* >= 3.3.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 4, 0) <= LINUX_VERSION_CODE)
#define _kc_kmap_atomic(page) kmap_atomic(page)
#define _kc_kunmap_atomic(addr) kunmap_atomic(addr)
#endif /* >= 3.4.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 5, 0) <= LINUX_VERSION_CODE)
#include <linux/of_net.h>
#define HAVE_FDB_OPS
#define HAVE_ETHTOOL_GET_TS_INFO
#endif /* >= 3.5.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 7, 0) <= LINUX_VERSION_CODE)
#define HAVE_NAPI_GRO_FLUSH_OLD
#endif /* >= 3.7.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
#ifndef HAVE_SRIOV_CONFIGURE
#define HAVE_SRIOV_CONFIGURE
#endif
#endif /* >= 3.8.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE)
#define HAVE_ENCAP_TSO_OFFLOAD
#define HAVE_SKB_INNER_NETWORK_HEADER
#endif /* >= 3.10.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 11, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SET_VF_LINK_STATE
#define HAVE_SKB_INNER_PROTOCOL
#define HAVE_MPLS_FEATURES
#endif /* >= 3.11.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 13, 0) <= LINUX_VERSION_CODE)
#define HAVE_VXLAN_CHECKS
#define HAVE_NDO_SELECT_QUEUE_ACCEL

#define HAVE_NET_GET_RANDOM_ONCE
#define HAVE_HWMON_DEVICE_REGISTER_WITH_GROUPS
#endif /* >= 3.13.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif /* >= 3.14.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 16, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
#define HAVE_VLAN_FIND_DEV_DEEP_RCU
#endif /* >= 3.16.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 18, 0) <= LINUX_VERSION_CODE)
#define HAVE_SKBUFF_CSUM_LEVEL
#define HAVE_MULTI_VLAN_OFFLOAD_EN
#define HAVE_ETH_GET_HEADLEN_FUNC
#endif /* >= 3.18.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 19, 0) <= LINUX_VERSION_CODE)
#define HAVE_RXFH_HASHFUNC
#endif /* >= 3.19.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 4, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SET_VF_TRUST
#endif /* >= 4.4.0 */

/* ************************************************************** */
#if (KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE)
#include <net/devlink.h>
#endif /* >= 4.6.0 */

/* ************************************************************** */
#if (KERNEL_VERSION(4, 8, 0) <= LINUX_VERSION_CODE)
#define HAVE_IO_MAP_WC_SIZE
#endif /* >= 4.8.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
#define HAVE_NETDEVICE_MIN_MAX_MTU
#endif /* >= 4.10.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE)
#define HAVE_VOID_NDO_GET_STATS64
#define HAVE_VM_OPS_FAULT_NO_VMA
#endif /* >= 4.11.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE)
#define HAVE_HWTSTAMP_FILTER_NTP_ALL
#define HAVE_NDO_SETUP_TC_ADM_INDEX
#define HAVE_PCI_ERROR_HANDLER_RESET_PREPARE
#define HAVE_PTP_CLOCK_DO_AUX_WORK
#endif /* >= 4.13.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
#define HAVE_XDP_SUPPORT
#endif /* >= 4.14.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_BPF_NETDEV_BPF
#define HAVE_TIMER_SETUP
#define HAVE_XDP_DATA_META
#endif /* >= 4.15.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_SB_DEV
#endif /* >= 4.19.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 0, 0) <= LINUX_VERSION_CODE)
#define dev_open(x) dev_open(x, NULL)
#define HAVE_NEW_ETHTOOL_LINK_SETTINGS_ONLY

#ifndef get_ds
#define get_ds()	(KERNEL_DS)
#endif

#ifndef dma_zalloc_coherent
#define dma_zalloc_coherent(d, s, h, f) _sss_nic_dma_zalloc_coherent(d, s, h, f)
static inline void *_sss_nic_dma_zalloc_coherent(struct device *dev,
						 size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	/* Above kernel 5.0, fixed up all remaining architectures
	 * to zero the memory in dma_alloc_coherent, and made
	 * dma_zalloc_coherent a no-op wrapper around dma_alloc_coherent,
	 * which fixes all of the above issues.
	 */
	return dma_alloc_coherent(dev, size, dma_handle, gfp);
}
#endif

#if (KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE)
struct timeval {
	__kernel_old_time_t		tv_sec;			/* seconds */
	__kernel_suseconds_t	tv_usec;		/* microseconds */
};
#endif

#ifndef do_gettimeofday
#define do_gettimeofday(time) _kc_do_gettimeofday(time)
static inline void _kc_do_gettimeofday(struct timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
#endif

#endif /* >= 5.0.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_SB_DEV_ONLY
#define ETH_GET_HEADLEN_NEED_DEV
#endif /* >= 5.2.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
#ifndef FIELD_SIZEOF
#define FIELD_SIZEOF(t, f) (sizeof(((t *)0)->f))
#endif
#endif /* >= 5.4.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE)
#define HAVE_DEVLINK_FLASH_UPDATE_PARAMS
#endif /* >= 5.5.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE)
#ifndef rtc_time_to_tm
#define rtc_time_to_tm rtc_time64_to_tm
#endif
#define HAVE_NDO_TX_TIMEOUT_TXQ
#endif /* >= 5.6.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
#define SUPPORTED_COALESCE_PARAMS

#ifndef pci_cleanup_aer_uncorrect_error_status
#define pci_cleanup_aer_uncorrect_error_status pci_aer_clear_nonfatal_status
#endif
#endif /* >= 5.7.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE)
#define HAVE_XDP_FRAME_SZ
#endif /* >= 5.9.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
#define HAS_PCIE_ERR_RPT_FUNC
#endif /* >= 5.15.0 */

#if (KERNEL_VERSION(5, 17, 0) <= LINUX_VERSION_CODE)
#define HAS_DEV_ADDR_SET_FUNC
#endif /* >= 5.17.0 */

#if (KERNEL_VERSION(5, 18, 0) > LINUX_VERSION_CODE)
#define HAS_SET_DMA_MASK_FUNC
#endif /* < 5.18.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE)
#define HAVE_BFP_WARN_NETDEV_PARAM
#define USE_OLD_PCI_FUNCTION
#include <linux/filter.h>
#endif /* >= 6.2.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(6, 5, 0) <= LINUX_VERSION_CODE)
#define HAVE_BFP_WARN_NETDEV_PARAM
#define CLASS_CREATE_WITH_ONE_PARAM
#endif /* >= 6.5.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(6, 6, 0) <= LINUX_VERSION_CODE)
#define HAVE_BFP_WARN_NETDEV_PARAM
#define USE_OLD_PCI_FUNCTION
#define CLASS_CREATE_WITH_ONE_PARAM
#ifdef HAS_PCIE_ERR_RPT_FUNC
#undef HAS_PCIE_ERR_RPT_FUNC
#endif
#endif /* >= 6.6.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(6, 8, 0) <= LINUX_VERSION_CODE)
#define strlcpy strscpy
#define HAVE_RXFH_PARAM
#endif /* >= 6.8.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE)
#define HAVE_DEVLINK_FW_FILE_NAME_PARAM
#endif /* < 5.10.0 */

#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
#define HAVE_DEVLINK_FW_FILE_NAME_MEMBER
#endif /* < 5.11.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE)
#define HAVE_ENCAPSULATION_TSO
#endif /* >= 3.10.0 */

#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
#define HAVE_ENCAPSULATION_CSUM
#endif /* >= 3.8.0 */

#ifndef eth_zero_addr
static inline void __kc_eth_zero_addr(u8 *addr)
{
	memset(addr, 0x00, ETH_ALEN);
}

#define eth_zero_addr(_addr) __kc_eth_zero_addr(_addr)
#endif

#ifndef netdev_hw_addr_list_for_each
#define netdev_hw_addr_list_for_each(ha, l) \
	list_for_each_entry(ha, &(l)->list, list)
#endif

#define spin_lock_deinit(lock)

#define destroy_work(work)

#ifndef HAVE_TIMER_SETUP
void initialize_timer(const void *adapter_hdl, struct timer_list *timer);
#endif

#define nicif_err(priv, type, dev, fmt, args...) \
	netif_level(err, priv, type, dev, "[NIC]" fmt, ##args)
#define nicif_warn(priv, type, dev, fmt, args...) \
	netif_level(warn, priv, type, dev, "[NIC]" fmt, ##args)
#define nicif_notice(priv, type, dev, fmt, args...) \
	netif_level(notice, priv, type, dev, "[NIC]" fmt, ##args)
#define nicif_info(priv, type, dev, fmt, args...) \
	netif_level(info, priv, type, dev, "[NIC]" fmt, ##args)
#define nicif_dbg(priv, type, dev, fmt, args...) \
	netif_level(dbg, priv, type, dev, "[NIC]" fmt, ##args)

#define destroy_completion(completion)
#define sema_deinit(lock)
#define mutex_deinit(lock)
#define rwlock_deinit(lock)

#define tasklet_state(tasklet) ((tasklet)->state)

#ifndef hash_init
#define HASH_SIZE(name) (ARRAY_SIZE(name))

static inline void __hash_init(struct hlist_head *ht, unsigned int sz)
{
	unsigned int i;

	for (i = 0; i < sz; i++)
		INIT_HLIST_HEAD(&ht[i]);
}

#define hash_init(hashtable) __hash_init(hashtable, HASH_SIZE(hashtable))
#endif

#if (KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE)
#ifndef FIELD_SIZEOF
#define FIELD_SIZEOF sizeof_field
#endif

#ifndef HAVE_TX_TIMEOUT_TXQUEUE
#define HAVE_TX_TIMEOUT_TXQUEUE
#endif
#endif /* > 5.5.0 */

#if (KERNEL_VERSION(5, 6, 0) < LINUX_VERSION_CODE)
#define HAS_ETHTOOL_SUPPORTED_COALESCE_PARAMS
#define SSSNIC_SUPPORTED_COALESCE_PARAMS \
	(ETHTOOL_COALESCE_MAX_FRAMES | ETHTOOL_COALESCE_USECS | \
ETHTOOL_COALESCE_USECS | ETHTOOL_COALESCE_MAX_FRAMES | \
ETHTOOL_COALESCE_RX_USECS_LOW | ETHTOOL_COALESCE_RX_USECS_HIGH | \
ETHTOOL_COALESCE_PKT_RATE_LOW | ETHTOOL_COALESCE_PKT_RATE_HIGH | \
ETHTOOL_COALESCE_USE_ADAPTIVE_RX | \
ETHTOOL_COALESCE_RX_MAX_FRAMES_LOW | ETHTOOL_COALESCE_RX_MAX_FRAMES_HIGH)
#endif /* > 5.6.0 */

#if (KERNEL_VERSION(5,9,0) < LINUX_VERSION_CODE)
#ifndef DEVLINK_HAVE_SUPPORTED_FLASH_UPDATE_PARAMS
#define DEVLINK_HAVE_SUPPORTED_FLASH_UPDATE_PARAMS
#endif
#endif /* > 5.9.0 */

#if IS_BUILTIN(CONFIG_NET_DEVLINK)
#if !defined(HAVE_DEVLINK_FLASH_UPDATE_PARAMS) && !defined(__BCLINUX_21_10__)
#define HAVE_DEVLINK_FLASH_UPDATE_PARAMS
#endif
#endif

#include "sss_kcompat.h"

#endif
/* ************************************************************************ */
