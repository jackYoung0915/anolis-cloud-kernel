/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#ifndef __ERDMA_SW_H__
#define __ERDMA_SW_H__

#include "kcompat.h"
#include "erdma_verbs.h"

int erdma_compat_init(void);
void erdma_compat_exit(void);

void erdma_gen_port_from_qpn(u32 sip, u32 dip, u32 lqpn, u32 rqpn, u16 *sport,
			     u16 *dport);

int erdma_handle_compat_attr(struct erdma_qp *qp, struct ib_qp_attr *attr,
			     int attr_mask);

int erdma_add_gid(const struct ib_gid_attr *attr, void **context);

int erdma_del_gid(const struct ib_gid_attr *attr, void **context);

int erdma_create_ah(struct ib_ah *ibah,
		    struct rdma_ah_init_attr *init_attr,
		    struct ib_udata *udata);

int erdma_destroy_ah(struct ib_ah *ibah, u32 flags);

#endif
