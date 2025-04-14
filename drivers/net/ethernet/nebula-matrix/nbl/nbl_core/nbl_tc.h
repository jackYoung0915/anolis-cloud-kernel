// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 nebula-matrix Limited.
 *
 */

#ifndef _NBL_TC_OFFLOAD_H
#define _NBL_TC_OFFLOAD_H

#include "nbl_service.h"

int nbl_serv_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv);
int nbl_serv_indr_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv);

#endif
