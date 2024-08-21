// SPDX-License-Identifier: GPL-2.0

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

/* Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved. */

#include "kcompat.h"

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <net/addrconf.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>
#include <uapi/rdma/erdma-abi.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

bool rand_qpn;
module_param(rand_qpn, bool, 0444);
MODULE_PARM_DESC(rand_qpn, "randomized qpn");

static void assemble_qbuf_mtt_for_cmd(struct erdma_mem *mtt, u32 *cfg,
				      u64 *addr0, u64 *addr1)
{
	struct erdma_pbl *pbl = mtt->pbl;

	if (mtt->mtt_nents > ERDMA_MAX_INLINE_MTT_ENTRIES) {
		*addr0 = pbl->buf_dma;
		*cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
				   ERDMA_MR_INDIRECT_MTT);
	} else {
		*addr0 = pbl->buf[0];
		memcpy(addr1, pbl->buf + 1, MTT_SIZE(mtt->mtt_nents - 1));
		*cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
				   ERDMA_MR_INLINE_MTT);
	}
}

static int create_qp_cmd(struct erdma_dev *dev, struct erdma_qp *qp,
			 bool is_user)
{
	struct erdma_pd *pd = to_epd(qp->ibqp.pd);
	struct erdma_cmdq_create_qp_req req;
	struct erdma_uqp *user_qp;
	u64 resp0, resp1;
	int err;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_QP_SQ_DEPTH_MASK,
			      ilog2(qp->attrs.sq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_QPN_MASK, QP_ID(qp));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_QP_RQ_DEPTH_MASK,
			      ilog2(qp->attrs.rq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_PD_MASK, pd->pdn);

	if (!is_user) {
		u32 pgsz_range = ilog2(SZ_1M) - ERDMA_HW_PAGE_SHIFT;

		req.sq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);
		req.rq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_OFFSET_MASK, 0) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, 1) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
				   ERDMA_MR_INLINE_MTT);
		req.rq_mtt_cfg = req.sq_mtt_cfg;

		req.rq_buf_addr = qp->kern_qp.rq_buf_dma_addr;
		req.sq_buf_addr = qp->kern_qp.sq_buf_dma_addr;
		req.sq_db_info_dma_addr = qp->kern_qp.sq_db_info_dma_addr;
		req.rq_db_info_dma_addr = qp->kern_qp.rq_db_info_dma_addr;
	} else {
		user_qp = &qp->user_qp;
		req.sq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->sq_mtt.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.sq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);

		req.rq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->rq_mtt.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.rq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg = user_qp->sq_mtt.page_offset;
		req.sq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->sq_mtt.mtt_nents);

		req.rq_mtt_cfg = user_qp->rq_mtt.page_offset;
		req.rq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->rq_mtt.mtt_nents);

		assemble_qbuf_mtt_for_cmd(&user_qp->sq_mtt, &req.sq_mtt_cfg,
					  &req.sq_buf_addr, req.sq_mtt_entry);
		assemble_qbuf_mtt_for_cmd(&user_qp->rq_mtt, &req.rq_mtt_cfg,
					  &req.rq_buf_addr, req.rq_mtt_entry);

		req.sq_db_info_dma_addr = user_qp->sq_db_info_dma_addr;
		req.rq_db_info_dma_addr = user_qp->rq_db_info_dma_addr;
	}

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), &resp0,
				  &resp1);
	if (err)
		return err;

	qp->attrs.cookie =
		FIELD_GET(ERDMA_CMDQ_CREATE_QP_RESP_COOKIE_MASK, resp0);

	return 0;
}

static int regmr_cmd(struct erdma_dev *dev, struct erdma_mr *mr)
{
	struct erdma_pd *pd = to_epd(mr->ibmr.pd);
	u32 mtt_type = ERDMA_MR_INLINE_MTT;
	struct erdma_cmdq_reg_mr_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_REG_MR);

	if (mr->type == ERDMA_MR_TYPE_FRMR ||
	    mr->mem.page_cnt > ERDMA_MAX_INLINE_MTT_ENTRIES) {
		if (mr->mem.pbl->continuous) {
			req.phy_addr[0] = mr->mem.pbl->buf_dma;
			mtt_type = ERDMA_MR_INDIRECT_MTT;
		} else {
			req.phy_addr[0] = sg_dma_address(mr->mem.pbl->sglist);
			mtt_type = mr->mem.pbl->level;
		}
	} else if (mr->type != ERDMA_MR_TYPE_DMA) {
		memcpy(req.phy_addr, mr->mem.pbl->buf,
		       MTT_SIZE(mr->mem.page_cnt));
	}

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MR_VALID_MASK, mr->valid) |
		   FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, mr->ibmr.lkey & 0xFF) |
		   FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, mr->ibmr.lkey >> 8);
	req.cfg1 = FIELD_PREP(ERDMA_CMD_REGMR_PD_MASK, pd->pdn) |
		   FIELD_PREP(ERDMA_CMD_REGMR_TYPE_MASK, mr->type) |
		   FIELD_PREP(ERDMA_CMD_REGMR_RIGHT_MASK, mr->access);
	req.cfg2 = FIELD_PREP(ERDMA_CMD_REGMR_PAGESIZE_MASK,
			      ilog2(mr->mem.page_size)) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_TYPE_MASK, mtt_type) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_CNT_MASK, mr->mem.page_cnt);
	/* Clear this field because hardware will check it. */
	req.size = 0;

	if (mr->type == ERDMA_MR_TYPE_DMA)
		goto post_cmd;

	if (mr->type == ERDMA_MR_TYPE_NORMAL) {
		req.start_va = mr->mem.va;
		req.size = mr->mem.len;
	}

	if (!mr->mem.pbl->continuous && mr->mem.pbl->level > 1) {
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_MR_VERSION_MASK, 1);
		req.cfg2 |= FIELD_PREP(ERDMA_CMD_REGMR_PBL_PAGESIZE_MASK,
				       PAGE_SHIFT - ERDMA_HW_PAGE_SHIFT);
		req.size_h = upper_32_bits(mr->mem.len);
		req.mtt_cnt_h = mr->mem.page_cnt >> 20;
		ibdev_dbg(&dev->ibdev,
			  "cfg0 %x, cfg2 %x, size_h %u, mtt_cmt_h %u\n",
			  req.cfg0, req.cfg2, req.size_h, req.mtt_cnt_h);
		ibdev_dbg(&dev->ibdev, "mtt_0_level: 0x%llx\n",
			  req.phy_addr[0]);
	}

post_cmd:
	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

static int create_cq_cmd(struct erdma_dev *dev, struct erdma_cq *cq,
			 bool is_user)
{
	struct erdma_cmdq_create_cq_req req;
	struct erdma_mem *mtt;
	u32 page_size;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_CQ);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_CQN_MASK, cq->cqn) |
		   FIELD_PREP(ERDMA_CMD_CREATE_CQ_DEPTH_MASK, ilog2(cq->depth));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_EQN_MASK, cq->assoc_eqn);

	if (!is_user) {
		page_size = SZ_32M;
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				       ilog2(page_size) - ERDMA_HW_PAGE_SHIFT);
		req.qbuf_addr_l = lower_32_bits(cq->kern_cq.qbuf_dma_addr);
		req.qbuf_addr_h = upper_32_bits(cq->kern_cq.qbuf_dma_addr);

		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK, 1) |
			    FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
				       ERDMA_MR_INLINE_MTT);

		req.first_page_offset = 0;
		req.cq_db_info_addr =
			cq->kern_cq.qbuf_dma_addr + (cq->depth << CQE_SHIFT);
	} else {
		mtt = &cq->user_cq.qbuf_mtt;
		req.cfg0 |=
			FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				   ilog2(mtt->page_size) - ERDMA_HW_PAGE_SHIFT);
		if (mtt->mtt_nents == 1) {
			req.qbuf_addr_l = lower_32_bits(mtt->pbl->buf[0]);
			req.qbuf_addr_h = upper_32_bits(mtt->pbl->buf[0]);
			req.cfg1 |=
				FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
					   ERDMA_MR_INLINE_MTT);
		} else {
			req.qbuf_addr_l = lower_32_bits(mtt->pbl->buf_dma);
			req.qbuf_addr_h = upper_32_bits(mtt->pbl->buf_dma);
			req.cfg1 |=
				FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
					   ERDMA_MR_INDIRECT_MTT);
		}
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK,
				       mtt->mtt_nents);

		req.first_page_offset = mtt->page_offset;
		req.cq_db_info_addr = cq->user_cq.db_info_dma_addr;
	}

	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

static int erdma_alloc_idx(struct erdma_resource_cb *res_cb)
{
	int idx;
	unsigned long flags;

	spin_lock_irqsave(&res_cb->lock, flags);
	idx = find_next_zero_bit(res_cb->bitmap, res_cb->max_cap,
				 res_cb->next_alloc_idx);
	if (idx == res_cb->max_cap) {
		idx = find_first_zero_bit(res_cb->bitmap, res_cb->max_cap);
		if (idx == res_cb->max_cap) {
			res_cb->next_alloc_idx = 1;
			spin_unlock_irqrestore(&res_cb->lock, flags);
			return -ENOSPC;
		}
	}

	set_bit(idx, res_cb->bitmap);
	res_cb->next_alloc_idx = idx + 1;
	spin_unlock_irqrestore(&res_cb->lock, flags);

	return idx;
}

static inline void erdma_free_idx(struct erdma_resource_cb *res_cb, u32 idx)
{
	unsigned long flags;
	u32 used;

	spin_lock_irqsave(&res_cb->lock, flags);
	used = __test_and_clear_bit(idx, res_cb->bitmap);
	spin_unlock_irqrestore(&res_cb->lock, flags);
	WARN_ON(!used);
}

static struct rdma_user_mmap_entry *
erdma_user_mmap_entry_insert(struct ib_ucontext *uctx, u64 address, u32 size,
			     u8 mmap_flag, u64 *mmap_offset)
{
	struct erdma_user_mmap_entry *entry =
		kzalloc(sizeof(*entry), GFP_KERNEL);
	int ret;

	if (!entry)
		return NULL;

	entry->address = (u64)address;
	entry->mmap_flag = mmap_flag;

	size = PAGE_ALIGN(size);

	ret = rdma_user_mmap_entry_insert(uctx, &entry->rdma_entry, size);
	if (ret) {
		kfree(entry);
		return NULL;
	}

	*mmap_offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

int erdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
		       struct ib_udata *unused)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->max_mr_size = dev->attrs.max_mr_size;
	attr->vendor_id = PCI_VENDOR_ID_ALIBABA;
	attr->vendor_part_id = dev->pdev->device;
	attr->hw_ver = dev->pdev->revision;
	attr->max_qp = dev->attrs.max_qp - 1;
	attr->max_qp_wr = min(dev->attrs.max_send_wr, dev->attrs.max_recv_wr);
	attr->max_qp_rd_atom = dev->attrs.max_ord;
	attr->max_qp_init_rd_atom = dev->attrs.max_ird;
	attr->max_res_rd_atom = dev->attrs.max_qp * dev->attrs.max_ird;
	attr->device_cap_flags =
		IB_DEVICE_LOCAL_DMA_LKEY | IB_DEVICE_MEM_MGT_EXTENSIONS;
	ibdev->local_dma_lkey = dev->attrs.local_dma_key;
	attr->max_send_sge = dev->attrs.max_send_sge;
	attr->max_recv_sge = dev->attrs.max_recv_sge;
	attr->max_sge_rd = dev->attrs.max_sge_rd;
	attr->max_cq = dev->attrs.max_cq - 1;
	attr->max_cqe = dev->attrs.max_cqe;
	attr->max_mr = dev->attrs.max_mr;
	attr->max_pd = dev->attrs.max_pd;
	attr->max_mw = dev->attrs.max_mw;
	attr->max_fast_reg_page_list_len = ERDMA_MAX_FRMR_PA;
	attr->page_size_cap = ERDMA_PAGE_SIZE_SUPPORT;

	if (dev->attrs.cap_flags & ERDMA_DEV_CAP_FLAGS_ATOMIC) {
		attr->atomic_cap = IB_ATOMIC_GLOB;
		attr->masked_atomic_cap = IB_ATOMIC_GLOB;
	}

	attr->fw_ver = ((u64)(dev->attrs.fw_version >> 16) << 32) |
		       (((dev->attrs.fw_version >> 8) & 0xFF) << 16) |
		       ((dev->attrs.fw_version & 0xFF));

	read_lock(&dev->netdev_lock);
	if (dev->netdev)
		addrconf_addr_eui48((u8 *)&attr->sys_image_guid,
				    dev->netdev->dev_addr);
	read_unlock(&dev->netdev_lock);

	return 0;
}

int erdma_query_gid(struct ib_device *ibdev, port_t port, int idx,
		    union ib_gid *gid)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(gid, 0, sizeof(*gid));
	ether_addr_copy(gid->raw, dev->attrs.peer_addr);

	return 0;
}

int erdma_query_port(struct ib_device *ibdev, port_t port,
		     struct ib_port_attr *attr)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->state = dev->port_state;
	attr->active_speed = IB_SPEED_EDR;
	attr->active_width = IB_WIDTH_4X;
	attr->max_mtu = ib_mtu_int_to_enum(dev->mtu);
	attr->active_mtu = ib_mtu_int_to_enum(dev->mtu);

	if (compat_mode)
		attr->gid_tbl_len = 16;
	else
		attr->gid_tbl_len = 1;
	attr->pkey_tbl_len = 1;
	attr->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_DEVICE_MGMT_SUP;
	attr->max_msg_sz = -1;
	if (dev->port_state == IB_PORT_ACTIVE)
		attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	else
		attr->phys_state = IB_PORT_PHYS_STATE_DISABLED;

	return 0;
}

int erdma_get_port_immutable(struct ib_device *ibdev, port_t port,
			     struct ib_port_immutable *port_immutable)
{
	if (compat_mode) {
		port_immutable->gid_tbl_len = 16;
		port_immutable->core_cap_flags =
			RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
		port_immutable->max_mad_size = IB_MGMT_MAD_SIZE;
		port_immutable->pkey_tbl_len = 1;
	} else {
		port_immutable->gid_tbl_len = 1;
		port_immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
	}

	return 0;
}

int erdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);
	int pdn;

	ERDMA_INC_CNT(dev, CMD_ALLOC_PD);

	pdn = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_PD]);
	if (pdn < 0) {
		ERDMA_INC_CNT(dev, CMD_ALLOC_PD_FAILED);
		return pdn;
	}

	pd->pdn = pdn;

	return 0;
}

int erdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibpd->device);
	struct erdma_pd *pd = to_epd(ibpd);

	ERDMA_INC_CNT(dev, CMD_DEALLOC_PD);

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_PD], pd->pdn);
	return 0;
}

static void erdma_flush_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct erdma_qp *qp =
		container_of(dwork, struct erdma_qp, reflush_dwork);
	struct erdma_cmdq_reflush_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_REFLUSH);
	req.qpn = QP_ID(qp);
	req.sq_pi = qp->kern_qp.sq_pi;
	req.rq_pi = qp->kern_qp.rq_pi;
	erdma_post_cmd_wait(&qp->dev->cmdq, &req, sizeof(req), NULL, NULL);
}

static int erdma_qp_validate_cap(struct erdma_dev *dev,
				 struct ib_qp_init_attr *attrs)
{
	ibdev_dbg(
		&dev->ibdev,
		"create_qp_cap:send_wr(%u),recv_wr(%u),send_sge(%u),recv_sge(%u),inline(%u)\n",
		attrs->cap.max_send_wr, attrs->cap.max_recv_wr,
		attrs->cap.max_send_sge, attrs->cap.max_recv_sge,
		attrs->cap.max_inline_data);

	if ((attrs->cap.max_send_wr > dev->attrs.max_send_wr) ||
	    (attrs->cap.max_recv_wr > dev->attrs.max_recv_wr) ||
	    (attrs->cap.max_send_sge > dev->attrs.max_send_sge) ||
	    (attrs->cap.max_recv_sge > dev->attrs.max_recv_sge) ||
	    (attrs->cap.max_inline_data > ERDMA_MAX_INLINE) ||
	    !attrs->cap.max_send_wr || !attrs->cap.max_recv_wr) {
		return -EINVAL;
	}

	return 0;
}

static int erdma_qp_validate_attr(struct erdma_dev *dev,
				  struct ib_qp_init_attr *attrs)
{
	if (attrs->qp_type != IB_QPT_RC)
		return -EOPNOTSUPP;

	if (attrs->srq)
		return -EOPNOTSUPP;

	if (!attrs->send_cq || !attrs->recv_cq)
		return -EOPNOTSUPP;

	return 0;
}

static void free_kernel_qp(struct erdma_qp *qp)
{
	struct erdma_dev *dev = qp->dev;

	vfree(qp->kern_qp.swr_tbl);
	vfree(qp->kern_qp.rwr_tbl);

	if (qp->kern_qp.sq_buf)
		dma_free_coherent(&dev->pdev->dev,
				  qp->attrs.sq_size << SQEBB_SHIFT,
				  qp->kern_qp.sq_buf,
				  qp->kern_qp.sq_buf_dma_addr);

	if (qp->kern_qp.rq_buf)
		dma_free_coherent(&dev->pdev->dev,
				  qp->attrs.rq_size << RQE_SHIFT,
				  qp->kern_qp.rq_buf,
				  qp->kern_qp.rq_buf_dma_addr);

	if (qp->kern_qp.sq_db_info)
		dma_pool_free(dev->db_pool, qp->kern_qp.sq_db_info,
			      qp->kern_qp.sq_db_info_dma_addr);

	if (qp->kern_qp.rq_db_info)
		dma_pool_free(dev->db_pool, qp->kern_qp.rq_db_info,
			      qp->kern_qp.rq_db_info_dma_addr);
}

static int update_kernel_qp_oob_attr(struct erdma_qp *qp,
				     struct ib_qp_attr *attr, int attr_mask)
{
	struct iw_ext_conn_param *param =
		(struct iw_ext_conn_param *)(qp->ibqp.qp_context);

	if (!qp->attrs.connect_without_cm)
		return -EINVAL;

	if (param == NULL)
		return -EINVAL;

	if (attr_mask & IB_QP_DEST_QPN)
		qp->attrs.remote_qp_num = attr->dest_qp_num;

	if (param->sk_addr.family == AF_INET) {
		((struct sockaddr_in *)&qp->attrs.raddr)->sin_family = AF_INET;
		((struct sockaddr_in *)&qp->attrs.laddr)->sin_family = AF_INET;
		qp->attrs.raddr.in.sin_addr.s_addr = param->sk_addr.daddr_v4;
		qp->attrs.laddr.in.sin_addr.s_addr = param->sk_addr.saddr_v4;
	} else if (param->sk_addr.family == AF_INET6) {
		((struct sockaddr_in6 *)&qp->attrs.raddr)->sin6_family =
			AF_INET6;
		((struct sockaddr_in6 *)&qp->attrs.laddr)->sin6_family =
			AF_INET6;
		memcpy(&qp->attrs.raddr.in6.sin6_addr, &param->sk_addr.daddr_v6,
		       sizeof(struct in6_addr));
		memcpy(&qp->attrs.laddr.in6.sin6_addr, &param->sk_addr.saddr_v6,
		       sizeof(struct in6_addr));
	} else {
		return -EINVAL;
	}
	qp->attrs.dport = ntohs(param->sk_addr.dport);
	qp->attrs.sport = param->sk_addr.sport;

	return 0;
}

static int init_kernel_qp(struct erdma_dev *dev, struct erdma_qp *qp,
			  struct ib_qp_init_attr *attrs)
{
	struct erdma_kqp *kqp = &qp->kern_qp;
	int ret = -ENOMEM;

	if (attrs->sq_sig_type == IB_SIGNAL_ALL_WR)
		kqp->sig_all = 1;

	kqp->sq_pi = 0;
	kqp->sq_ci = 0;
	kqp->rq_pi = 0;
	kqp->rq_ci = 0;
	kqp->hw_sq_db = dev->func_bar +
			(ERDMA_SDB_SHARED_PAGE_INDEX << ERDMA_HW_PAGE_SHIFT);
	kqp->hw_rq_db = dev->func_bar + ERDMA_BAR_RQDB_SPACE_OFFSET;

	kqp->swr_tbl = vmalloc(qp->attrs.sq_size * sizeof(u64));
	kqp->rwr_tbl = vmalloc(qp->attrs.rq_size * sizeof(u64));
	if (!kqp->swr_tbl || !kqp->rwr_tbl)
		goto err_out;

	kqp->sq_buf = dma_alloc_coherent(&dev->pdev->dev,
					 qp->attrs.sq_size << SQEBB_SHIFT,
					 &kqp->sq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->sq_buf)
		goto err_out;

	kqp->rq_buf = dma_alloc_coherent(&dev->pdev->dev,
					 qp->attrs.rq_size << RQE_SHIFT,
					 &kqp->rq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->rq_buf)
		goto err_out;

	kqp->sq_db_info = dma_pool_alloc(dev->db_pool, GFP_KERNEL | __GFP_ZERO,
					 &kqp->sq_db_info_dma_addr);
	if (!kqp->sq_db_info)
		goto err_out;

	kqp->rq_db_info = dma_pool_alloc(dev->db_pool, GFP_KERNEL | __GFP_ZERO,
					 &kqp->rq_db_info_dma_addr);
	if (!kqp->rq_db_info)
		goto err_out;

	if (attrs->create_flags & IB_QP_CREATE_IWARP_WITHOUT_CM) {
		struct iw_ext_conn_param *param =
			(struct iw_ext_conn_param *)(attrs->qp_context);

		if (param == NULL) {
			ret = -EINVAL;
			goto err_out;
		}
		if (param->sk_addr.family != PF_INET) {
			ibdev_err_ratelimited(
				&dev->ibdev,
				"IPv4 address is required for connection without CM.\n");
			ret = -EINVAL;
			goto err_out;
		}
		qp->attrs.connect_without_cm = true;
		((struct sockaddr_in *)&qp->attrs.raddr)->sin_family = AF_INET;
		((struct sockaddr_in *)&qp->attrs.laddr)->sin_family = AF_INET;
		qp->attrs.raddr.in.sin_addr.s_addr = param->sk_addr.daddr_v4;
		qp->attrs.laddr.in.sin_addr.s_addr = param->sk_addr.saddr_v4;
		qp->attrs.dport = ntohs(param->sk_addr.dport);
		qp->attrs.sport = param->sk_addr.sport;
	}
	spin_lock_init(&kqp->sq_lock);
	spin_lock_init(&kqp->rq_lock);

	return 0;

err_out:
	free_kernel_qp(qp);
	return ret;
}

static struct erdma_pbl *erdma_create_cont_pbl(struct erdma_dev *dev,
					       size_t size)
{
	struct erdma_pbl *pbl;
	int ret = -ENOMEM;

	pbl = kzalloc(sizeof(*pbl), GFP_KERNEL);
	if (!pbl)
		return ERR_PTR(-ENOMEM);

	pbl->size = size;
	pbl->buf = kzalloc(pbl->size, GFP_KERNEL);
	if (!pbl->buf)
		goto err_free_pbl;

	pbl->continuous = true;
	pbl->buf_dma = dma_map_single(&dev->pdev->dev, pbl->buf, pbl->size,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, pbl->buf_dma))
		goto err_free_pbl_buf;

	ibdev_dbg(&dev->ibdev, "map buffer: va:%p, pa:%llx, size:%lu\n",
		  pbl->buf, pbl->buf_dma, pbl->size);

	return pbl;

err_free_pbl_buf:
	kfree(pbl->buf);

err_free_pbl:
	kfree(pbl);

	return ERR_PTR(ret);
}

static int erdma_create_pbl_buf_sg(struct erdma_dev *dev, struct erdma_pbl *pbl)
{
	struct scatterlist *sglist;
	void *buf = pbl->buf;
	u32 npages, i, nsg;
	struct page *pg;

	/* Failed if buf is not page aligned */
	if ((uintptr_t)buf & ~PAGE_MASK)
		return -EINVAL;

	npages = DIV_ROUND_UP(pbl->size, PAGE_SIZE);
	sglist = vzalloc(npages * sizeof(*sglist));
	if (!sglist)
		return -ENOMEM;

	sg_init_table(sglist, npages);
	for (i = 0; i < npages; i++) {
		pg = vmalloc_to_page(buf);
		if (!pg)
			goto err;
		sg_set_page(&sglist[i], pg, PAGE_SIZE, 0);
		buf += PAGE_SIZE;
	}

	nsg = dma_map_sg(&dev->pdev->dev, sglist, npages, DMA_TO_DEVICE);
	if (!nsg)
		goto err;

	pbl->sglist = sglist;
	pbl->nsg = nsg;

	return 0;

err:
	vfree(sglist);

	return -ENOMEM;
}

static void erdma_destroy_pbl_buf_sg(struct erdma_dev *dev,
				     struct erdma_pbl *pbl)
{
	dma_unmap_sg(&dev->pdev->dev, pbl->sglist, pbl->nsg, DMA_TO_DEVICE);
	vfree(pbl->sglist);
}

static struct erdma_pbl *erdma_create_scatter_pbl(struct erdma_dev *dev,
						  size_t size)
{
	struct erdma_pbl *pbl;
	int ret = -ENOMEM;

	pbl = kzalloc(sizeof(*pbl), GFP_KERNEL);
	if (!pbl)
		return ERR_PTR(-ENOMEM);

	pbl->size = ALIGN(size, PAGE_SIZE);
	pbl->buf = vzalloc(pbl->size);
	pbl->continuous = false;
	if (!pbl->buf)
		goto err_free_pbl;

	ret = erdma_create_pbl_buf_sg(dev, pbl);
	if (ret)
		goto err_free_pbl_buf;

	ibdev_dbg(&dev->ibdev, "create scatter pbl, size:%lu, nsg:%u\n",
		  pbl->size, pbl->nsg);

	return pbl;

err_free_pbl_buf:
	vfree(pbl->buf);

err_free_pbl:
	kfree(pbl);

	return ERR_PTR(ret);
}

static void erdma_destroy_scatter_pbl(struct erdma_dev *dev,
				      struct erdma_pbl *pbl)
{
	erdma_destroy_pbl_buf_sg(dev, pbl);
	vfree(pbl->buf);
	kfree(pbl);
}

static void erdma_init_middle_pbl(struct erdma_pbl *pbl,
				  struct erdma_pbl *next_pbl)
{
	struct scatterlist *sg;
	u32 idx = 0, i;

	for_each_sg(next_pbl->sglist, sg, next_pbl->nsg, i)
		pbl->buf[idx++] = sg_dma_address(sg);
}

static struct erdma_pbl *erdma_create_pbl(struct erdma_dev *dev, size_t size,
					  bool force_continuous)
{
	struct erdma_pbl *pbl, *tmp_pbl;
	int ret, level = 0;

	ibdev_dbg(&dev->ibdev, "create_pbl, size:%lu, force cont:%d\n", size,
		  force_continuous);

	if (!(dev->attrs.cap_flags & ERDMA_DEV_CAP_FLAGS_MTT_VA))
		force_continuous = true;

	if (force_continuous)
		return erdma_create_cont_pbl(dev, size);

	pbl = erdma_create_scatter_pbl(dev, size);
	if (IS_ERR(pbl))
		return pbl;
	level = 1;

	/* convergence the pbl table. */
	while (pbl->nsg != 1 && level <= 3) {
		tmp_pbl = erdma_create_scatter_pbl(dev, MTT_SIZE(pbl->nsg));
		if (IS_ERR(tmp_pbl)) {
			ret = PTR_ERR(tmp_pbl);
			goto err_free_pbl;
		}
		erdma_init_middle_pbl(tmp_pbl, pbl);
		tmp_pbl->low_level = pbl;
		pbl = tmp_pbl;
		level++;
	}

	if (level > 3) {
		ret = -ENOMEM;
		goto err_free_pbl;
	}

	pbl->level = level;
	ibdev_dbg(&dev->ibdev, "top pbl: level:%d, dma_addr 0x%llx\n",
		  pbl->level, pbl->sglist[0].dma_address);

	return pbl;

err_free_pbl:
	while (pbl) {
		tmp_pbl = pbl->low_level;
		erdma_destroy_scatter_pbl(dev, pbl);
		pbl = tmp_pbl;
	}

	return ERR_PTR(ret);
}

static void erdma_init_pbl_leaf(struct erdma_mem *mem, struct erdma_pbl *pbl)
{
	u64 *page_list = pbl->buf;
	struct ib_block_iter biter;
	u32 idx = 0;

	rdma_umem_for_each_dma_block(mem->umem, &biter, mem->page_size)
		page_list[idx++] = rdma_block_iter_dma_address(&biter);
}

static void erdma_init_bottom_pbl(struct erdma_dev *dev, struct erdma_mem *mem)
{
	struct erdma_pbl *pbl = mem->pbl;

	while (pbl->low_level)
		pbl = pbl->low_level;

	erdma_init_pbl_leaf(mem, pbl);
}

static void erdma_destroy_pbl(struct erdma_dev *dev, struct erdma_pbl *pbl)
{
	struct erdma_pbl *tmp_pbl;

	if (pbl->continuous) {
		dma_unmap_single(&dev->pdev->dev, pbl->buf_dma, pbl->size,
				 DMA_TO_DEVICE);
		kfree(pbl->buf);
		kfree(pbl);
	} else {
		while (pbl) {
			tmp_pbl = pbl->low_level;
			erdma_destroy_scatter_pbl(dev, pbl);
			pbl = tmp_pbl;
		}
	}
}

static int get_mtt_entries(struct ib_udata *udata, struct erdma_ucontext *ctx,
			   struct erdma_mem *mem, u64 start, u64 len,
			   int access, u64 virt, unsigned long req_page_size,
			   bool is_mr)
{
	struct erdma_dev *dev = to_edev(ctx->ibucontext.device);
	int ret;

	mem->umem = ib_umem_get(&dev->ibdev, start, len, access);
	if (IS_ERR(mem->umem)) {
		ret = PTR_ERR(mem->umem);
		mem->umem = NULL;
		return ret;
	}

	mem->va = virt;
	mem->len = len;
	mem->page_size = ib_umem_find_best_pgsz(mem->umem, req_page_size, virt);
	mem->page_offset = start & (mem->page_size - 1);
	mem->mtt_nents = ib_umem_num_dma_blocks(mem->umem, mem->page_size);
	mem->page_cnt = mem->mtt_nents;

	ibdev_dbg(&dev->ibdev, "page_size:%u, page_offset:%u, mtt_nents:%u\n",
		  mem->page_size, mem->page_offset, mem->page_cnt);

	mem->pbl = erdma_create_pbl(dev, MTT_SIZE(mem->page_cnt), !is_mr);
	if (IS_ERR(mem->pbl)) {
		ret = PTR_ERR(mem->pbl);
		goto error_ret;
	}

	erdma_init_bottom_pbl(dev, mem);

	return 0;

error_ret:
	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}

	return ret;
}

static void put_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem)
{
	if (mem->pbl)
		erdma_destroy_pbl(dev, mem->pbl);

	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}
}

static int erdma_map_user_dbrecords(struct ib_udata *udata,
				    struct erdma_ucontext *uctx,
				    u64 dbrecords_va,
				    struct erdma_user_dbrecords_page **dbr_page,
				    dma_addr_t *dma_addr)
{
	struct erdma_user_dbrecords_page *page = NULL;
	int rv = 0;

	mutex_lock(&uctx->dbrecords_page_mutex);

	list_for_each_entry(page, &uctx->dbrecords_page_list, list)
		if (page->va == (dbrecords_va & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		rv = -ENOMEM;
		goto out;
	}

	page->va = (dbrecords_va & PAGE_MASK);
	page->refcnt = 0;

	page->umem = ib_umem_get(uctx->ibucontext.device,
				 dbrecords_va & PAGE_MASK, PAGE_SIZE, 0);
	if (IS_ERR(page->umem)) {
		rv = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &uctx->dbrecords_page_list);

found:
	*dma_addr = sg_dma_address(page->umem->sg_head.sgl) +
		    (dbrecords_va & ~PAGE_MASK);
	*dbr_page = page;
	page->refcnt++;

out:
	mutex_unlock(&uctx->dbrecords_page_mutex);
	return rv;
}

static void
erdma_unmap_user_dbrecords(struct erdma_ucontext *ctx,
			   struct erdma_user_dbrecords_page **dbr_page)
{
	if (!ctx || !(*dbr_page))
		return;

	mutex_lock(&ctx->dbrecords_page_mutex);
	if (--(*dbr_page)->refcnt == 0) {
		list_del(&(*dbr_page)->list);
		ib_umem_release((*dbr_page)->umem);
		kfree(*dbr_page);
	}

	*dbr_page = NULL;
	mutex_unlock(&ctx->dbrecords_page_mutex);
}

static int init_user_qp(struct erdma_qp *qp, struct ib_udata *udata,
			struct erdma_ucontext *uctx, u64 va, u32 len,
			u64 db_info_va)
{
	dma_addr_t db_info_dma_addr;
	u32 rq_offset;
	int ret;

	if (len < (ALIGN(qp->attrs.sq_size * SQEBB_SIZE, ERDMA_HW_PAGE_SIZE) +
		   qp->attrs.rq_size * RQE_SIZE))
		return -EINVAL;

	ret = get_mtt_entries(udata, uctx, &qp->user_qp.sq_mtt, va,
			      qp->attrs.sq_size << SQEBB_SHIFT, 0, va,
			      (SZ_1M - SZ_4K), false);
	if (ret)
		return ret;

	rq_offset = ALIGN(qp->attrs.sq_size << SQEBB_SHIFT, ERDMA_HW_PAGE_SIZE);
	qp->user_qp.rq_offset = rq_offset;

	ret = get_mtt_entries(udata, uctx, &qp->user_qp.rq_mtt, va + rq_offset,
			      qp->attrs.rq_size << RQE_SHIFT, 0, va + rq_offset,
			      (SZ_1M - SZ_4K), false);
	if (ret)
		goto put_sq_mtt;

	ret = erdma_map_user_dbrecords(udata, uctx, db_info_va,
				       &qp->user_qp.user_dbr_page,
				       &db_info_dma_addr);
	if (ret)
		goto put_rq_mtt;

	qp->user_qp.sq_db_info_dma_addr = db_info_dma_addr;
	qp->user_qp.rq_db_info_dma_addr = db_info_dma_addr + ERDMA_DB_SIZE;

	return 0;

put_rq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mtt);

put_sq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mtt);

	return ret;
}

static void free_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx)
{
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mtt);
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mtt);
	erdma_unmap_user_dbrecords(uctx, &qp->user_qp.user_dbr_page);
}

int erdma_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attrs,
		    struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_uresp_create_qp uresp;
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_ureq_create_qp ureq;
	struct erdma_ucontext *uctx;
	u32 next_idx;
	int ret;

	uctx = rdma_udata_to_drv_context(udata, struct erdma_ucontext,
					 ibucontext);

	ERDMA_INC_CNT(dev, CMD_CREATE_QP);

	ret = erdma_qp_validate_cap(dev, attrs);
	if (ret)
		goto err_out;

	ret = erdma_qp_validate_attr(dev, attrs);
	if (ret)
		goto err_out;

	qp->scq = to_ecq(attrs->send_cq);
	qp->rcq = to_ecq(attrs->recv_cq);
	qp->dev = dev;
	qp->attrs.cc = dev->attrs.cc;

	init_rwsem(&qp->state_lock);
	kref_init(&qp->ref);
	init_completion(&qp->safe_free);

	if (rand_qpn) {
		get_random_bytes(&next_idx, sizeof(u32));
		dev->next_alloc_qpn = next_idx % dev->attrs.max_qp;
	}
	ret = xa_alloc_cyclic(&dev->qp_xa, &qp->ibqp.qp_num, qp,
			      XA_LIMIT(1, dev->attrs.max_qp - 1),
			      &dev->next_alloc_qpn, GFP_KERNEL);
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_out;
	}

	qp->attrs.sq_size = roundup_pow_of_two(attrs->cap.max_send_wr *
					       ERDMA_MAX_WQEBB_PER_SQE);
	qp->attrs.rq_size = roundup_pow_of_two(attrs->cap.max_recv_wr);

	if (uctx) {
		ret = ib_copy_from_udata(&ureq, udata,
					 min(sizeof(ureq), udata->inlen));
		if (ret)
			goto err_out_xa;

		ret = init_user_qp(qp, udata, uctx, ureq.qbuf_va, ureq.qbuf_len,
				   ureq.db_record_va);
		if (ret)
			goto err_out_xa;

		memset(&uresp, 0, sizeof(uresp));

		uresp.num_sqe = qp->attrs.sq_size;
		uresp.num_rqe = qp->attrs.rq_size;
		uresp.qp_id = QP_ID(qp);
		uresp.rq_offset = qp->user_qp.rq_offset;

		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_out_cmd;
	} else {
		ret = init_kernel_qp(dev, qp, attrs);
		if (ret)
			goto err_out_xa;
	}

	INIT_DELAYED_WORK(&qp->reflush_dwork, erdma_flush_worker);

	qp->attrs.max_send_sge = attrs->cap.max_send_sge;
	qp->attrs.max_recv_sge = attrs->cap.max_recv_sge;
	qp->attrs.state = ERDMA_QP_STATE_IDLE;

	ret = create_qp_cmd(dev, qp, uctx ? true : false);
	if (ret)
		goto err_out_cmd;

	return 0;

err_out_cmd:
	if (uctx)
		free_user_qp(qp, uctx);
	else
		free_kernel_qp(qp);
err_out_xa:
	xa_erase(&dev->qp_xa, QP_ID(qp));
err_out:
	ERDMA_INC_CNT(dev, CMD_CREATE_QP_FAILED);
	return ret;
}

struct ib_qp *erdma_kzalloc_qp(struct ib_pd *ibpd,
			       struct ib_qp_init_attr *attrs,
			       struct ib_udata *udata)
{
	struct erdma_qp *qp;
	int ret;
	struct erdma_ucontext *uctx;

	uctx = rdma_udata_to_drv_context(udata, struct erdma_ucontext,
					 ibucontext);

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		ret = -ENOMEM;
		goto err_out;
	}

	qp->ibqp.res.user = uctx ? 1 : 0;

	qp->ibqp.device = ibpd->device;
	qp->ibqp.pd = ibpd;
	qp->ibqp.qp_type = attrs->qp_type;

	ret = erdma_create_qp(&qp->ibqp, attrs, udata);
	if (ret)
		goto err_free;

		/* clear the field, otherwise core code will have problems. */
	qp->ibqp.res.task = NULL;
	return &qp->ibqp;
err_free:
	kfree(qp);
err_out:
	return ERR_PTR(ret);
}

static int erdma_create_stag(struct erdma_dev *dev, u32 *stag)
{
	int stag_idx;

	stag_idx = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX]);
	if (stag_idx < 0)
		return stag_idx;

	/* For now, we always let key field be zero. */
	*stag = (stag_idx << 8);

	return 0;
}

struct ib_mr *erdma_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	ERDMA_INC_CNT(dev, CMD_GET_DMA_MR);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ERDMA_INC_CNT(dev, CMD_GET_DMA_MR_FAILED);
		return ERR_PTR(-ENOMEM);
	}
	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_DMA;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(acc);
	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto out_remove_stag;
	}

	return &mr->ibmr;

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	ERDMA_INC_CNT(dev, CMD_GET_DMA_MR_FAILED);
	return ERR_PTR(ret);
}

struct ib_mr *erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
				u32 max_num_sg)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	ERDMA_INC_CNT(dev, CMD_ALLOC_MR);

	if (mr_type != IB_MR_TYPE_MEM_REG) {
		ERDMA_INC_CNT(dev, CMD_ALLOC_MR_FAILED);
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (max_num_sg > ERDMA_MR_MAX_MTT_CNT) {
		ibdev_err(&dev->ibdev, "max_num_sg too large:%u", max_num_sg);
		ERDMA_INC_CNT(dev, CMD_ALLOC_MR_FAILED);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ERDMA_INC_CNT(dev, CMD_ALLOC_MR_FAILED);
		return ERR_PTR(-ENOMEM);
	}

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_FRMR;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	/* update it in FRMR. */
	mr->access = ERDMA_MR_ACC_LR | ERDMA_MR_ACC_LW | ERDMA_MR_ACC_RR |
		     ERDMA_MR_ACC_RW;

	mr->mem.page_size = PAGE_SIZE; /* update it later. */
	mr->mem.page_cnt = max_num_sg;

	mr->mem.pbl = erdma_create_pbl(dev, MTT_SIZE(max_num_sg), true);
	if (IS_ERR(mr->mem.pbl)) {
		ret = PTR_ERR(mr->mem.pbl);
		goto out_remove_stag;
	}

	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto out_destroy_pbl;
	}

	return &mr->ibmr;

out_destroy_pbl:
	erdma_destroy_pbl(dev, mr->mem.pbl);

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	ERDMA_INC_CNT(dev, CMD_ALLOC_MR_FAILED);

	return ERR_PTR(ret);
}

static int erdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct erdma_mr *mr = to_emr(ibmr);

	if (mr->mem.mtt_nents >= mr->mem.page_cnt)
		return -1;

	mr->mem.pbl->buf[mr->mem.mtt_nents] = addr;
	mr->mem.mtt_nents++;

	return 0;
}

int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset)
{
	struct erdma_mr *mr = to_emr(ibmr);
	int num;

	mr->mem.mtt_nents = 0;

	num = ib_sg_to_pages(&mr->ibmr, sg, sg_nents, sg_offset,
			     erdma_set_page);

	return num;
}

struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibpd->device);
	struct erdma_mr *mr = NULL;
	u32 stag;
	int ret;
	struct erdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);

	ERDMA_INC_CNT(dev, CMD_REG_USR_MR);

	ibdev_dbg(&dev->ibdev,
		  "start:0x%llx, len:%llu, virt:0x%llx, access:0x%x\n", start,
		  len, virt, access);

	if (!len || len > dev->attrs.max_mr_size) {
		ibdev_err(&dev->ibdev,
			  "ERROR: Out of mr size: %llu, max %llu\n", len,
			  dev->attrs.max_mr_size);
		ERDMA_INC_CNT(dev, CMD_REG_USR_MR_FAILED);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = get_mtt_entries(udata, uctx, &mr->mem, start, len, access, virt,
			      SZ_2G - SZ_4K, true);
	if (ret)
		goto err_out_free;

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto err_out_put_mtt;

	mr->ibmr.lkey = mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->mem.va = virt;
	mr->mem.len = len;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(access);
	if (compat_mode)
		mr->access = mr->access | ERDMA_MR_ACC_RW;
	mr->valid = 1;
	mr->type = ERDMA_MR_TYPE_NORMAL;

	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto err_out_mr;
	}

	return &mr->ibmr;

err_out_mr:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

err_out_put_mtt:
	put_mtt_entries(dev, &mr->mem);

err_out_free:
	kfree(mr);

	ERDMA_INC_CNT(dev, CMD_REG_USR_MR_FAILED);
	return ERR_PTR(ret);
}

int erdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibmr->device);
	struct erdma_mr *mr = to_emr(ibmr);
	struct erdma_cmdq_dereg_mr_req req;
	int ret;

	ERDMA_INC_CNT(dev, CMD_DEREG_MR);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DEREG_MR);

	req.cfg = FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, ibmr->lkey >> 8) |
		  FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, ibmr->lkey & 0xFF);

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (ret) {
		ERDMA_INC_CNT(dev, CMD_DEREG_MR_FAILED);
		dev_err_ratelimited(
			&dev->pdev->dev,
			"ERROR: err code = %d, cmd of dereg mr failed.\n", ret);
		return ret;
	}

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], ibmr->lkey >> 8);

	put_mtt_entries(dev, &mr->mem);

	kfree(mr);
	return 0;
}

int erdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	int err;
	struct erdma_cmdq_destroy_cq_req req;

	ERDMA_INC_CNT(dev, CMD_DESTROY_CQ);

	hrtimer_cancel(&cq->dim.timer);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_CQ);
	req.cqn = cq->cqn;

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err_ratelimited(
			&dev->pdev->dev,
			"ERROR: err code = %d, cmd of destroy cq failed.\n",
			err);
		ERDMA_INC_CNT(dev, CMD_DESTROY_CQ_FAILED);
		return err;
	}
	if (rdma_is_kernel_res(&cq->ibcq.res)) {
		dma_free_coherent(&dev->pdev->dev,
				  WARPPED_BUFSIZE(cq->depth << CQE_SHIFT),
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	} else {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	}

	xa_erase(&dev->cq_xa, cq->cqn);
	return 0;
}

static void erdma_ib_lock_cqs(struct erdma_cq *send_cq,
			      struct erdma_cq *recv_cq)
	__acquires(&send_cq->kern_cq.lock) __acquires(&recv_cq->kern_cq.lock)
{
	if (send_cq) {
		if (recv_cq) {
			if (send_cq->cqn < recv_cq->cqn) {
				spin_lock(&send_cq->kern_cq.lock);
				spin_lock_nested(&recv_cq->kern_cq.lock,
						 SINGLE_DEPTH_NESTING);
			} else if (send_cq->cqn == recv_cq->cqn) {
				spin_lock(&send_cq->kern_cq.lock);
				__acquire(&recv_cq->kern_cq.lock);
			} else {
				spin_lock(&recv_cq->kern_cq.lock);
				spin_lock_nested(&send_cq->kern_cq.lock,
						 SINGLE_DEPTH_NESTING);
			}
		} else {
			spin_lock(&send_cq->kern_cq.lock);
			__acquire(&recv_cq->kern_cq.lock);
		}
	} else if (recv_cq) {
		spin_lock(&recv_cq->kern_cq.lock);
		__acquire(&send_cq->kern_cq.lock);
	} else {
		__acquire(&send_cq->kern_cq.lock);
		__acquire(&recv_cq->kern_cq.lock);
	}
}

static void erdma_ib_unlock_cqs(struct erdma_cq *send_cq,
				struct erdma_cq *recv_cq)
	__releases(&send_cq->kern_cq.lock) __releases(&recv_cq->kern_cq.lock)
{
	if (send_cq) {
		if (recv_cq) {
			if (send_cq->cqn < recv_cq->cqn) {
				spin_unlock(&recv_cq->kern_cq.lock);
				spin_unlock(&send_cq->kern_cq.lock);
			} else if (send_cq->cqn == recv_cq->cqn) {
				__release(&recv_cq->kern_cq.lock);
				spin_unlock(&send_cq->kern_cq.lock);
			} else {
				spin_unlock(&send_cq->kern_cq.lock);
				spin_unlock(&recv_cq->kern_cq.lock);
			}
		} else {
			__release(&recv_cq->kern_cq.lock);
			spin_unlock(&send_cq->kern_cq.lock);
		}
	} else if (recv_cq) {
		__release(&send_cq->kern_cq.lock);
		spin_unlock(&recv_cq->kern_cq.lock);
	} else {
		__release(&recv_cq->kern_cq.lock);
		__release(&send_cq->kern_cq.lock);
	}
}

int erdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	struct erdma_qp_attrs qp_attrs;
	int err;
	struct erdma_cmdq_destroy_qp_req req;
	unsigned long flags;

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		local_irq_save(flags);
		erdma_ib_lock_cqs(qp->scq, qp->rcq);
		qp->attrs.flags |= ERDMA_QP_IN_DESTROY;
		erdma_ib_unlock_cqs(qp->scq, qp->rcq);
		local_irq_restore(flags);
	}

	ERDMA_INC_CNT(dev, CMD_DESTROY_QP);

	down_write(&qp->state_lock);
	qp_attrs.state = ERDMA_QP_STATE_ERROR;
	erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE);
	up_write(&qp->state_lock);

	cancel_delayed_work_sync(&qp->reflush_dwork);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_QP);
	req.qpn = QP_ID(qp);

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err_ratelimited(
			&dev->pdev->dev,
			"ERROR: err code = %d, cmd of destroy qp failed.\n",
			err);
		ERDMA_INC_CNT(dev, CMD_DESTROY_QP_FAILED);
		return err;
	}

	erdma_qp_put(qp);
	wait_for_completion(&qp->safe_free);

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		free_kernel_qp(qp);
	} else {
		put_mtt_entries(dev, &qp->user_qp.sq_mtt);
		put_mtt_entries(dev, &qp->user_qp.rq_mtt);
		erdma_unmap_user_dbrecords(ctx, &qp->user_qp.user_dbr_page);
	}

	if (qp->cep)
		erdma_cep_put(qp->cep);

	xa_erase(&dev->qp_xa, QP_ID(qp));

	kfree(qp);
	return 0;
}

void erdma_qp_get_ref(struct ib_qp *ibqp)
{
	erdma_qp_get(to_eqp(ibqp));
}

void erdma_qp_put_ref(struct ib_qp *ibqp)
{
	erdma_qp_put(to_eqp(ibqp));
}

int erdma_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct erdma_user_mmap_entry *entry;
	pgprot_t prot;
	int err = -EINVAL;

	rdma_entry = rdma_user_mmap_entry_get(ctx, vma);
	if (!rdma_entry)
		return -EINVAL;

	entry = to_emmap(rdma_entry);

	switch (entry->mmap_flag) {
	case ERDMA_MMAP_IO_NC:
		/* map doorbell. */
		prot = pgprot_noncached(vma->vm_page_prot);
		err = rdma_user_mmap_io(ctx, vma, PFN_DOWN(entry->address),
					PAGE_SIZE, prot, rdma_entry);
		break;
	default:
		return -EINVAL;
	}

	rdma_user_mmap_entry_put(rdma_entry);
	return err;
}

void erdma_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct erdma_user_mmap_entry *entry = to_emmap(rdma_entry);

	kfree(entry);
}

#define ERDMA_SDB_PAGE 0
#define ERDMA_SDB_ENTRY 1
#define ERDMA_SDB_SHARED 2

static void alloc_db_resources(struct erdma_dev *dev,
			       struct erdma_ucontext *ctx)
{
	struct erdma_devattr *attrs = &dev->attrs;
	u32 bitmap_idx, hw_page_idx;

	if (attrs->disable_dwqe)
		goto alloc_normal_db;

	/* Try to alloc independent SDB page. */
	spin_lock(&dev->db_bitmap_lock);
	bitmap_idx = find_first_zero_bit(dev->sdb_page, attrs->dwqe_pages);
	if (bitmap_idx != attrs->dwqe_pages) {
		set_bit(bitmap_idx, dev->sdb_page);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_PAGE;
		ctx->sdb_bitmap_idx = bitmap_idx;
		ctx->sdb = dev->func_bar_addr + ERDMA_BAR_SQDB_SPACE_OFFSET +
			   (bitmap_idx << ERDMA_HW_PAGE_SHIFT);

		return;
	}

	bitmap_idx = find_first_zero_bit(dev->sdb_entry, attrs->dwqe_entries);
	if (bitmap_idx != attrs->dwqe_entries) {
		set_bit(bitmap_idx, dev->sdb_entry);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_ENTRY;
		ctx->sdb_bitmap_idx = bitmap_idx;
		hw_page_idx = attrs->dwqe_pages +
			      bitmap_idx / ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
		ctx->sdb_entid = bitmap_idx % ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
		ctx->sdb = dev->func_bar_addr + ERDMA_BAR_SQDB_SPACE_OFFSET +
			   (hw_page_idx << ERDMA_HW_PAGE_SHIFT);

		return;
	}

	spin_unlock(&dev->db_bitmap_lock);

alloc_normal_db:
	ctx->sdb_type = ERDMA_SDB_SHARED;
	ctx->sdb = dev->func_bar_addr +
		   (ERDMA_SDB_SHARED_PAGE_INDEX << ERDMA_HW_PAGE_SHIFT);
}

static void erdma_uctx_user_mmap_entries_remove(struct erdma_ucontext *uctx)
{
	rdma_user_mmap_entry_remove(uctx->sq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->cq_db_mmap_entry);
}

int erdma_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibctx->device);
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_uresp_alloc_ctx uresp = {};
	int ret;

	ERDMA_INC_CNT(dev, CMD_ALLOC_UCTX);

	if (atomic_inc_return(&dev->num_ctx) > ERDMA_MAX_CONTEXT) {
		ret = -ENOMEM;
		goto err_out;
	}

	INIT_LIST_HEAD(&ctx->dbrecords_page_list);
	mutex_init(&ctx->dbrecords_page_mutex);

	alloc_db_resources(dev, ctx);

	ctx->rdb = dev->func_bar_addr + ERDMA_BAR_RQDB_SPACE_OFFSET;
	ctx->cdb = dev->func_bar_addr + ERDMA_BAR_CQDB_SPACE_OFFSET;

	ctx->sq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ibctx, (u64)ctx->sdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.sdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -ENOMEM;
		goto err_out;
	}

	ctx->rq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ibctx, (u64)ctx->rdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.rdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	ctx->cq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ibctx, (u64)ctx->cdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.cdb);
	if (!ctx->cq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	uresp.dev_id = dev->pdev->device;
	uresp.sdb_type = ctx->sdb_type;
	uresp.sdb_entid = ctx->sdb_entid;
	uresp.sdb_off = ctx->sdb & ~PAGE_MASK;
	uresp.rdb_off = ctx->rdb & ~PAGE_MASK;
	uresp.cdb_off = ctx->cdb & ~PAGE_MASK;

	ret = ib_copy_to_udata(udata, &uresp,
			       min(sizeof(uresp), udata->outlen));
	if (ret)
		goto err_out;

	return 0;

err_out:
	erdma_uctx_user_mmap_entries_remove(ctx);
	atomic_dec(&dev->num_ctx);

	if (ret)
		ERDMA_INC_CNT(dev, CMD_ALLOC_UCTX_FAILED);

	return ret;
}

void erdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = to_edev(ibctx->device);

	ERDMA_INC_CNT(dev, CMD_DEALLOC_UCTX);

	spin_lock(&dev->db_bitmap_lock);
	if (ctx->sdb_type == ERDMA_SDB_PAGE)
		clear_bit(ctx->sdb_bitmap_idx, dev->sdb_page);
	else if (ctx->sdb_type == ERDMA_SDB_ENTRY)
		clear_bit(ctx->sdb_bitmap_idx, dev->sdb_entry);
	erdma_uctx_user_mmap_entries_remove(ctx);

	spin_unlock(&dev->db_bitmap_lock);

	atomic_dec(&dev->num_ctx);
}

static int ib_qp_state_to_erdma_qp_state[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = ERDMA_QP_STATE_IDLE,
	[IB_QPS_INIT] = ERDMA_QP_STATE_IDLE,
	[IB_QPS_RTR] = ERDMA_QP_STATE_RTR,
	[IB_QPS_RTS] = ERDMA_QP_STATE_RTS,
	[IB_QPS_SQD] = ERDMA_QP_STATE_CLOSING,
	[IB_QPS_SQE] = ERDMA_QP_STATE_TERMINATE,
	[IB_QPS_ERR] = ERDMA_QP_STATE_ERROR
};

#define IB_QP_OOB_CONN_ATTR IB_QP_RESERVED1
int erdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata)
{
	enum erdma_qp_attr_mask erdma_attr_mask = 0;
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_qp_attrs new_attrs;
	int ret = 0;

	if (attr_mask & IB_QP_OOB_CONN_ATTR) {
		ret = update_kernel_qp_oob_attr(qp, attr, attr_mask);
		if (ret)
			return ret;
	}

	if (compat_mode)
		erdma_handle_compat_attr(qp, attr, attr_mask);

	memset(&new_attrs, 0, sizeof(new_attrs));

	if (attr_mask & IB_QP_STATE) {
		new_attrs.state = ib_qp_state_to_erdma_qp_state[attr->qp_state];
		if ((qp->attrs.connect_without_cm || compat_mode) &&
		    new_attrs.state == ERDMA_QP_STATE_RTR)
			new_attrs.state = ERDMA_QP_STATE_RTS;
		erdma_attr_mask |= ERDMA_QP_ATTR_STATE;
	}

	down_write(&qp->state_lock);

	ret = erdma_modify_qp_internal(qp, &new_attrs, erdma_attr_mask);

	up_write(&qp->state_lock);

	return ret;
}

static inline enum ib_qp_state query_qp_state(struct erdma_qp *qp)
{
	switch (qp->attrs.state) {
	case ERDMA_QP_STATE_IDLE:
		return IB_QPS_INIT;
	case ERDMA_QP_STATE_RTR:
		return IB_QPS_RTR;
	case ERDMA_QP_STATE_RTS:
		return IB_QPS_RTS;
	case ERDMA_QP_STATE_CLOSING:
		return IB_QPS_ERR;
	case ERDMA_QP_STATE_TERMINATE:
		return IB_QPS_ERR;
	case ERDMA_QP_STATE_ERROR:
		return IB_QPS_ERR;
	default:
		return IB_QPS_ERR;
	}
}

int erdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		   int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct erdma_qp *qp;
	struct erdma_dev *dev;

	if (ibqp && qp_attr && qp_init_attr) {
		qp = to_eqp(ibqp);
		dev = to_edev(ibqp->device);
	} else {
		return -EINVAL;
	}

	qp_attr->cap.max_inline_data = ERDMA_MAX_INLINE;
	qp_init_attr->cap.max_inline_data = ERDMA_MAX_INLINE;

	qp_attr->cap.max_send_wr = qp->attrs.sq_size;
	qp_attr->cap.max_recv_wr = qp->attrs.rq_size;
	qp_attr->cap.max_send_sge = qp->attrs.max_send_sge;
	qp_attr->cap.max_recv_sge = qp->attrs.max_recv_sge;

	qp_attr->path_mtu = ib_mtu_int_to_enum(dev->mtu);
	qp_attr->max_rd_atomic = qp->attrs.irq_size;
	qp_attr->max_dest_rd_atomic = qp->attrs.orq_size;

	qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ;

	qp_init_attr->cap = qp_attr->cap;

	qp_attr->qp_state = query_qp_state(qp);
	qp_attr->cur_qp_state = query_qp_state(qp);

	return 0;
}

static int erdma_init_user_cq(struct ib_udata *udata,
			      struct erdma_ucontext *uctx, struct erdma_cq *cq,
			      struct erdma_ureq_create_cq *ureq)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);
	int ret;

	ret = get_mtt_entries(udata, uctx, &cq->user_cq.qbuf_mtt, ureq->qbuf_va,
			      ureq->qbuf_len, 0, ureq->qbuf_va, SZ_64M - SZ_4K,
			      false);
	if (ret)
		return ret;

	ret = erdma_map_user_dbrecords(udata, uctx, ureq->db_record_va,
				       &cq->user_cq.user_dbr_page,
				       &cq->user_cq.db_info_dma_addr);
	if (ret)
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);

	return ret;
}

static void init_cq_dbrec(struct erdma_cq *cq)
{
	u64 db_data =
		FIELD_PREP(ERDMA_CQDB_IDX_MASK, 0xFF) |
		FIELD_PREP(ERDMA_CQDB_CQN_MASK, cq->cqn) |
		FIELD_PREP(ERDMA_CQDB_CMDSN_MASK, 3);

	*cq->kern_cq.db_record = db_data;
}

static int erdma_init_kernel_cq(struct erdma_cq *cq)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);

	cq->kern_cq.qbuf =
		dma_alloc_coherent(&dev->pdev->dev,
				   WARPPED_BUFSIZE(cq->depth << CQE_SHIFT),
				   &cq->kern_cq.qbuf_dma_addr, GFP_KERNEL);
	if (!cq->kern_cq.qbuf)
		return -ENOMEM;

	cq->kern_cq.db_record =
		(u64 *)(cq->kern_cq.qbuf + (cq->depth << CQE_SHIFT));

	init_cq_dbrec(cq);

	spin_lock_init(&cq->kern_cq.lock);
	/* use default cqdb addr */
	cq->kern_cq.db = dev->func_bar + ERDMA_BAR_CQDB_SPACE_OFFSET;

	return 0;
}

int erdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct ib_udata *udata)
{
	struct erdma_dev *dev = to_edev(ibcq->device);
	struct erdma_cq *cq = to_ecq(ibcq);
	unsigned int depth = attr->cqe;
	int ret;
	struct erdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);

	ERDMA_INC_CNT(dev, CMD_CREATE_CQ);

	if (depth > dev->attrs.max_cqe) {
		dev_warn(&dev->pdev->dev,
			 "WARN: exceed cqe(%d) > capbility(%d)\n", depth,
			 dev->attrs.max_cqe);
		ERDMA_INC_CNT(dev, CMD_CREATE_CQ_FAILED);
		return -EINVAL;
	}

	depth = roundup_pow_of_two(depth);
	cq->ibcq.cqe = depth;
	cq->depth = depth;
	cq->assoc_eqn = attr->comp_vector + 1;

	ret = xa_alloc_cyclic(&dev->cq_xa, &cq->cqn, cq,
			      XA_LIMIT(1, dev->attrs.max_cq - 1),
			      &dev->next_alloc_cqn, GFP_KERNEL);
	if (ret < 0) {
		ERDMA_INC_CNT(dev, CMD_CREATE_CQ_FAILED);
		return ret;
	}

	if (udata) {
		struct erdma_ureq_create_cq ureq;
		struct erdma_uresp_create_cq uresp;

		ret = ib_copy_from_udata(&ureq, udata,
					 min(udata->inlen, sizeof(ureq)));
		if (ret)
			goto err_out_xa;

		ret = erdma_init_user_cq(udata, uctx, cq, &ureq);
		if (ret)
			goto err_out_xa;

		uresp.cq_id = cq->cqn;
		uresp.num_cqe = depth;

		ret = ib_copy_to_udata(udata, &uresp,
				       min(sizeof(uresp), udata->outlen));
		if (ret)
			goto err_free_res;
	} else {
		ret = erdma_init_kernel_cq(cq);
		if (ret)
			goto err_out_xa;
	}

	ret = create_cq_cmd(dev, cq, udata ? true : false);
	if (ret)
		goto err_free_res;

	hrtimer_init(&cq->dim.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cq->dim.timer.function = cq_timer_fn;

	return 0;

err_free_res:
	if (udata) {
		erdma_unmap_user_dbrecords(uctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	} else {
		dma_free_coherent(&dev->pdev->dev,
				  WARPPED_BUFSIZE(depth << CQE_SHIFT),
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	}

err_out_xa:
	xa_erase(&dev->cq_xa, cq->cqn);
	ERDMA_INC_CNT(dev, CMD_CREATE_CQ_FAILED);
	return ret;
}

struct net_device *erdma_get_netdev(struct ib_device *device, port_t port_num)
{
	struct erdma_dev *edev = to_edev(device);

	read_lock(&edev->netdev_lock);
	if (edev->netdev)
		dev_hold(edev->netdev);
	read_unlock(&edev->netdev_lock);

	return edev->netdev;
}

void erdma_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

void erdma_set_mtu(struct erdma_dev *dev, u32 mtu)
{
	struct erdma_cmdq_config_mtu_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_CONF_MTU);
	req.mtu = mtu;

	erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

int erdma_set_retrans_num(struct erdma_dev *dev, u32 retrans_num)
{
	struct erdma_cmdq_set_retrans_num_req req;
	int ret;

	if (retrans_num == 0 || retrans_num > 0xffUL)
		return -EINVAL;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_SET_RETRANS_NUM);
	req.retrans_num = retrans_num;

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (!ret)
		dev->attrs.retrans_num = retrans_num;

	return ret;
}

void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason)
{
	struct ib_event event;

	event.device = &dev->ibdev;
	event.element.port_num = 1;
	event.event = reason;

	ib_dispatch_event(&event);
}

int erdma_query_hw_stats(struct erdma_dev *dev)
{
	struct erdma_cmdq_query_stats_resp *stats;
	struct erdma_cmdq_query_req req;
	dma_addr_t dma_addr;
	int err;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_GET_STATS);

	stats = dma_pool_alloc(dev->resp_pool, GFP_KERNEL | __GFP_ZERO,
			       &dma_addr);
	if (!stats)
		return -ENOMEM;

	req.target_addr = dma_addr;
	req.target_length = ERDMA_HW_RESP_SIZE;
	/* Clear the magic fileds. */
	stats->hdr.magic = 0;

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (err)
		goto out;

	if (stats->hdr.magic != 0x5566) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&dev->stats.value[ERDMA_STATS_TX_REQS_CNT], &stats->tx_req_cnt,
	       sizeof(__u64) * (ERDMA_STATS_RX_PPS_METER_DROP_CNT -
				ERDMA_STATS_TX_REQS_CNT + 1));

out:
	dma_pool_free(dev->resp_pool, stats, dma_addr);

	return err;
}

const struct cpumask *erdma_get_vector_affinity(struct ib_device *ibdev,
						int comp_vector)
{
	struct erdma_dev *dev = to_edev(ibdev);

	return &dev->ceqs[comp_vector].irq.affinity_hint_mask;
}
