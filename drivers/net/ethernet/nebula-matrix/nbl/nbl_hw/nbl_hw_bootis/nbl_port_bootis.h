// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_PORT_BOOTIS_H_
#define _NBL_PORT_BOOTIS_H_

#include "nbl_resource.h"

#define NBL_I2C_READ_MAXLEN		4
#define NBL_SFF_8636_SLAVE_ADDR		0x50
#define ETH_MODULE_SFF_8636_MAX_LEN	640

struct nbl_eth_statistics_info {
	const char *descp;
	const u32 addr;
};

#endif
