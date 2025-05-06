// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>

#include "sysctl.h"

static s32 vk_mm_compute_batch(void)
{
	u64 memsized_batch;
	s32 nr = num_present_cpus();
	s32 batch = max_t(s32, nr * 2, 32);

	/* batch size set to 0.4% of (total memory/#cpus), or max int32 */
	memsized_batch = min_t(u64, (totalram_pages() / nr) / 256, INT_MAX);

	return max_t(s32, memsized_batch, batch);
}

void vk_sync_overcommit_as(struct vkernel *vk)
{
	struct percpu_counter *fbc = &vk->sysctl_vm.vm_committed_as;
	unsigned long flags;
	int cpu;
	s32 *pcount;
	s32 count;

	raw_spin_lock_irqsave(&fbc->lock, flags);
	for_each_cpu_or(cpu, cpu_online_mask, cpu_dying_mask) {
		pcount = per_cpu_ptr(fbc->counters, cpu);
		count = *pcount;
		fbc->count += count;
		*pcount -= count;
	}
	raw_spin_unlock_irqrestore(&fbc->lock, flags);
}

int vk_init_sysctl_vm(struct vkernel_sysctl_vm *vm)
{
	vm->max_map_count = DEFAULT_MAX_MAP_COUNT;
	vm->dac_mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
#ifdef CONFIG_LSM_MMAP_MIN_ADDR
	if (vm->dac_mmap_min_addr > CONFIG_LSM_MMAP_MIN_ADDR)
		vm->mmap_min_addr = vm->dac_mmap_min_addr;
	else
		vm->mmap_min_addr = CONFIG_LSM_MMAP_MIN_ADDR;
#else
	vm->mmap_min_addr = vm->dac_mmap_min_addr;
#endif

	vm->overcommit_memory = 0;
	vm->overcommit_ratio = 50;
	vm->overcommit_kbytes = 0;
	vm->as_batch = vk_mm_compute_batch();
	if (percpu_counter_init(&vm->vm_committed_as, 0, GFP_KERNEL)) {
		pr_err("vkernel: failed to init sysctl_vm vm_committed_as\n");
		return -ENOMEM;
	}

	return 0;
}

void vk_uninit_sysctl_vm(struct vkernel_sysctl_vm *vm)
{
	percpu_counter_destroy(&vm->vm_committed_as);
}

int vkernel_set_sysctl_vm(struct vkernel_sysctl_vm *vm, struct vkernel_sysctl_vm_desc *desc)
{
	if (desc->max_map_count > 0)
		vm->max_map_count = desc->max_map_count;

	if (desc->mmap_min_addr) {
		vm->dac_mmap_min_addr = desc->mmap_min_addr;
#ifdef CONFIG_LSM_MMAP_MIN_ADDR
		if (vm->dac_mmap_min_addr > CONFIG_LSM_MMAP_MIN_ADDR)
			vm->mmap_min_addr = vm->dac_mmap_min_addr;
		else
			vm->mmap_min_addr = CONFIG_LSM_MMAP_MIN_ADDR;
#else
		vm->mmap_min_addr = vm->dac_mmap_min_addr;
#endif
	}

	if (desc->overcommit_memory > 0) {
		vm->overcommit_memory = desc->overcommit_memory;
		if (desc->overcommit_ratio > 0) {
			vm->overcommit_ratio = desc->overcommit_ratio;
			vm->overcommit_kbytes = 0;
		} else if (desc->overcommit_kbytes) {
			vm->overcommit_ratio = 0;
			vm->overcommit_kbytes = desc->overcommit_kbytes;
		}
	}

	return 0;
}
EXPORT_SYMBOL(vkernel_set_sysctl_vm);
