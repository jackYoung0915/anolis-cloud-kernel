// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author: Bennie Yan <bennie@nebula-matrix.com> bennie.yan
 */

#include "nbl_dev.h"

const char *const nbl_sysfs_qos_name[] = {
	/* rdma */
	"save",
	"tc2pri",
	"sq_pri_map",
	"raq_pri_map",
	"pri_imap",
	"pfc_imap",
	"db_to_csch_en",
	"sw_db_csch_th",
	"csch_qlen_th",
	"poll_wgt",
	"sp_wrr",
	"tc_wgt",

	"pfc",
	"pfc_buffer",
	"trust",
	"dscp2prio",
};

static ssize_t dscp2prio_show(struct nbl_sysfs_qos_info *qos_info, char *buf)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE - len, "dscp2prio mapping:\n");
	for (i = 0; i < NBL_DSCP_MAX; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "\tprio:%d dscp:%d,\n",
				qos_config->dscp2prio_map[i], i);

	return len;
}

static ssize_t dscp2prio_store(struct nbl_sysfs_qos_info *qos_info, const char *buf, size_t count)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);
	struct nbl_service_ops *serv_ops = NBL_DEV_MGT_TO_SERV_OPS(dev_mgt);
	struct nbl_common_info *common = NBL_DEV_MGT_TO_COMMON(dev_mgt);
	char cmd[8];
	int dscp, prio, ret;
	int i;

	ret = sscanf(buf, "%7[^,], %d , %d", cmd, &dscp, &prio);

	if (strncmp(cmd, "set", 3) == 0) {
		if (ret != 3 || dscp < 0 || dscp >= NBL_DSCP_MAX || prio < 0 || prio > 7)
			return -EINVAL;
		qos_config->dscp2prio_map[dscp] = prio;
	} else if (strncmp(cmd, "del", 3) == 0) {
		if (ret != 3 || dscp < 0 || dscp >= NBL_DSCP_MAX)
			return -EINVAL;
		if (qos_config->dscp2prio_map[dscp] == 0)
			return -EINVAL;
		qos_config->dscp2prio_map[dscp] = 0;
	} else if (strncmp(cmd, "flush", 5) == 0) {
		for (i = 0; i < NBL_DSCP_MAX; i++)
			qos_config->dscp2prio_map[i] = i / NBL_MAX_PFC_PRIORITIES;
	} else {
		return -EINVAL;
	}

	serv_ops->configure_qos(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt), NBL_COMMON_TO_ETH_ID(common),
				qos_config->pfc, qos_config->trust_mode, qos_config->dscp2prio_map);

	return count;
}

static ssize_t trust_mode_show(struct nbl_sysfs_qos_info *qos_info, char *buf)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 qos_config->trust_mode == NBL_TRUST_MODE_DSCP ? "dscp" : "802.1p");
}

static ssize_t trust_mode_store(struct nbl_sysfs_qos_info *qos_info, const char *buf, size_t count)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);
	struct nbl_service_ops *serv_ops = NBL_DEV_MGT_TO_SERV_OPS(dev_mgt);
	struct nbl_common_info *common = NBL_DEV_MGT_TO_COMMON(dev_mgt);
	u8 trust_mode;
	int ret;

	if (strncmp(buf, "dscp", 4) == 0) {
		trust_mode = NBL_TRUST_MODE_DSCP;
	} else if (strncmp(buf, "802.1p", 6) == 0) {
		trust_mode = NBL_TRUST_MODE_8021P;
	} else {
		netdev_err(net_dev->netdev, "Invalid trust mode: %s\n", buf);
			return -EINVAL;
	}

	if (qos_config->trust_mode == trust_mode)
		return count;

	ret = serv_ops->configure_qos(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt),
				      NBL_COMMON_TO_ETH_ID(common),
				      qos_config->pfc, trust_mode, qos_config->dscp2prio_map);
	if (ret) {
		netdev_err(net_dev->netdev, "configure_qos trust mode: %s failed\n", buf);
		return -EIO;
	}

	qos_config->trust_mode = trust_mode;

	netdev_info(net_dev->netdev, "Trust mode set to %s\n", buf);
	return count;
}

static ssize_t pfc_buffer_size_show(struct nbl_sysfs_qos_info *qos_info, char *buf)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	int prio;
	ssize_t count = 0;

	for (prio = 0; prio < NBL_MAX_PFC_PRIORITIES; prio++)
		count += snprintf(buf + count, PAGE_SIZE - count, "prio %d, xoff %d, xon %d\n",
				  prio, qos_config->buffer_sizes[prio][0],
				  qos_config->buffer_sizes[prio][1]);

	return count;
}

static ssize_t pfc_buffer_size_store(struct nbl_sysfs_qos_info *qos_info,
				     const char *buf, size_t count)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);
	struct nbl_service_ops *serv_ops = NBL_DEV_MGT_TO_SERV_OPS(dev_mgt);
	struct nbl_common_info *common = NBL_DEV_MGT_TO_COMMON(dev_mgt);
	int prio, xoff, xon;
	int ret;

	if (sscanf(buf, "%d,%d,%d", &prio, &xoff, &xon) != 3)
		return -EINVAL;

	if (prio < 0 || prio >= NBL_MAX_PFC_PRIORITIES)
		return -EINVAL;

	ret = serv_ops->set_pfc_buffer_size(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt),
					    NBL_COMMON_TO_ETH_ID(common), prio, xoff, xon);
	if (ret) {
		netdev_err(net_dev->netdev, "set_pfc_buffer_size failed\n");
		return ret;
	}
	qos_config->buffer_sizes[prio][0] = xoff;
	qos_config->buffer_sizes[prio][1] = xon;

	return count;
}

static ssize_t pfc_show(struct nbl_sysfs_qos_info *qos_info, char *buf)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;

	return scnprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d,%d,%d,%d\n",
			 qos_config->pfc[0], qos_config->pfc[1],
			 qos_config->pfc[2], qos_config->pfc[3],
			 qos_config->pfc[4], qos_config->pfc[5],
			 qos_config->pfc[6], qos_config->pfc[7]);
}

static ssize_t pfc_store(struct nbl_sysfs_qos_info *qos_info, const char *buf, size_t count)
{
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);
	struct nbl_service_ops *serv_ops = NBL_DEV_MGT_TO_SERV_OPS(dev_mgt);
	struct nbl_common_info *common = NBL_DEV_MGT_TO_COMMON(dev_mgt);
	u8 pfc_config[NBL_MAX_PFC_PRIORITIES];
	int ret, i;
	ssize_t len = count;
	bool changed = false;

	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' '))
		len--;

	if (len == 0) {
		netdev_err(net_dev->netdev, "Invalid input: no data to parse.\n");
		return count;
	}

	if (len != 15) {
		netdev_err(net_dev->netdev, "Invalid input length %ld.\n", len);
		return -EINVAL;
	}

	ret = sscanf(buf, "%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd",
		     &pfc_config[0], &pfc_config[1], &pfc_config[2], &pfc_config[3],
		     &pfc_config[4], &pfc_config[5], &pfc_config[6], &pfc_config[7]);

	if (ret != NBL_MAX_PFC_PRIORITIES) {
		netdev_err(net_dev->netdev, "Failed to parse PFC. Expected 8 got %d\n", ret);
		return -EINVAL;
	}

	for (i = 0; i < NBL_MAX_PFC_PRIORITIES; i++) {
		if (pfc_config[i] != qos_config->pfc[i]) {
			changed = true;
			break;
		}
	}

	if (!changed)
		return count;

	netdev_info(net_dev->netdev, "Parsed PFC configuration: %u %u %u %u %u %u %u %u\n",
		    pfc_config[0], pfc_config[1], pfc_config[2], pfc_config[3],
		    pfc_config[4], pfc_config[5], pfc_config[6], pfc_config[7]);

	for (i = 0; i < NBL_MAX_PFC_PRIORITIES; i++)
		if (pfc_config[i] > 1)
			return -EINVAL;

	ret = serv_ops->configure_qos(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt),
				      NBL_COMMON_TO_ETH_ID(common), pfc_config,
				      qos_config->trust_mode, qos_config->dscp2prio_map);
	if (ret) {
		netdev_err(net_dev->netdev, "configure_qos trust mode: %s failed\n", buf);
		return -EIO;
	}

	for (i = 0; i < NBL_MAX_PFC_PRIORITIES; i++)
		qos_config->pfc[i] = pfc_config[i];

	return count;
}

static ssize_t nbl_qos_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct nbl_sysfs_qos_info *qos_info =
					container_of(attr, struct nbl_sysfs_qos_info, kobj_attr);
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);

	switch (qos_info->offset) {
	case NBL_QOS_PFC:
		return pfc_show(qos_info, buf);
	case NBL_QOS_TRUST:
		return trust_mode_show(qos_info, buf);
	case NBL_QOS_DSCP2PRIO:
		return dscp2prio_show(qos_info, buf);
	case NBL_QOS_PFC_BUFFER:
		return pfc_buffer_size_show(qos_info, buf);
	case NBL_QOS_RDMA_SAVE:
	case NBL_QOS_RDMA_TC2PRI:
	case NBL_QOS_RDMA_SQ_PRI_MAP:
	case NBL_QOS_RDMA_RAQ_PRI_MAP:
	case NBL_QOS_RDMA_PRI_IMAP:
	case NBL_QOS_RDMA_PFC_IMAP:
	case NBL_QOS_RDMA_DB_TO_CSCH_EN:
	case NBL_QOS_RDMA_SW_DB_CSCH_TH:
	case NBL_QOS_RDMA_CSCH_QLEN_TH:
	case NBL_QOS_RDMA_POLL_WGT:
	case NBL_QOS_RDMA_SP_WRR:
	case NBL_QOS_RDMA_TC_WGT:
		return nbl_dev_rdma_qos_cfg_show(dev_mgt, qos_info->offset, buf);
	default:
		return -EINVAL;
	}
}

static ssize_t nbl_qos_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	struct nbl_sysfs_qos_info *qos_info =
					container_of(attr, struct nbl_sysfs_qos_info, kobj_attr);
	struct nbl_dev_net *net_dev = qos_info->net_dev;
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);

	switch (qos_info->offset) {
	case NBL_QOS_PFC:
		return pfc_store(qos_info, buf, count);
	case NBL_QOS_TRUST:
		return trust_mode_store(qos_info, buf, count);
	case NBL_QOS_DSCP2PRIO:
		return dscp2prio_store(qos_info, buf, count);
	case NBL_QOS_PFC_BUFFER:
		return pfc_buffer_size_store(qos_info, buf, count);
	case NBL_QOS_RDMA_SAVE:
	case NBL_QOS_RDMA_TC2PRI:
	case NBL_QOS_RDMA_SQ_PRI_MAP:
	case NBL_QOS_RDMA_RAQ_PRI_MAP:
	case NBL_QOS_RDMA_PRI_IMAP:
	case NBL_QOS_RDMA_PFC_IMAP:
	case NBL_QOS_RDMA_DB_TO_CSCH_EN:
	case NBL_QOS_RDMA_SW_DB_CSCH_TH:
	case NBL_QOS_RDMA_CSCH_QLEN_TH:
	case NBL_QOS_RDMA_POLL_WGT:
	case NBL_QOS_RDMA_SP_WRR:
	case NBL_QOS_RDMA_TC_WGT:
		return nbl_dev_rdma_qos_cfg_store(dev_mgt, qos_info->offset, buf, count);
	default:
		return -EINVAL;
	}
}

static void nbl_init_qos_config(struct nbl_dev_net *net_dev)
{
	struct nbl_netdev_priv *net_priv = netdev_priv(net_dev->netdev);
	struct nbl_adapter *adapter = net_priv->adapter;
	struct nbl_dev_mgt *dev_mgt = NBL_ADAPTER_TO_DEV_MGT(adapter);
	struct nbl_service_ops *serv_ops = NBL_DEV_MGT_TO_SERV_OPS(dev_mgt);
	struct nbl_common_info *common = NBL_DEV_MGT_TO_COMMON(dev_mgt);
	struct nbl_net_qos *qos_config = &net_dev->qos_config;
	int i;

	for (i = 0; i < NBL_DSCP_MAX; i++)
		qos_config->dscp2prio_map[i] = i / NBL_MAX_PFC_PRIORITIES;

	for (i = 0; i < NBL_MAX_PFC_PRIORITIES; i++)
		serv_ops->get_pfc_buffer_size(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt),
					      NBL_COMMON_TO_ETH_ID(common), i,
					      &qos_config->buffer_sizes[i][0],
					      &qos_config->buffer_sizes[i][1]);

	serv_ops->configure_qos(NBL_DEV_MGT_TO_SERV_PRIV(dev_mgt), NBL_COMMON_TO_ETH_ID(common),
				qos_config->pfc, qos_config->trust_mode, qos_config->dscp2prio_map);
}

int nbl_netdev_add_sysfs(struct net_device *netdev, struct nbl_dev_net *net_dev)
{
	int ret;
	int i;

	nbl_init_qos_config(net_dev);
	net_dev->qos_config.qos_kobj = kobject_create_and_add("qos", &netdev->dev.kobj);
	if (!net_dev->qos_config.qos_kobj)
		return -ENOMEM;

	for (i = 0; i < NBL_QOS_TYPE_MAX; i++) {
		net_dev->qos_config.qos_info[i].net_dev = net_dev;
		net_dev->qos_config.qos_info[i].offset = i;
		/* create qos sysfs */
		sysfs_attr_init(&net_dev->qos_config.qos_info[i].kobj_attr.attr);
		net_dev->qos_config.qos_info[i].kobj_attr.attr.name = nbl_sysfs_qos_name[i];
		net_dev->qos_config.qos_info[i].kobj_attr.attr.mode = 0644;
		net_dev->qos_config.qos_info[i].kobj_attr.show = nbl_qos_show;
		net_dev->qos_config.qos_info[i].kobj_attr.store = nbl_qos_store;
		ret = sysfs_create_file(net_dev->qos_config.qos_kobj,
					&net_dev->qos_config.qos_info[i].kobj_attr.attr);
		if (ret)
			netdev_err(netdev, "Failed to create %s sysfs file\n",
				   nbl_sysfs_qos_name[i]);
	}

	return 0;
}

void nbl_netdev_remove_sysfs(struct nbl_dev_net *net_dev)
{
	int i;

	if (!net_dev->qos_config.qos_kobj)
		return;

	for (i = 0; i < NBL_QOS_TYPE_MAX; i++)
		sysfs_remove_file(net_dev->qos_config.qos_kobj,
				  &net_dev->qos_config.qos_info[i].kobj_attr.attr);

	kobject_put(net_dev->qos_config.qos_kobj);
}
