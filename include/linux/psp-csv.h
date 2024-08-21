/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hygon Secure Virtualization feature CSV driver interface
 *
 * Copyright (C) Hygon Info Technologies Ltd.
 */

#ifndef __PSP_CSV_H__
#define __PSP_CSV_H__

#include <linux/types.h>

#define CSV_EXT_CSV3_MULT_LUP_DATA_BIT	0
#define CSV_EXT_CSV3_MULT_LUP_DATA	(1 << CSV_EXT_CSV3_MULT_LUP_DATA_BIT)
#define CSV_EXT_CSV3_INJ_SECRET_BIT	1
#define CSV_EXT_CSV3_INJ_SECRET		(1 << CSV_EXT_CSV3_INJ_SECRET_BIT)

/**
 * Guest/platform management commands
 */
enum csv3_cmd {
	/* Guest launch commands */
	CSV3_CMD_SET_GUEST_PRIVATE_MEMORY	= 0x200,
	CSV3_CMD_LAUNCH_ENCRYPT_DATA		= 0x201,
	CSV_CMD_LAUNCH_ENCRYPT_VMCB		= 0x202,
	/* Guest NPT(Nested Page Table) management commands */
	CSV_CMD_UPDATE_NPT			= 0x203,

	/* Guest migration commands */
	CSV_CMD_SEND_ENCRYPT_DATA		= 0x210,
	CSV_CMD_SEND_ENCRYPT_CONTEXT		= 0x211,
	CSV_CMD_RECEIVE_ENCRYPT_DATA		= 0x212,
	CSV_CMD_RECEIVE_ENCRYPT_CONTEXT		= 0x213,

	/* Guest debug commands */
	CSV_CMD_DBG_READ_VMSA			= 0x220,
	CSV_CMD_DBG_READ_MEM			= 0x221,

	/* Platform secure memory management commands */
	CSV_CMD_SET_SMR				= 0x230,
	CSV_CMD_SET_SMCR			= 0x231,

	CSV3_CMD_MAX,
};

/**
 * struct csv3_data_launch_encrypt_data - CSV3_CMD_LAUNCH_ENCRYPT_DATA command
 *
 * @handle: handle of the VM to update
 * @gpa: guest address where data is copied
 * @length: len of memory to be encrypted
 * @data_blocks: memory regions to hold data page address
 */
struct csv3_data_launch_encrypt_data {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 gpa;			/* In */
	u32 length;			/* In */
	u32 reserved1;			/* In */
	u64 data_blocks[8];		/* In */
} __packed;

/**
 * struct csv_data_launch_encrypt_vmcb - CSV_CMD_LAUNCH_ENCRYPT_VMCB command
 *
 * @handle: handle of the VM
 * @vcpu_id: id of vcpu per vmsa/vmcb
 * @vmsa_addr: memory address of initial vmsa data
 * @vmsa_len: len of initial vmsa data
 * @shadow_vmcb_addr: memory address of shadow vmcb data
 * @shadow_vmcb_len: len of shadow vmcb data
 * @secure_vmcb_addr: memory address of secure vmcb data
 * @secure_vmcb_len: len of secure vmcb data
 */
struct csv_data_launch_encrypt_vmcb {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u32 vcpu_id;			/* In */
	u32 reserved1;			/* In */
	u64 vmsa_addr;			/* In */
	u32 vmsa_len;			/* In */
	u32 reserved2;			/* In */
	u64 shadow_vmcb_addr;		/* In */
	u32 shadow_vmcb_len;		/* In */
	u32 reserved3;			/* In */
	u64 secure_vmcb_addr;		/* Out */
	u32 secure_vmcb_len;		/* Out */
} __packed;

/**
 * struct csv_data_update_npt - CSV_CMD_UPDATE_NPT command
 *
 * @handle: handle assigned to the VM
 * @error_code: nested page fault error code
 * @gpa: guest page address where npf happens
 * @spa: physical address which maps to gpa in host page table
 * @level: page level which can be mapped in nested page table
 * @page_attr: page attribute for gpa
 * @page_attr_mask: which page attribute bit should be set
 * @npages: number of pages from gpa is handled.
 */
struct csv_data_update_npt {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u32 error_code;			/* In */
	u32 reserved1;			/* In */
	u64 gpa;			/* In */
	u64 spa;			/* In */
	u64 level;			/* In */
	u64 page_attr;			/* In */
	u64 page_attr_mask;		/* In */
	u32 npages;			/* In/Out */
} __packed;

/**
 * struct csv_data_mem_region - define a memory region
 *
 * @base_address: base address of a memory region
 * @size: size of memory region
 */
struct csv_data_memory_region {
	u64 base_address;		/* In */
	u64 size;			/* In */
} __packed;

/**
 * struct csv3_data_set_guest_private_memory - CSV3_CMD_SET_GUEST_PRIVATE_MEMORY
 * command parameters
 *
 * @handle: handle assigned to the VM
 * @nregions: number of memory regions
 * @regions_paddr: address of memory containing multiple memory regions
 */
struct csv3_data_set_guest_private_memory {
	u32 handle;			/* In */
	u32 nregions;			/* In */
	u64 regions_paddr;		/* In */
} __packed;

/**
 * struct csv_data_set_smr - CSV_CMD_SET_SMR command parameters
 *
 * @smr_entry_size: size of SMR entry
 * @nregions: number of memory regions
 * @regions_paddr: address of memory containing multiple memory regions
 */
struct csv_data_set_smr {
	u32 smr_entry_size;		/* In */
	u32 nregions;			/* In */
	u64 regions_paddr;		/* In */
} __packed;

/**
 * struct csv_data_set_smcr - CSV_CMD_SET_SMCR command parameters
 *
 * @base_address: start address of SMCR memory
 * @size: size of SMCR memory
 */
struct csv_data_set_smcr {
	u64 base_address;		/* In */
	u64 size;			/* In */
} __packed;

/**
 * struct csv_data_dbg_read_vmsa - CSV_CMD_DBG_READ_VMSA command parameters
 *
 * @handle: handle assigned to the VM
 * @spa: system physical address of memory to get vmsa of the specific vcpu
 * @size: size of the host memory
 * @vcpu_id: the specific vcpu
 */
struct csv_data_dbg_read_vmsa {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 spa;			/* In */
	u32 size;			/* In */
	u32 vcpu_id;			/* In */
} __packed;

/**
 * struct csv_data_dbg_read_mem - CSV_CMD_DBG_READ_MEM command parameters
 *
 * @handle: handle assigned to the VM
 * @gpa: guest physical address of the memory to access
 * @spa: system physical address of memory to get data from gpa
 * @size: size of guest memory to access
 */
struct csv_data_dbg_read_mem {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 gpa;			/* In */
	u64 spa;			/* In */
	u32 size;			/* In */
} __packed;

/**
 * struct csv3_data_attestation_report - ATTESTATION secure call command parameters
 *
 * @handle: handle of the VM to process
 * @resp_gpa: guest physical address to save the generated report
 * @resp_length: length of the generated report
 * @req_gpa: guest physical address of the input for the report
 * @req_length: length of the input for the report
 * @fw_error_code: firmware status code when generating the report
 */
struct csv3_data_attestation_report {
	u32 handle;				/* Out */
	u32 reserved1;
	u64 resp_gpa;				/* In */
	u8 reserved2[16];
	u32 resp_len;				/* In/Out */
	u32 reserved3;
	u64 req_gpa;				/* In */
	u32 req_len;				/* In,Out */
	u32 fw_error_code;			/* Out */
} __packed;

/**
 * struct csv_data_send_encrypt_data - SEND_ENCRYPT_DATA command parameters
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header
 * @hdr_len: len of packet header
 * @guest_block: physical address containing multiple guest address
 * @guest_len: len of guest block
 * @flag: flag of send encrypt data
 *        0x00000000: migrate pages in guest block
 *        0x00000001: set readonly of pages in guest block
 *            others: invalid
 * @trans_block: physical address of a page containing multiple host memory pages
 * @trans_len: len of host memory region
 */
struct csv_data_send_encrypt_data {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 hdr_address;		/* In */
	u32 hdr_len;			/* In/Out */
	u32 reserved1;			/* In */
	u64 guest_block;		/* In */
	u32 guest_len;			/* In */
	u32 flag;			/* In */
	u64 trans_block;		/* In */
	u32 trans_len;			/* In/Out */
} __packed;

/**
 * struct csv_data_send_encrypt_context - SEND_ENCRYPT_CONTEXT command parameters
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header
 * @hdr_len: len of packet header
 * @trans_block: physical address of a page containing multiple host memory pages
 * @trans_len: len of host memory region
 */
struct csv_data_send_encrypt_context {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 hdr_address;		/* In */
	u32 hdr_len;			/* In/Out */
	u32 reserved1;			/* In */
	u64 trans_block;		/* In */
	u32 trans_len;			/* In/Out */
} __packed;

/**
 * struct csv_data_receive_encrypt_data - RECEIVE_ENCRYPT_DATA command parameters
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header blob
 * @hdr_len: len of packet header
 * @guest_block: system physical address containing multiple guest address
 * @guest_len: len of guest block memory region
 * @trans_block: physical address of a page containing multiple host memory pages
 * @trans_len: len of host memory region
 */
struct csv_data_receive_encrypt_data {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 hdr_address;		/* In */
	u32 hdr_len;			/* In */
	u32 reserved1;			/* In */
	u64 guest_block;		/* In */
	u32 guest_len;			/* In */
	u32 reserved2;			/* In */
	u64 trans_block;		/* In */
	u32 trans_len;			/* In */
} __packed;

/**
 * struct csv_data_receive_encrypt_context - RECEIVE_ENCRYPT_CONTEXT command parameters
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header
 * @hdr_len: len of packet header
 * @trans_block: physical address of a page containing multiple host memory pages
 * @trans_len: len of host memory region
 * @shadow_vmcb_block: physical address of a page containing multiple shadow vmcb address
 * @secure_vmcb_block: physical address of a page containing multiple secure vmcb address
 * @vmcb_block_len: len of shadow/secure vmcb block
 */
struct csv_data_receive_encrypt_context {
	u32 handle;			/* In */
	u32 reserved;			/* In */
	u64 hdr_address;		/* In */
	u32 hdr_len;			/* In */
	u32 reserved1;			/* In */
	u64 trans_block;		/* In */
	u32 trans_len;			/* In */
	u32 reserved2;			/* In */
	u64 shadow_vmcb_block;		/* In */
	u64 secure_vmcb_block;		/* In */
	u32 vmcb_block_len;		/* In */
} __packed;

#ifdef CONFIG_CRYPTO_DEV_SP_PSP

/**
 * csv_get_extension_info - collect extension set of the firmware
 *
 * @buf: The buffer to save extension set
 * @size: The size of the @buf
 *
 * Returns:
 * 0 if @buf is filled with extension bitflags
 * -%ENODEV if the CSV device is not available
 * -%EINVAL if @buf is NULL or @size is too smaller
 */
int csv_get_extension_info(void *buf, size_t *size);

#else	/* !CONFIG_CRYPTO_DEV_SP_PSP */

static inline int csv_get_extension_info(void *buf, size_t *size) { return -ENODEV; }

#endif	/* !CONFIG_CRYPTO_DEV_SP_PSP */

#endif	/* __PSP_CSV_H__ */
