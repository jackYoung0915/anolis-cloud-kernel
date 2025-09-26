/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 3SNIC Co., Ltd */

#ifndef _SSSNIC_KCOMPAT_H_
#define _SSSNIC_KCOMPAT_H_


#undef __always_unused
#define __always_unused __attribute__((__unused__))

#ifndef ETHTOOL_GLINKSETTINGS
/* adapt to SUPPORTED_** and ADVERTISED_**, only 32 bits */
enum ethtool_link_mode_bit_indices {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT	= 5,
	ETHTOOL_LINK_MODE_Autoneg_BIT		= 6,
	ETHTOOL_LINK_MODE_TP_BIT		= 7,
	ETHTOOL_LINK_MODE_FIBRE_BIT		= 10,
	ETHTOOL_LINK_MODE_Pause_BIT		= 13,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT	= 14,
	ETHTOOL_LINK_MODE_Backplane_BIT		= 16,
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT	= 12,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT	= 17,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT	= 19,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT	= 20,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT	= 23,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT	= 24,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT	= 25,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT	= 26,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT	= 31,
};

#ifndef __ETHTOOL_LINK_MODE_MASK_NBITS
#define __ETHTOOL_LINK_MODE_MASK_NBITS 32
#endif
#endif

#ifndef __ETHTOOL_DECLARE_LINK_MODE_MASK
#define __ETHTOOL_DECLARE_LINK_MODE_MASK(name)	\
	DECLARE_BITMAP(name, __ETHTOOL_LINK_MODE_MASK_NBITS)
#endif

#ifndef ETHTOOL_LINK_MODE_1000baseX_Full_BIT
#define ETHTOOL_LINK_MODE_1000baseX_Full_BIT 41
#endif

#ifndef ETHTOOL_LINK_MODE_10000baseCR_Full_BIT
#define ETHTOOL_LINK_MODE_10000baseCR_Full_BIT 42
#define ETHTOOL_LINK_MODE_10000baseSR_Full_BIT 43
#define ETHTOOL_LINK_MODE_10000baseLR_Full_BIT 44
#define ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT 45
#endif

#ifndef ETHTOOL_LINK_MODE_25000baseKR_Full_BIT
#define ETHTOOL_LINK_MODE_25000baseKR_Full_BIT 32
#define ETHTOOL_LINK_MODE_25000baseSR_Full_BIT 33
#endif

#ifndef ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT
#define ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT 34
#define ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT 35
#define ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT 40
#endif

#ifndef ETHTOOL_LINK_MODE_50000baseKR_Full_BIT
#define ETHTOOL_LINK_MODE_50000baseKR_Full_BIT 52
#define ETHTOOL_LINK_MODE_50000baseCR_Full_BIT 54
#define ETHTOOL_LINK_MODE_50000baseSR_Full_BIT 53
#endif

#ifndef ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT
#define ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT 36
#define ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT 37
#define ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT 38
#define ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT 39
#endif

#ifndef ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT
#define ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT 57
#define ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT 59
#define ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT 58
#endif

#ifndef ETHTOOL_LINK_MODE_100000baseKR_Full_BIT
#define ETHTOOL_LINK_MODE_100000baseKR_Full_BIT 75
#define ETHTOOL_LINK_MODE_100000baseCR_Full_BIT 78
#define ETHTOOL_LINK_MODE_100000baseSR_Full_BIT 76
#endif

#ifndef ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT
#define ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT 62
#define ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT 63
#define ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT 66
#endif

#ifndef ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT
#define ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT 80
#define ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT 81
#define ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT 84
#endif

#ifndef SPEED_50000
#define SPEED_50000	50000
#endif

#ifndef SPEED_200000
#define SPEED_200000	200000
#endif

#ifdef ETHTOOL_GMODULEEEPROM
#ifndef ETH_MODULE_SFF_8472
#define ETH_MODULE_SFF_8472 0x2
#endif
#ifndef ETH_MODULE_SFF_8636
#define ETH_MODULE_SFF_8636 0x3
#endif
#ifndef ETH_MODULE_SFF_8436
#define ETH_MODULE_SFF_8436 0x4
#endif
#ifndef ETH_MODULE_SFF_8472_LEN
#define ETH_MODULE_SFF_8472_LEN 512
#endif
#ifndef ETH_MODULE_SFF_8636_MAX_LEN
#define ETH_MODULE_SFF_8636_MAX_LEN 640
#endif
#ifndef ETH_MODULE_SFF_8436_MAX_LEN
#define ETH_MODULE_SFF_8436_MAX_LEN 640
#endif
#endif

#ifndef high_16_bits
#define low_16_bits(x) ((x) & 0xFFFF)
#define high_16_bits(x) (((x) & 0xFFFF0000) >> 16)
#endif

#ifndef U8_MAX
#define U8_MAX 0xFF
#endif

#if (KERNEL_VERSION(3, 11, 0) > LINUX_VERSION_CODE)
#define netdev_notifier_info_to_dev(ptr) ptr
#endif

/* ************************************************************************ */
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
#ifndef bitmap_zalloc
#define bitmap_zalloc(nbits, flags) kmalloc_array(BITS_TO_LONGS(nbits),    \
						  sizeof(unsigned long), flags | __GFP_ZERO);
#endif
#endif

#if (KERNEL_VERSION(5, 3, 0) > LINUX_VERSION_CODE)
#if (KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif
#endif

#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
#ifndef skb_frag_off_add
#define skb_frag_off_add(frag, delta) \
do { \
	(frag)->page_offset += (unsigned short)(delta); \
} while(0)
#endif /* skb_frag_off_add */
#endif /* 5.4.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 9, 0) > LINUX_VERSION_CODE)
#define HAVE_XDP_QUERY_PROG
#endif

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
#ifndef HAVE_ETHTOOL_COALESCE_EXTACK
#define HAVE_ETHTOOL_COALESCE_EXTACK
#endif
#endif /* 5.10.0 */

/* ************************************************************************ */
#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
#ifndef HAVE_ETHTOOL_RINGPARAM_EXTACK
#define HAVE_ETHTOOL_RINGPARAM_EXTACK
#endif
#endif /* 5.10.0 */


#if (KERNEL_VERSION(5, 10, 0) < LINUX_VERSION_CODE)
#define HAVE_DEVLINK_FLASH_UPDATE_PARAMS_FW
#endif /* (KERNEL_VERSION(5, 10, 0) < LINUX_VERSION_CODE)*/

#if (KERNEL_VERSION(4,19,90) >= LINUX_VERSION_CODE)
#define devlink_params_publish(x) do{}while(0)
#define devlink_params_unpublish(x) do{}while(0)
#define DEVLINK_HAS_NO_FLASH_UPDATE
#endif

#if (KERNEL_VERSION(5,15,0) <= LINUX_VERSION_CODE)
#ifndef HAS_DEVLINK_ALLOC_SETS_DEV
#define HAS_DEVLINK_ALLOC_SETS_DEV
#endif
#ifndef NO_DEVLINK_REGISTER_SETS_DEV
#define NO_DEVLINK_REGISTER_SETS_DEV
#endif

#ifndef NO_DEVLINK_PARAMS_PUBLISH
#define NO_DEVLINK_PARAMS_PUBLISH
#endif

#ifndef HAVE_ETHTOOL_COALESCE_EXTACK
#define HAVE_ETHTOOL_COALESCE_EXTACK
#endif

#define devlink_params_publish(x) do{}while(0)
#define devlink_params_unpublish(x) do{}while(0)

#ifndef DEVLINK_REGISTER_RETURN_VOID
#define DEVLINK_REGISTER_RETURN_VOID

#ifdef HAVE_ETHTOOL_RINGPARAM_EXTACK
#undef HAVE_ETHTOOL_RINGPARAM_EXTACK
#endif

#endif

#endif

#if (KERNEL_VERSION(5,15,0) < LINUX_VERSION_CODE)
#ifndef REGISTER_DEVLINK_PARAMETER_PREFERRED
#define REGISTER_DEVLINK_PARAMETER_PREFERRED
#endif
#endif

#if (KERNEL_VERSION(6,0,0) <= LINUX_VERSION_CODE)
/*
 * NEED_NETIF_NAPI_ADD_NO_WEIGHT
 *
 * Upstream commit b48b89f9c189 ("net: drop the weight argument from
 * netif_napi_add") removes weight argument from function call.
 */
#define NEED_NETIF_NAPI_ADD_NO_WEIGHT
#endif

#if (KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE)
#define HAVE_ETHTOOL_RINGPARAM_EXTACK
#endif

#define HAS_EAR_IS_NATIVE
#define HAVE_RXFH_HASHFUNC
#define HAVE_RXFH_PARAM

#endif
