// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#ifndef _NBL_RESOURCE_BOOTIS_H_
#define _NBL_RESOURCE_BOOTIS_H_

#include "nbl_resource.h"

#define NBL_MAX_PF_BOOTIS                       5
#define NBL_BOOTIS_ECPU_ETH0_GROUP_ID	(1)
#define NBL_BOOTIS_ECPU_ETH1_GROUP_ID	(1)
#define NBL_LAG_UPA_EXT_TYPE_TBL_ID	0
#define NBL_LAG_UPA_PCMRT_TBL_BASE	0

/* product NO(DF200 as 2)-V NO.R NO.B NO.SP NO */
#define NBL_BOOTIS_DRIVER_VERSION	"2-2.1.100.1"

int nbl_flow_mgt_start_bootis(struct nbl_resource_mgt *res_mgt);
void nbl_flow_mgt_stop_bootis(struct nbl_resource_mgt *res_mgt);
int nbl_flow_setup_ops_bootis(struct nbl_resource_ops *resource_ops);
void nbl_flow_remove_ops_bootis(struct nbl_resource_ops *resource_ops);
int nbl_intr_setup_ops_bootis(struct nbl_resource_ops *res_ops);
int nbl_queue_setup_ops_bootis(struct nbl_resource_ops *res_ops);
void nbl_queue_remove_ops_bootis(struct nbl_resource_ops *resource_ops);
void nbl_intr_remove_ops_bootis(struct nbl_resource_ops *res_ops);
int nbl_port_mgt_start_bootis(struct nbl_resource_mgt *res_mgt);
void nbl_port_mgt_stop_bootis(struct nbl_resource_mgt *res_mgt);
int nbl_port_setup_ops_bootis(struct nbl_resource_ops *res_ops);
void nbl_port_remove_ops_bootis(struct nbl_resource_ops *res_ops);
int nbl_res_intr_init_msix_resource(void *priv);

void nbl_queue_mgt_init_bootis(struct nbl_queue_mgt *queue_mgt);

#endif
