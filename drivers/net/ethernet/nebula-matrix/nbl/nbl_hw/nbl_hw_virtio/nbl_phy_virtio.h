// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_PHY_VIRTIO_H_
#define _NBL_PHY_VIRTIO_H_

#include "nbl_core.h"
#include "nbl_hw.h"
#include "nbl_phy.h"

#define NBL_PCI_CAP_COMMON_CFG			1
#define NBL_PCI_CAP_NOTIFY_CFG			2
#define NBL_PCI_CAP_ISR_CFG			3
#define NBL_PCI_CAP_DEVICE_CFG			4
#define NBL_PCI_CAP_RDMA_CFG			10

struct nbl_phy_init_addr_tbl {
	int comm;
	int isr;
	int notify;
	int device;
	int rdma;
};

struct nbl_pci_cap {
	u8 cap_vndr;
	u8 cap_next;
	u8 cap_len;
	u8 cfg_type;
	u8 bar;
	u8 id;
	u8 padding[2];
	u32 offset;
	u32 length;
};

struct nbl_phy_mgt_virtio {
	struct nbl_phy_mgt phy_mgt;
	u8 __iomem *comm;
	u8 __iomem *notify_base;
	u8 __iomem *isr;
	u8 __iomem *device;
	u8 __iomem *rdma;
	size_t common_len;
	size_t device_len;
};

#endif
