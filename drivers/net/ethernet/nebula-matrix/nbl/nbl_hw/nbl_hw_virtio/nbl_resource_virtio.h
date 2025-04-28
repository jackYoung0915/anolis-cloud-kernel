// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_RESOURCE_VIRTIO_H_
#define _NBL_RESOURCE_VIRTIO_H_

#include <linux/virtio_net.h>
#include "nbl_resource.h"

#define NBL_VIRTIO_RESET_WAIT_TIMES		(2000)
#define NBL_VIRTIO_DEFAULT_DESC_NUM		(1024)
#define NBL_VIRTIO_DEFAULT_MTU			(1500)

#define NBL_PCI_CFG_VIRTIO_VSI_ID		(128) /* 4B */
#define NBL_PCI_CFG_RDMA_MSIX_OFF		(132) /* 4B */
#define NBL_PCI_CFG_EXT_HEADER_OFF		(140) /* 4B */

struct nbl_pci_common_cfg {
	/* About the whole device. */
	u32 device_feature_select;
	u32 device_feature;
	u32 guest_feature_select;
	u32 guest_feature;
	u16 msix_config;
	u16 num_queues;
	u8 device_status;
	u8 config_generation;

	/* About a specific virtqueue */
	u16 queue_select;
	u16 queue_size;
	u16 queue_msix_vector;
	u16 queue_enable;
	u16 queue_notify_off;
	u32 queue_desc_lo;
	u32 queue_desc_hi;
	u32 queue_avail_lo;
	u32 queue_avail_hi;
	u32 queue_used_lo;
	u32 queue_used_hi;
};

struct nbl_pci_net_config {
	u8 mac[ETH_ALEN];
	u16 status;
	u16 max_queue_pairs;
	u16 mtu;
	u32 speed;
	u8 duplex;
	u8 rss_max_key_size;
	u16 rss_max_indirection_table_length;
	u32 supported_hash_types;
	u32 real_num_queues;
};

#define NBL_VIRTIO_DRIVER_FEATURES \
			((1ULL << VIRTIO_NET_F_CSUM) | \
			(1ULL << VIRTIO_NET_F_MTU) | \
			(1ULL << VIRTIO_NET_F_MRG_RXBUF) | \
			(1ULL << VIRTIO_NET_F_MAC) | \
			(1ULL << VIRTIO_NET_F_GSO) | \
			(1ULL << VIRTIO_NET_F_HOST_TSO4) | \
			(1ULL << VIRTIO_NET_F_HOST_TSO6) | \
			(1ULL << VIRTIO_NET_F_CTRL_VQ) | \
			(1ULL << VIRTIO_NET_F_STATUS) | \
			(1ULL << VIRTIO_NET_F_MQ) | \
			(1ULL << VIRTIO_F_VERSION_1) | \
			(1ULL << VIRTIO_F_RING_PACKED) | \
			(1ULL << VIRTIO_F_SR_IOV))

#endif
