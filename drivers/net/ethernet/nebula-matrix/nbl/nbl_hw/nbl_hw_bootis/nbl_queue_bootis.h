// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_QUEUE_BOOTIS_H_
#define _NBL_QUEUE_BOOTIS_H_

#include "nbl_resource.h"
#include "nbl_resource_bootis.h"

#define NBL_BOOTIS_MAX_QID			(4096)

#define NBL_BOOTIS_ECPU_QUEUE_BASE		(1792)
#define NBL_BOOTIS_ECPU_LOCAL_QUEUE(global)	((global) - NBL_BOOTIS_ECPU_QUEUE_BASE)
#define NBL_BOOTIS_ECPU_ETH0_QUEUE_NUM		(16)
#define NBL_BOOTIS_ECPU_ETH1_QUEUE_NUM		(16)
#define NBL_BOOTIS_ECPU_ETH0_QUEUE_BASE		(1824)
#define NBL_BOOTIS_ECPU_ETH1_QUEUE_BASE		(1840)
/* The particular queue for high priority packets, e.g lacp/lldp */
#define NBL_BOOTIS_ECPU_ETH0_FIXED_QUEUE	\
	(NBL_BOOTIS_ECPU_ETH0_QUEUE_BASE + NBL_BOOTIS_ECPU_ETH0_QUEUE_NUM - 1)
#define NBL_BOOTIS_ECPU_ETH1_FIXED_QUEUE		\
	(NBL_BOOTIS_ECPU_ETH1_QUEUE_BASE + NBL_BOOTIS_ECPU_ETH1_QUEUE_NUM - 1)

#define NBL_BOOTIS_ECPU_PF2_NOTIFY_INDEX	(2)
#define NBL_BOOTIS_ECPU_PF3_NOTIFY_INDEX	(3)

enum nbl_rss_hash_func_type {
	NBL_RSS_HASH_FUNC_XOR,
	NBL_RSS_HASH_FUNC_TOEPLITZ,
	NBL_RSS_HASH_FUNC_TPYE_MAX
};

enum nbl_rss_hash_field_type {
	NBL_RSS_HASH_FIELD_TYPE_IP,
	NBL_RSS_HASH_FIELD_TYPE_TCPUDP_OVER_IP,
	NBL_RSS_HASH_FIELD_TYPE_MAX,
};

#endif
