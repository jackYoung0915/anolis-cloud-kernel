// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/dmar.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/x86_init.h>
#include <asm/iommu_table.h>

static bool disable_dac_quirk __read_mostly;

const struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;
#endif

int iommu_merge __read_mostly = 0;

int no_iommu __read_mostly;
/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

extern struct iommu_table_entry __iommu_table[], __iommu_table_end[];

void __init pci_iommu_alloc(void)
{
	struct iommu_table_entry *p;

	sort_iommu_table(__iommu_table, __iommu_table_end);
	check_iommu_entries(__iommu_table, __iommu_table_end);

	for (p = __iommu_table; p < __iommu_table_end; p++) {
		if (p && p->detect && p->detect() > 0) {
			p->flags |= IOMMU_DETECTED;
			if (p->early_init)
				p->early_init();
			if (p->flags & IOMMU_FINISH_IF_DETECTED)
				break;
		}
	}
}

/*
 * See <Documentation/x86/x86_64/boot-options.rst> for the iommu kernel
 * parameter documentation.
 */
static __init int iommu_setup(char *p)
{
	iommu_merge = 1;

	if (!p)
		return -EINVAL;

	while (*p) {
		if (!strncmp(p, "off", 3))
			no_iommu = 1;
		/* gart_parse_options has more force support */
		if (!strncmp(p, "force", 5))
			force_iommu = 1;
		if (!strncmp(p, "noforce", 7)) {
			iommu_merge = 0;
			force_iommu = 0;
		}

		if (!strncmp(p, "biomerge", 8)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "panic", 5))
			panic_on_overflow = 1;
		if (!strncmp(p, "nopanic", 7))
			panic_on_overflow = 0;
		if (!strncmp(p, "merge", 5)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "nomerge", 7))
			iommu_merge = 0;
		if (!strncmp(p, "forcesac", 8))
			pr_warn("forcesac option ignored.\n");
		if (!strncmp(p, "allowdac", 8))
			pr_warn("allowdac option ignored.\n");
		if (!strncmp(p, "nodac", 5))
			pr_warn("nodac option ignored.\n");
		if (!strncmp(p, "usedac", 6)) {
			disable_dac_quirk = true;
			return 1;
		}
#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft", 4))
			swiotlb = 1;
#endif
		if (!strncmp(p, "pt", 2))
			iommu_set_default_passthrough(true);
		if (!strncmp(p, "nopt", 4))
			iommu_set_default_translated(true);

		gart_parse_options(p);

		p += strcspn(p, ",");
		if (*p == ',')
			++p;
	}
	return 0;
}
early_param("iommu", iommu_setup);

static int __init pci_iommu_init(void)
{
	struct iommu_table_entry *p;

	x86_init.iommu.iommu_init();

	for (p = __iommu_table; p < __iommu_table_end; p++) {
		if (p && (p->flags & IOMMU_DETECTED) && p->late_init)
			p->late_init();
	}

	return 0;
}
/* Must execute after PCI subsystem */
rootfs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
#include <linux/intel-iommu.h>
/***
 * usage:
 *  set "zhaoxin_patch_bitmask=0|1" in cmdline
 * value description:
 *  bit 0: enable(1) node check or not(0). default 1
 */
unsigned long zhaoxin_patch_code = ZHAOXIN_PATCH_CODE_DEFAULT;
static int __init zhaoxin_patch_code_setup(char *str)
{
	int err = kstrtoul(str, 0, &zhaoxin_patch_code);

	if (err || zhaoxin_patch_code > ZHAOXIN_PATCH_CODE_MAX) {
		pr_err("cmdline 'zhaoxin_patch_bitmask=%s' inappropriate\n", str);
		zhaoxin_patch_code = ZHAOXIN_PATCH_CODE_DEFAULT;
		return err;
	}

	if (ZHAOXIN_P2CW_NODE_CHECK | zhaoxin_patch_code)
		pr_info("zhaoxin p2cw patch node check is enabled\n");

	return 0;
}
__setup("zhaoxin_patch_bitmask=", zhaoxin_patch_code_setup);

bool zhaoxin_kh40000;

bool is_zhaoxin_kh40000(void)
{
	return zhaoxin_kh40000;
}

static void quirk_zhaoxin_p2cw_patch(struct pci_dev *pci)
{
	if (pci->revision == 0x10) {
		zhaoxin_kh40000 = true;
		pr_debug("zhaoxin p2cw patch is enabled\n");
	}
}

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_ZHAOXIN, 0x1001, quirk_zhaoxin_p2cw_patch);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_ZHAOXIN, 0x345B, quirk_zhaoxin_p2cw_patch);

static struct pci_dev *get_pci_dev(struct device *dev)
{
	if (dev_is_pci(dev))
		return to_pci_dev(dev);

	if (dev->parent)
		return get_pci_dev(dev->parent);

	return NULL;
}

static int patch_do_basic_check(struct device *dev,
				enum dma_data_direction dir)
{
	u64 dma_mask = *dev->dma_mask;

	if (dir != DMA_FROM_DEVICE && dir != DMA_BIDIRECTIONAL)
		return false;

	if (dma_mask <= DMA_BIT_MASK(32))
		return false;

	return true;
}

static int patch_check_paddr(struct device *dev, phys_addr_t paddr,
			     bool is_iommu)
{
	unsigned long pfn;
	const struct iommu_ops *ops;
	struct dmar_domain *domain;

	if ((zhaoxin_patch_code & ZHAOXIN_P2CW_NODE_CHECK) == 0)
		return true;

	if (is_iommu) {
#ifdef CONFIG_INTEL_IOMMU
		ops = pci_bus_type.iommu_ops;
		if (ops && ops->iova_to_phys) {
			domain = find_domain(dev);
			paddr = ops->iova_to_phys(&domain->domain, paddr);
		}
#endif
	}

	pfn = PFN_DOWN(paddr);
	if (pfn_to_nid(pfn) != dev_to_node(dev))
		return true;

	return false;
}

static void patch_pci_posted_request_order(struct device *dev)
{
	u8 vid;
	struct pci_dev *pci;

	pci = get_pci_dev(dev);
	if (!pci)
		return;

	pci_read_config_byte(pci, PCI_VENDOR_ID, &vid);
}

void patch_p2cw_single_map(struct device *dev, dma_addr_t paddr,
			   enum dma_data_direction dir,
			   const struct dma_map_ops *ops)
{
	bool is_iommu = ops ? 1 : 0;

	if (patch_do_basic_check(dev, dir))
		if (patch_check_paddr(dev, paddr, is_iommu))
			patch_pci_posted_request_order(dev);
}

void patch_p2cw_sg_map(struct device *dev, struct scatterlist *sglist,
		       int nelems, enum dma_data_direction dir,
		       const struct dma_map_ops *ops)
{
	struct scatterlist *sg;
	int i;
	bool is_iommu = ops ? 1 : 0;

	if (patch_do_basic_check(dev, dir)) {
		for_each_sg(sglist, sg, nelems, i) {
			if (patch_check_paddr(dev, sg_dma_address(sg), is_iommu)) {
				patch_pci_posted_request_order(dev);
				break;
			}
		}
	}
}

/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static int via_no_dac_cb(struct pci_dev *pdev, void *data)
{
	pdev->dev.bus_dma_limit = DMA_BIT_MASK(32);
	return 0;
}

static void via_no_dac(struct pci_dev *dev)
{
	if (!disable_dac_quirk) {
		dev_info(&dev->dev, "disabling DAC on VIA PCI bridge\n");
		pci_walk_bus(dev->subordinate, via_no_dac_cb, NULL);
	}
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID,
				PCI_CLASS_BRIDGE_PCI, 8, via_no_dac);
#endif
