// SPDX-License-Identifier: GPL-2.0-only

#include <linux/uaccess.h>
#include <linux/kernel.h>

#include <asm/vsyscall.h>

#ifdef CONFIG_X86_64
static __always_inline u64 canonical_address(u64 vaddr, u8 vaddr_bits)
{
	return ((s64)vaddr << (64 - vaddr_bits)) >> (64 - vaddr_bits);
}

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	unsigned long vaddr = (unsigned long)unsafe_src;

	/*
	 * Do not allow userspace addresses.  This disallows
	 * normal userspace and the userspace guard page:
	 */
	if (vaddr < TASK_SIZE_MAX + PAGE_SIZE)
		return false;

	/*
	 * Reading from the vsyscall page may cause an unhandled fault in
	 * certain cases.  Though it is at an address above TASK_SIZE_MAX, it is
	 * usually considered as a user space address.
	 */
	if (is_vsyscall_vaddr(vaddr))
		return false;

	/*
	 * Allow everything during early boot before 'x86_virt_bits'
	 * is initialized.  Needed for instruction decoding in early
	 * exception handlers.
	 */
	if (!boot_cpu_data.x86_virt_bits)
		return true;

	return canonical_address(vaddr, boot_cpu_data.x86_virt_bits) == vaddr;
}
#else
bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	return (unsigned long)unsafe_src >= TASK_SIZE_MAX;
}
#endif
