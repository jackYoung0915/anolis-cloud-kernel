// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_FLOW_BOOTIS_H_
#define _NBL_FLOW_BOOTIS_H_

#include "nbl_core.h"
#include "nbl_hw.h"
#include "nbl_phy_bootis.h"
#include "nbl_resource.h"

#define NBL_MACVLAN_TBL_BUCKET_SIZE			16
#define NBL_MACVLAN_X_AXIS_BUCKET_SIZE			4
#define NBL_MACVLAN_Y_AXIS_BUCKET_SIZE			16

#define NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM	(128)
#define NBL_BOOTIS_MACVLAN_RESULT_TBL_BASE(eth)	(256 + (eth) * NBL_BOOTIS_PER_PF_MACVLAN_MAX_NUM)
struct nbl_flow_macvlan_node_data {
	u16 vsi;
	u16 flow_id;
};

#endif
