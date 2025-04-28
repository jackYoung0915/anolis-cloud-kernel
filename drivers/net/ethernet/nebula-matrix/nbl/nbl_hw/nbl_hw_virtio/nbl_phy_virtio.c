// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 nebula-matrix Limited.
 * Author:
 */

#include "nbl_phy_virtio.h"

static void nbl_phy_get_common_cfg(void *priv, u32 offset, void *buf, u32 len)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;
	struct device *dev = NBL_PHY_MGT_TO_DEV(&phy_mgt_virtio->phy_mgt);
	void __iomem *comm = phy_mgt_virtio->comm;
	u8 b;
	u16 w;
	u32 l;

	if (offset + len > phy_mgt_virtio->common_len) {
		dev_warn(dev, "get common cfg overstep the bound %u > %lu.\n",
			 offset + len, phy_mgt_virtio->common_len);
		return;
	}

	switch (len) {
	case 1:
		b = ioread8(comm + offset);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = ioread16(comm + offset);
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = ioread32(comm + offset);
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = ioread32(comm + offset);
		memcpy(buf, &l, sizeof(l));
		l = ioread32(comm + offset + sizeof(l));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		break;
	}
}

static void nbl_phy_set_common_cfg(void *priv, u32 offset, void *buf, u32 len)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;
	struct device *dev = NBL_PHY_MGT_TO_DEV(&phy_mgt_virtio->phy_mgt);
	void __iomem *comm = phy_mgt_virtio->comm;
	u8 b;
	u16 w;
	u32 l;

	if (offset + len > phy_mgt_virtio->common_len) {
		dev_warn(dev, "set common cfg overstep the bound %u > %lu.\n",
			 offset + len, phy_mgt_virtio->common_len);
		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		iowrite8(b, comm + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		iowrite16(w, comm + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		iowrite32(l, comm + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		iowrite32(l, comm + offset);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		iowrite32(l, comm + offset + sizeof(l));
		break;
	default:
		break;
	}
}

static void nbl_phy_get_device_cfg(void *priv, u32 offset, void *buf, u32 len)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;
	struct device *dev = NBL_PHY_MGT_TO_DEV(&phy_mgt_virtio->phy_mgt);
	void __iomem *device = phy_mgt_virtio->device;
	u8 b;
	u16 w;
	u32 l;

	if (offset + len > phy_mgt_virtio->device_len) {
		dev_warn(dev, "get device cfg overstep the bound %u > %lu.\n",
			 offset + len, phy_mgt_virtio->device_len);
		return;
	}

	switch (len) {
	case 1:
		b = ioread8(device + offset);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = ioread16(device + offset);
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = ioread32(device + offset);
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = ioread32(device + offset);
		memcpy(buf, &l, sizeof(l));
		l = ioread32(device + offset + sizeof(l));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		break;
	}
}

static void nbl_phy_set_device_cfg(void *priv, u32 offset, void *buf, u32 len)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;
	struct device *dev = NBL_PHY_MGT_TO_DEV(&phy_mgt_virtio->phy_mgt);
	void __iomem *device = phy_mgt_virtio->device;
	u8 b;
	u16 w;
	u32 l;

	if (offset + len > phy_mgt_virtio->device_len) {
		dev_warn(dev, "set device cfg overstep the bound %u > %lu.\n",
			 offset + len, phy_mgt_virtio->device_len);
		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		iowrite8(b, device + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		iowrite16(w, device + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		iowrite32(l, device + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		iowrite32(l, device + offset);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		iowrite32(l, device + offset + sizeof(l));
		break;
	default:
		break;
	}
}

static bool nbl_phy_get_rdma_capability(void *priv)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;

	if (phy_mgt_virtio->rdma)
		return true;
	return false;
}

static void nbl_phy_update_tail_ptr(void *priv, struct nbl_notify_param *param)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;
	u8 __iomem *notify_addr = phy_mgt_virtio->notify_base;
	u32 local_qid = param->notify_qid;
	u32 tail_ptr = param->tail_ptr;

	writel((((u32)tail_ptr << 16) | (u32)local_qid), notify_addr);
}

static u8 *nbl_phy_get_tail_ptr(void *priv)
{
	struct nbl_phy_mgt_virtio *phy_mgt_virtio = (struct nbl_phy_mgt_virtio *)priv;

	return phy_mgt_virtio->notify_base;
}

static struct nbl_phy_ops phy_ops_virtio = {
	.get_common_cfg			= nbl_phy_get_common_cfg,
	.set_common_cfg			= nbl_phy_set_common_cfg,
	.get_device_cfg			= nbl_phy_get_device_cfg,
	.set_device_cfg			= nbl_phy_set_device_cfg,
	.get_rdma_capability		= nbl_phy_get_rdma_capability,
	.update_tail_ptr		= nbl_phy_update_tail_ptr,
	.get_tail_ptr			= nbl_phy_get_tail_ptr,
};

/* Structure starts here, adding an op should not modify anything below */
static int nbl_phy_setup_phy_mgt(struct nbl_common_info *common,
				 struct nbl_phy_mgt_virtio **phy_mgt_virtio)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	*phy_mgt_virtio = devm_kzalloc(dev, sizeof(struct nbl_phy_mgt_virtio), GFP_KERNEL);
	if (!*phy_mgt_virtio)
		return -ENOMEM;

	NBL_PHY_MGT_TO_COMMON(&(*phy_mgt_virtio)->phy_mgt) = common;

	return 0;
}

static void nbl_phy_remove_phy_mgt(struct nbl_common_info *common,
				   struct nbl_phy_mgt_virtio **phy_mgt_virtio)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, *phy_mgt_virtio);
	*phy_mgt_virtio = NULL;
}

static int nbl_phy_setup_ops(struct nbl_common_info *common, struct nbl_phy_ops_tbl **phy_ops_tbl,
			     struct nbl_phy_mgt_virtio *phy_mgt_virtio)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	*phy_ops_tbl = devm_kzalloc(dev, sizeof(struct nbl_phy_ops_tbl), GFP_KERNEL);
	if (!*phy_ops_tbl)
		return -ENOMEM;

	NBL_PHY_OPS_TBL_TO_OPS(*phy_ops_tbl) = &phy_ops_virtio;
	NBL_PHY_OPS_TBL_TO_PRIV(*phy_ops_tbl) = phy_mgt_virtio;

	return 0;
}

static void nbl_phy_remove_ops(struct nbl_common_info *common, struct nbl_phy_ops_tbl **phy_ops_tbl)
{
	struct device *dev;

	dev = NBL_COMMON_TO_DEV(common);
	devm_kfree(dev, *phy_ops_tbl);
	*phy_ops_tbl = NULL;
}

static int nbl_phy_pci_find_capability(struct pci_dev *pdev, u8 cfg_type,
				       u32 ioresource_types, int *bar_mask)
{
	int pos;
	u8 type, bar;

	for (pos = pci_find_capability(pdev, PCI_CAP_ID_VNDR);
	     pos > 0;
	     pos = pci_find_next_capability(pdev, pos, PCI_CAP_ID_VNDR)) {
		pci_read_config_byte(pdev, pos + offsetof(struct nbl_pci_cap, cfg_type), &type);
		pci_read_config_byte(pdev, pos + offsetof(struct nbl_pci_cap, bar), &bar);

		if (bar > 0x5)
			continue;

		if (type == cfg_type) {
			if (pci_resource_len(pdev, bar) &&
			    pci_resource_flags(pdev, bar) & ioresource_types) {
				*bar_mask |= (1 << bar);
				return pos;
			}
		}
	}

	return 0;
}

static void nbl_phy_pci_find_capabilities(struct pci_dev *pdev, int *bar_mask,
					  struct nbl_phy_init_addr_tbl *init_addr_tbl)
{
	int comm, notify, isr, device, rdma;

	comm = nbl_phy_pci_find_capability(pdev, NBL_PCI_CAP_COMMON_CFG,
					   IORESOURCE_IO | IORESOURCE_MEM, bar_mask);

	notify = nbl_phy_pci_find_capability(pdev, NBL_PCI_CAP_NOTIFY_CFG,
					     IORESOURCE_IO | IORESOURCE_MEM, bar_mask);

	isr = nbl_phy_pci_find_capability(pdev, NBL_PCI_CAP_ISR_CFG,
					  IORESOURCE_IO | IORESOURCE_MEM, bar_mask);

	if (!comm || !notify || !isr)
		dev_err(&pdev->dev, "miss capabilities %i/%i/%i\n", comm, notify, isr);

	device = nbl_phy_pci_find_capability(pdev, NBL_PCI_CAP_DEVICE_CFG,
					     IORESOURCE_IO | IORESOURCE_MEM, bar_mask);

	rdma = nbl_phy_pci_find_capability(pdev, NBL_PCI_CAP_RDMA_CFG,
					   IORESOURCE_IO | IORESOURCE_MEM, bar_mask);

	init_addr_tbl->comm = comm;
	init_addr_tbl->isr = isr;
	init_addr_tbl->notify = notify;
	init_addr_tbl->device = device;
	init_addr_tbl->rdma = rdma;
}

static void __iomem *nbl_phy_pci_map_capability(struct pci_dev *pdev, int off, size_t *len)
{
	u8 bar;
	u32 offset, length;
	void __iomem *p;

	pci_read_config_byte(pdev, off + offsetof(struct nbl_pci_cap, bar), &bar);
	pci_read_config_dword(pdev, off + offsetof(struct nbl_pci_cap, offset), &offset);
	pci_read_config_dword(pdev, off + offsetof(struct nbl_pci_cap, length), &length);

	p = pci_iomap_range(pdev, bar, offset, length);
	if (!p)
		dev_err(&pdev->dev, "unable to map %u@%u on bar %i\n", length, offset, bar);
	else if (len)
		*len = length;

	return p;
}

static int nbl_phy_pci_map_capabilities(struct pci_dev *pdev,
					struct nbl_phy_mgt_virtio *phy_mgt_virtio,
					struct nbl_phy_init_addr_tbl *init_addr_tbl)
{
	phy_mgt_virtio->comm = nbl_phy_pci_map_capability(pdev, init_addr_tbl->comm,
							  &phy_mgt_virtio->common_len);
	if (!phy_mgt_virtio->comm)
		goto err_map_comm;

	phy_mgt_virtio->notify_base = nbl_phy_pci_map_capability(pdev, init_addr_tbl->notify, NULL);
	if (!phy_mgt_virtio->notify_base)
		goto err_map_notify;

	phy_mgt_virtio->isr = nbl_phy_pci_map_capability(pdev, init_addr_tbl->isr, NULL);
	if (!phy_mgt_virtio->isr)
		goto err_map_isr;

	if (init_addr_tbl->device) {
		phy_mgt_virtio->device = nbl_phy_pci_map_capability(pdev, init_addr_tbl->device,
								    &phy_mgt_virtio->device_len);
		if (!phy_mgt_virtio->device)
			goto err_map_device;
	}

	if (init_addr_tbl->rdma) {
		phy_mgt_virtio->rdma = nbl_phy_pci_map_capability(pdev, init_addr_tbl->rdma, NULL);
		if (!phy_mgt_virtio->rdma)
			goto err_map_rdma;
	}

	return 0;

err_map_rdma:
	if (phy_mgt_virtio->device)
		pci_iounmap(pdev, phy_mgt_virtio->device);
err_map_device:
	pci_iounmap(pdev, phy_mgt_virtio->isr);
err_map_isr:
	pci_iounmap(pdev, phy_mgt_virtio->notify_base);
err_map_notify:
	pci_iounmap(pdev, phy_mgt_virtio->comm);
err_map_comm:
	return -EINVAL;
}

static void nbl_phy_unmap_capabilities(struct pci_dev *pdev,
				       struct nbl_phy_mgt_virtio *phy_mgt_virtio)
{
	if (phy_mgt_virtio->rdma)
		pci_iounmap(pdev, phy_mgt_virtio->rdma);

	if (phy_mgt_virtio->device)
		pci_iounmap(pdev, phy_mgt_virtio->device);

	pci_iounmap(pdev, phy_mgt_virtio->isr);
	pci_iounmap(pdev, phy_mgt_virtio->notify_base);
	pci_iounmap(pdev, phy_mgt_virtio->comm);
}

int nbl_phy_init_virtio(void *p, struct nbl_init_param *param)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct nbl_common_info *common;
	struct pci_dev *pdev;
	struct nbl_phy_mgt_virtio **phy_mgt_virtio;
	struct nbl_phy_mgt *phy_mgt;
	struct nbl_phy_ops_tbl **phy_ops_tbl;
	int bar_mask = 0;
	struct nbl_phy_init_addr_tbl init_addr_tbl;
	int ret = 0;

	common = NBL_ADAPTER_TO_COMMON(adapter);
	phy_mgt_virtio = (struct nbl_phy_mgt_virtio **)&NBL_ADAPTER_TO_PHY_MGT(adapter);
	phy_ops_tbl = &NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);
	pdev = NBL_COMMON_TO_PDEV(common);

	ret = nbl_phy_setup_phy_mgt(common, phy_mgt_virtio);
	if (ret)
		goto setup_mgt_fail;

	phy_mgt = &(*phy_mgt_virtio)->phy_mgt;

	nbl_phy_pci_find_capabilities(pdev, &bar_mask, &init_addr_tbl);

	ret = pci_request_selected_regions(pdev, bar_mask, NBL_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Request memory bar failed, err = %d\n", ret);
		goto request_bar_region_fail;
	}

	ret = nbl_phy_pci_map_capabilities(pdev, *phy_mgt_virtio, &init_addr_tbl);
	if (ret) {
		dev_err(&pdev->dev, "map capabilities failed, err = %d\n", ret);
		goto map_capabilities_fail;
	}

	ret = nbl_phy_setup_ops(common, phy_ops_tbl, *phy_mgt_virtio);
	if (ret)
		goto setup_ops_fail;

	return 0;

setup_ops_fail:
	nbl_phy_unmap_capabilities(pdev, *phy_mgt_virtio);
map_capabilities_fail:
	pci_release_selected_regions(pdev, bar_mask);
request_bar_region_fail:
	nbl_phy_remove_phy_mgt(common, phy_mgt_virtio);
setup_mgt_fail:
	return ret;
}

void nbl_phy_remove_virtio(void *p)
{
	struct nbl_adapter *adapter = (struct nbl_adapter *)p;
	struct nbl_common_info *common;
	struct nbl_phy_mgt_virtio **phy_mgt_virtio;
	struct nbl_phy_ops_tbl **phy_ops_tbl;
	struct pci_dev *pdev;
	int bar_mask = BIT(NBL_MEMORY_BAR);

	common = NBL_ADAPTER_TO_COMMON(adapter);
	phy_mgt_virtio = (struct nbl_phy_mgt_virtio **)&NBL_ADAPTER_TO_PHY_MGT(adapter);
	phy_ops_tbl = &NBL_ADAPTER_TO_PHY_OPS_TBL(adapter);
	pdev = NBL_COMMON_TO_PDEV(common);

	nbl_phy_unmap_capabilities(pdev, *phy_mgt_virtio);
	pci_release_selected_regions(pdev, bar_mask);
	nbl_phy_remove_phy_mgt(common, phy_mgt_virtio);
	nbl_phy_remove_ops(common, phy_ops_tbl);
}
