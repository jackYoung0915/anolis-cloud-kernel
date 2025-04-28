// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_INTERRUPT_BOOTIS_H_
#define _NBL_INTERRUPT_BOOTIS_H_

#include "nbl_resource.h"

#define NBL_BOOTIS_ECPU_MAX_INTERRUPTS			256
#define NBL_BOOTIS_MAX_OTHER_INTERRUPT			32
#define NBL_BOOTIS_MAX_NET_INTERRUPT			32
#define NBL_BOOTIS_MAX_INTERRUPT_PER_PF	(NBL_BOOTIS_MAX_OTHER_INTERRUPT + \
						NBL_BOOTIS_MAX_NET_INTERRUPT)

#endif
