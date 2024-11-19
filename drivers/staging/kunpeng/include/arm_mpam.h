/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ARM_MPAM_H
#define __LINUX_ARM_MPAM_H

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/cpumask.h>
#include <linux/types.h>

/*  */
/*
 * MPAM - Memory Partitioning and Monitoring table
 *
 * Conforms to "MPAM ACPI Description 1.0",
 * Null 0, 2017. Copyright 2017 ARM Limited or its affiliates.
 *
 ******************************************************************************/
#define ACPI_SIG_MPAM           "MPAM"	/* Memory Partitioning and Monitoring Table */

#pragma pack(1)

/*
 * The reason for comment this struct is that the struct has been defined in actbl3.h
 * and there is no caller to this struct, so we annotate it here.
 */

//struct acpi_table_mpam {
//	struct acpi_table_header	header;/* Common ACPI table header */
//};


/* Subtable header for MPAM */

struct acpi_mpam_header {
	u8			type;
	u16			length;
	u8			reserved;
	u64			base_address;
	u32			overflow_interrupt;
	u32			overflow_flags;
	u32			error_interrupt;
	u32			error_interrupt_flags;
	u32			not_ready_max;
	u32			offset;
};

/* Values for subtable type in ACPI_MPAM_NODE_HEADER */

enum AcpiMpamType {
	ACPI_MPAM_TYPE_SMMU		= 0,
	ACPI_MPAM_TYPE_CACHE		= 1,
	ACPI_MPAM_TYPE_MEMORY		= 2,
	ACPI_MPAM_TYPE_UNKNOWN		= 3
};

/* Flags */
#define ACPI_MPAM_IRQ_FLAGS    (1)     /* Interrupt mode */

/*
 *  MPAM Subtables
 */
struct acpi_mpam_node_smmu {
	struct acpi_mpam_header	header;
	u32			IORT_ref;
};

struct acpi_mpam_node_cache {
	struct acpi_mpam_header	header;
	u32			PPTT_ref;
};

struct acpi_mpam_node_memory {
	struct acpi_mpam_header	header;
	u8			proximity_domain;
	u8			reserved1[3];
};

/*******************************************************************************
 *
 * MPAM - Memory System Resource Partitioning and Monitoring
 *
 * Conforms to "ACPI for Memory System Resource Partitioning and Monitoring 2.0"
 * Document number: ARM DEN 0065, December, 2022.
 *
 ******************************************************************************/

/* MPAM RIS locator types. Table 11, Location types */
enum acpi_mpam_locator_type {
//	ACPI_MPAM_LOCATION_TYPE_PROCESSOR_CACHE = 0,
//	ACPI_MPAM_LOCATION_TYPE_MEMORY = 1,
//	ACPI_MPAM_LOCATION_TYPE_SMMU = 2,
//	ACPI_MPAM_LOCATION_TYPE_MEMORY_CACHE = 3,
//	ACPI_MPAM_LOCATION_TYPE_ACPI_DEVICE = 4,
	ACPI_MPAM_LOCATION_TYPE_INTERCONNECT = 5,
	ACPI_MPAM_LOCATION_TYPE_UNKNOWN = 0xFF
};

/* MPAM Functional dependency descriptor. Table 10 */
struct acpi_mpam_func_deps {
	u32 producer;
	u32 reserved;
};

/* MPAM Processor cache locator descriptor. Table 13 */
struct acpi_mpam_resource_cache_locator {
	u64 cache_reference;
	u32 reserved;
};

/* MPAM Memory locator descriptor. Table 14 */
struct acpi_mpam_resource_memory_locator {
	u64 proximity_domain;
	u32 reserved;
};

/* MPAM SMMU locator descriptor. Table 15 */
struct acpi_mpam_resource_smmu_locator {
	u64 smmu_interface;
	u32 reserved;
};

/* MPAM Memory-side cache locator descriptor. Table 16 */
struct acpi_mpam_resource_memcache_locator {
	u8 reserved[7];
	u8 level;
	u32 reference;
};

/* MPAM ACPI device locator descriptor. Table 17 */
struct acpi_mpam_resource_acpi_locator {
	u64 acpi_hw_id;
	u32 acpi_unique_id;
};

/* MPAM Interconnect locator descriptor. Table 18 */
struct acpi_mpam_resource_interconnect_locator {
	u64 inter_connect_desc_tbl_off;
	u32 reserved;
};

/* MPAM Locator structure. Table 12 */
struct acpi_mpam_resource_generic_locator {
	u64 descriptor1;
	u32 descriptor2;
};

union acpi_mpam_resource_locator {
	struct acpi_mpam_resource_cache_locator cache_locator;
	struct acpi_mpam_resource_memory_locator memory_locator;
	struct acpi_mpam_resource_smmu_locator smmu_locator;
	struct acpi_mpam_resource_memcache_locator mem_cache_locator;
	struct acpi_mpam_resource_acpi_locator acpi_locator;
	struct acpi_mpam_resource_interconnect_locator interconnect_ifc_locator;
	struct acpi_mpam_resource_generic_locator generic_locator;
};

/* Memory System Component Resource Node Structure Table 9 */
struct acpi_mpam_resource_node {
	u32 identifier;
	u8 ris_index;
	u16 reserved1;
	u8 locator_type;
	union acpi_mpam_resource_locator locator;
	u32 num_functional_deps;
};

/* Memory System Component (MSC) Node Structure. Table 4 */
struct acpi_mpam_msc_node {
	u16 length;
	u8 interface_type;
	u8 reserved;
	u32 identifier;
	u64 base_address;
	u32 mmio_size;
	u32 overflow_interrupt;
	u32 overflow_interrupt_flags;
	u32 reserved1;
	u32 overflow_interrupt_affinity;
	u32 error_interrupt;
	u32 error_interrupt_flags;
	u32 reserved2;
	u32 error_interrupt_affinity;
	u32 max_nrdy_usec;
	u64 hardware_id_linked_device;
	u32 instance_id_linked_device;
	u32 num_resouce_nodes;
};
#pragma pack()
/* End of Kunpeng MPAM ACPI Definitions  */
struct mpam_device;

enum mpam_class_types {
	MPAM_CLASS_SMMU,
	MPAM_CLASS_CACHE,   /* Well known caches, e.g. L2 */
	MPAM_CLASS_MEMORY,  /* Main memory */
	MPAM_CLASS_UNKNOWN, /* Everything else, e.g. TLBs etc */
};

struct mpam_device *
__mpam_device_create(u8 level_idx, enum mpam_class_types type,
			int component_id, const struct cpumask *fw_affinity,
			phys_addr_t hwpage_address);

/*
 * Create a device for a well known cache, e.g. L2.
 * @level_idx and @cache_id will be used to match the cache via cacheinfo
 * to learn the component affinity and export domain/resources via resctrl.
 * If the device can only be accessed from a smaller set of CPUs, provide
 * this as @device_affinity, which can otherwise be NULL.
 *
 * Returns the new device, or an ERR_PTR().
 */
static inline struct mpam_device * __init
mpam_device_create_cache(u8 level_idx, int cache_id,
			const struct cpumask *device_affinity,
			phys_addr_t hwpage_address)
{
	return __mpam_device_create(level_idx, MPAM_CLASS_CACHE, cache_id,
			device_affinity, hwpage_address);
}
/*
 * Create a device for a main memory.
 * For NUMA systems @nid allows multiple components to be created,
 * which will be exported as resctrl domains. MSCs for memory must
 * be accessible from any cpu.
 */
static inline struct mpam_device *
mpam_device_create_memory(int nid, phys_addr_t hwpage_address)
{
	struct cpumask dev_affinity;

	cpumask_copy(&dev_affinity, cpumask_of_node(nid));

	return __mpam_device_create(~0, MPAM_CLASS_MEMORY, nid,
			&dev_affinity, hwpage_address);
}
int mpam_discovery_start(void);
int mpam_discovery_complete(void);
void mpam_discovery_failed(void);

enum mpam_enable_type {
	MPAM_ENABLE_DENIED = 0,
	MPAM_ENABLE_ACPI,
	MPAM_ENABLE_OF,
};

extern enum mpam_enable_type kunpeng_mpam_enabled;

#define MPAM_IRQ_MODE_LEVEL    0x1
#define MPAM_IRQ_FLAGS_MASK    0x7f

#define mpam_irq_flags_to_acpi(x) ((x & MPAM_IRQ_MODE_LEVEL) ?  \
			ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE)

void mpam_device_set_error_irq(struct mpam_device *dev, u32 irq,
			u32 flags);
void mpam_device_set_overflow_irq(struct mpam_device *dev, u32 irq,
			u32 flags);

static inline int mpam_register_device_irq(struct mpam_device *dev,
			u32 overflow_interrupt, u32 overflow_flags,
			u32 error_interrupt, u32 error_flags)
{
	int irq, trigger;
	int ret = 0;
	u8 irq_flags;

	if (overflow_interrupt) {
		irq_flags = overflow_flags & MPAM_IRQ_FLAGS_MASK;
		trigger = mpam_irq_flags_to_acpi(irq_flags);

		irq = acpi_register_gsi(NULL, overflow_interrupt, trigger,
				ACPI_ACTIVE_HIGH);
		if (irq < 0) {
			pr_err_once("Failed to register overflow interrupt with ACPI\n");
			return ret;
		}

		mpam_device_set_overflow_irq(dev, irq, irq_flags);
	}

	if (error_interrupt) {
		irq_flags = error_flags & MPAM_IRQ_FLAGS_MASK;
		trigger = mpam_irq_flags_to_acpi(irq_flags);

		irq = acpi_register_gsi(NULL, error_interrupt, trigger,
				ACPI_ACTIVE_HIGH);
		if (irq < 0) {
			pr_err_once("Failed to register error interrupt with ACPI\n");
			return ret;
		}

		mpam_device_set_error_irq(dev, irq, irq_flags);
	}

	return ret;
}

int __init acpi_mpam_parse_version(void);

#endif
