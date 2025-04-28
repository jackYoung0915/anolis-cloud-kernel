// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author: Bennie Yan <bennie@nebula-matrix.com>
 */

#ifndef _NBL_SYSFS_H_
#define _NBL_SYSFS_H_

enum nbl_qos_param_types {
	NBL_QOS_RDMA_SAVE,
	NBL_QOS_RDMA_TC2PRI,
	NBL_QOS_RDMA_SQ_PRI_MAP,
	NBL_QOS_RDMA_RAQ_PRI_MAP,
	NBL_QOS_RDMA_PRI_IMAP,
	NBL_QOS_RDMA_PFC_IMAP,
	NBL_QOS_RDMA_DB_TO_CSCH_EN,
	NBL_QOS_RDMA_SW_DB_CSCH_TH,
	NBL_QOS_RDMA_CSCH_QLEN_TH,
	NBL_QOS_RDMA_POLL_WGT,
	NBL_QOS_RDMA_SP_WRR,

	/* function  base */
	NBL_QOS_RDMA_TC_WGT,
	NBL_QOS_PFC,
	NBL_QOS_PFC_BUFFER,
	NBL_QOS_TRUST,
	NBL_QOS_DSCP2PRIO,
	NBL_QOS_TYPE_MAX
};

struct nbl_sysfs_qos_info {
	int	offset;
	struct nbl_dev_net *net_dev;
	struct kobj_attribute kobj_attr;
};

struct nbl_net_qos {
	struct kobject *qos_kobj;
	struct nbl_sysfs_qos_info qos_info[NBL_QOS_TYPE_MAX];
	u8 pfc[NBL_MAX_PFC_PRIORITIES];
	u8 trust_mode;		/* Trust Mode value 0:802.1p 1: dscp */
	u8 dscp2prio_map[NBL_DSCP_MAX]; /* DSCP -> Priority map */
	int buffer_sizes[NBL_MAX_PFC_PRIORITIES][2];
};

#endif
