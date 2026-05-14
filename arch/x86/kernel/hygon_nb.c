// SPDX-License-Identifier: GPL-2.0-only
/*
 * Share support code for Hygon northbridges and derivatives.
 * Copyright (C) 2026 Chengdu Haiguang IC Design Co., Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <asm/hygon/hygon_nb.h>

#define PCI_DEVICE_ID_HYGON_18H_ROOT		0x1450

#define PCI_DEVICE_ID_HYGON_18H_DF_F4		0x1464

static struct pci_dev **hygon_roots;
static struct hygon_northbridge_info hygon_northbridges;

static const struct pci_device_id hygon_root_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HYGON, PCI_DEVICE_ID_HYGON_18H_ROOT) },
	{}
};

static const struct pci_device_id hygon_nb_misc_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HYGON, PCI_DEVICE_ID_HYGON_18H_DF_F3) },
	{}
};

static const struct pci_device_id hygon_nb_link_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HYGON, PCI_DEVICE_ID_HYGON_18H_DF_F4) },
	{}
};

static struct pci_dev *next_northbridge(struct pci_dev *dev,
					const struct pci_device_id *ids)
{
	do {
		dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev);
		if (!dev)
			break;
	} while (!pci_match_id(ids, dev));
	return dev;
}

u16 hygon_nb_num(void)
{
	return hygon_northbridges.num;
}
EXPORT_SYMBOL_GPL(hygon_nb_num);

struct hygon_northbridge *node_to_hygon_nb(int node)
{
	return (node < hygon_northbridges.num) ? &hygon_northbridges.nb[node] : NULL;
}
EXPORT_SYMBOL_GPL(node_to_hygon_nb);

int northbridge_init_hygon(void)
{
	const struct pci_device_id *misc_ids = hygon_nb_misc_ids;
	const struct pci_device_id *link_ids = hygon_nb_link_ids;
	const struct pci_device_id *root_ids = hygon_root_ids;
	struct pci_dev *root, *misc, *link;
	struct hygon_northbridge *nb;
	u16 misc_count = 0;
	int err = -ENODEV;
	u16 i = 0;

	if (hygon_northbridges.num)
		return 0;

	misc = NULL;
	while ((misc = next_northbridge(misc, misc_ids)))
		misc_count++;

	if (!misc_count) {
		err = -ENODEV;
		goto out;
	}

	hygon_roots = kcalloc(misc_count, sizeof(*hygon_roots), GFP_KERNEL);
	if (!hygon_roots) {
		err = -ENOMEM;
		goto out;
	}

	nb = kcalloc(misc_count, sizeof(struct hygon_northbridge), GFP_KERNEL);
	if (!nb) {
		err = -ENOMEM;
		goto err_free_roots;
	}

	hygon_northbridges.nb = nb;
	hygon_northbridges.num = misc_count;

	link = misc = root = NULL;
	for (i = 0; i < hygon_northbridges.num; i++) {
		hygon_roots[i] = root =
			next_northbridge(root, root_ids);
		node_to_hygon_nb(i)->misc = misc =
			next_northbridge(misc, misc_ids);
		node_to_hygon_nb(i)->link = link =
			next_northbridge(link, link_ids);
	}

	pr_info("Hygon Fam%xh Model%xh NB driver init success.\n",
		boot_cpu_data.x86, boot_cpu_data.x86_model);

	return 0;

err_free_roots:
	kfree(hygon_roots);

out:
	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		pr_err("Hygon Fam%xh Model%xh northbridge init failed(%d)!\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model, err);
	return err;
}

static __init int init_hygon_nbs(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return 0;

	northbridge_init_hygon();

	return 0;
}

/* This has to go after the PCI subsystem */
fs_initcall(init_hygon_nbs);
