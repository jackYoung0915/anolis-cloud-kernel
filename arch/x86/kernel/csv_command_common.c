// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hygon CSV support
 *
 * This file is shared between decompression boot code and running
 * linux kernel.
 *
 * Copyright (C) Hygon Info Technologies Ltd.
 */

#include <asm/e820/types.h>

#define CPUID_VENDOR_HygonGenuine_ebx	0x6f677948
#define CPUID_VENDOR_HygonGenuine_ecx	0x656e6975
#define CPUID_VENDOR_HygonGenuine_edx	0x6e65476e

#define MSR_CSV_ENABLED_BIT		30
#define MSR_CSV_ENABLED			BIT_ULL(MSR_CSV_ENABLED_BIT)
#define CPUID_CSV_ENABLED		BIT(30)

/*
 ****************************** CSV secure call ********************************
 *
 * CSV guest is based on hygon secure isolated virualization feature. An secure
 * processor which resides in hygon SOC manages guest's private memory. The
 * secure processor allocates or frees private memory for CSV guest and manages
 * CSV guest's nested page table.
 *
 * As the secure processor is considered as a PCI device in host, CSV guest can
 * not communicate with it directly. Howerver, CSV guest must request the secure
 * processor to change its physical memory between private memory and shared
 * memory. CSV secure call command is a method used to communicate with secure
 * processor that host cannot tamper with the data in CSV guest. Host can only
 * perform an external command to notify the secure processor to handle the
 * pending guest's command.
 *
 * CSV secure call pages:
 * Secure call pages are two dedicated pages that reserved by BIOS. We define
 * secure call pages as page A and page B. During guest launch stage, the secure
 * processor will parse the address of secure call pages. The secure processor
 * maps the two pages with same private memory page in NPT. The secure processor
 * always set one page as present and another page as non-present in NPT.

 * CSV secure call main work flow:
 * If we write the guest's commands in one page then read them from another page,
 * nested page fault happens and the guest exits to host. Then host will perform
 * an external command with the gpa(page A or page B) to the secure processor.
 * The secure processor checks that the gpa in NPF belongs to secure call pages,
 * read the guest's command to handle, then switch the present bit between the
 * two pages.
 *
 *			guest page A    guest page B
 *			      |              |
 *			  ____|______________|____
 *			  |                      |
 *			  |  nested page table   |
 *			  |______________________|
 *			      \              /
 *			       \            /
 *			        \          /
 *			         \        /
 *			          \      /
 *			       secure memory page
 *
 * CSV_SECURE_CMD_ENC:
 *	CSV guest declares a specifid memory range as secure. By default, all of
 *	CSV guest's memory mapped as secure.
 *	The secure processor allocate a block of secure memory and map the memory
 *	in CSV guest's NPT with the specified guest physical memory range in CSV
 *	secure call.
 *
 * CSV_SECURE_CMD_DEC:
 *	CSV guest declares a specified memory range as shared.
 *	The secure processor save the guest physical memory range in its own ram
 *	and free the range in CSV guest's NPT. When CSV guest access the memory,
 *	a new nested page fault happens.
 *
 * CSV_SECURE_CMD_RESET:
 *	CSV guest switches all of the shared memory to secure.
 *	The secure processor resets all the shared memory in CSV guest's NPT and
 *	clears the saved shared memory range. Then the secure process allocates
 *	secure memory to map in CSV guest's NPT.
 *
 * CSV_SECURE_CMD_UPDATE_SECURE_CALL_TABLE:
 *	CSV guest wants to change the secure call pages.
 *	The secure processor re-init the secure call context.
 *
 * CSV_SECURE_CMD_REQ_REPORT:
 *      CSV3 guest wants to request attestation report.
 *      The secure processor will update the request message buffer and respond
 *      buffer to indicate the result of this request.
 */
enum csv_secure_command_type {
	/* The secure call request should below CSV_SECURE_CMD_ACK */
	CSV_SECURE_CMD_ENC			= 0x1,
	CSV_SECURE_CMD_DEC,
	CSV_SECURE_CMD_RESET,
	CSV_SECURE_CMD_UPDATE_SECURE_CALL_TABLE,
	CSV_SECURE_CMD_REQ_REPORT		= 0x7,

	/* SECURE_CMD_ACK indicates secure call request can be handled */
	CSV_SECURE_CMD_ACK			= 0x6b,

	/*
	 * The following values are the error code of the secure call
	 * when firmware can't handling the specific secure call command
	 * as expected.
	 */
	CSV_SECURE_CMD_ERROR_INTERNAL		= 0x6c,
	CSV_SECURE_CMD_ERROR_INVALID_COMMAND	= 0x6d,
	CSV_SECURE_CMD_ERROR_INVALID_PARAM	= 0x6e,
	CSV_SECURE_CMD_ERROR_INVALID_ADDRESS	= 0x6f,
	CSV_SECURE_CMD_ERROR_INVALID_LENGTH	= 0x70,
};

/*
 * Secure call page fields.
 * Secure call page size is 4KB always. We define CSV secure call page structure
 * as below.
 * guid:	Must be in the first 128 bytes of the page. Its value should be
 *		(0xceba2fa59a5d926ful, 0xa556555d276b21abul) always.
 * cmd_type:	Command to be issued to the secure processor.
 * nums:	number of entries in the command.
 * base_address:Start address of the memory range.
 * size:	Size of the memory range.
 */
#define SECURE_CALL_ENTRY_MAX	(254)

/* size of secure call cmd is 4KB. */
struct csv_secure_call_cmd {
	union {
		u8	guid[16];
		u64	guid_64[2];
	};
	u32	cmd_type;
	u32	nums;
	u64	unused;
	struct {
		u64	base_address;
		u64	size;
	} entry[SECURE_CALL_ENTRY_MAX];
};

/* csv secure call guid, do not change the value. */
#define CSV_SECURE_CALL_GUID_LOW	0xceba2fa59a5d926ful
#define CSV_SECURE_CALL_GUID_HIGH	0xa556555d276b21abul

static u64 csv_boot_sc_page_a __initdata = -1ul;
static u64 csv_boot_sc_page_b __initdata = -1ul;
static u32 early_page_idx __initdata;

/**
 * csv_scan_secure_call_pages - try to find the secure call pages.
 * @boot_params:	boot parameters where e820_table resides.
 *
 * The secure call pages are reserved by BIOS. We scan all the reserved pages
 * to check the CSV secure call guid bytes.
 */
void __init csv_scan_secure_call_pages(struct boot_params *boot_params)
{
	struct boot_e820_entry *entry;
	struct csv_secure_call_cmd *sc_page;
	u64 offset;
	u64 addr;
	u8 i;
	u8 table_num;
	int count = 0;

	if (!boot_params)
		return;

	if (csv_boot_sc_page_a != -1ul && csv_boot_sc_page_b != -1ul)
		return;

	table_num = min_t(u8, boot_params->e820_entries,
			  E820_MAX_ENTRIES_ZEROPAGE);
	entry = &boot_params->e820_table[0];
	for (i = 0; i < table_num; i++) {
		if (entry[i].type != E820_TYPE_RESERVED)
			continue;

		addr = entry[i].addr & PAGE_MASK;
		for (offset = 0; offset < entry[i].size; offset += PAGE_SIZE) {
			sc_page = (void *)(addr + offset);
			if (sc_page->guid_64[0] == CSV_SECURE_CALL_GUID_LOW &&
			    sc_page->guid_64[1] == CSV_SECURE_CALL_GUID_HIGH) {
				if (count == 0)
					csv_boot_sc_page_a = addr + offset;
				else if (count == 1)
					csv_boot_sc_page_b = addr + offset;
				count++;
			}
			if (count >= 2)
				return;
		}
	}
}

/**
 * csv_early_secure_call - issue early secure call command at the stage where
 *			identity page table is created.
 * @base_address:	Start address of the specified memory range.
 * @num_pages:		number of the specific pages.
 * @cmd_type:		Secure call cmd type.
 */
void __init csv_early_secure_call(u64 base_address, u64 num_pages,
				  enum csv_secure_command_type cmd_type)
{
	struct csv_secure_call_cmd *page_rd;
	struct csv_secure_call_cmd *page_wr;
	u32 cmd_ack;

	if (csv_boot_sc_page_a == -1ul || csv_boot_sc_page_b == -1ul)
		return;

	/* identity mapping at the stage. */
	page_rd = (void *)(early_page_idx ? csv_boot_sc_page_a : csv_boot_sc_page_b);
	page_wr = (void *)(early_page_idx ? csv_boot_sc_page_b : csv_boot_sc_page_a);

	while (1) {
		page_wr->cmd_type = (u32)cmd_type;
		page_wr->nums = 1;
		page_wr->entry[0].base_address = base_address;
		page_wr->entry[0].size = num_pages << PAGE_SHIFT;

		/*
		 * Write command in page_wr must be done before retrieve cmd
		 * ack from page_rd, and it is ensured by the mb below.
		 */
		mb();

		cmd_ack = page_rd->cmd_type;
		if (cmd_ack != cmd_type)
			break;
	}
	early_page_idx ^= 1;
}
