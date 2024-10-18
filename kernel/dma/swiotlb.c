// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dynamic DMA mapping support.
 *
 * This implementation is a fallback for platforms that do not support
 * I/O TLBs (aka DMA address translation hardware).
 * Copyright (C) 2000 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 2000 Goutham Rao <goutham.rao@intel.com>
 * Copyright (C) 2000, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 03/05/07 davidm	Switch from PCI-DMA to generic device DMA API.
 * 00/12/13 davidm	Rename to swiotlb.c and add mark_clean() to avoid
 *			unnecessary i-cache flushing.
 * 04/07/.. ak		Better overflow handling. Assorted fixes.
 * 05/09/10 linville	Add support for syncing ranges, support syncing for
 *			DMA_BIDIRECTIONAL mappings, miscellaneous cleanup.
 * 08/12/11 beckyb	Add highmem support
 */

#define pr_fmt(fmt) "software IO TLB: " fmt

#include <linux/cache.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/swiotlb.h>
#include <linux/pfn.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/scatterlist.h>
#include <linux/cc_platform.h>
#include <linux/set_memory.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#include <asm/io.h>
#include <asm/dma.h>

#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/iommu-helper.h>

#define CREATE_TRACE_POINTS
#include <trace/events/swiotlb.h>

#define SLABS_PER_PAGE (1 << (PAGE_SHIFT - IO_TLB_SHIFT))

/*
 * Minimum IO TLB size to bother booting with.  Systems with mainly
 * 64bit capable cards will only lightly use the swiotlb.  If we can't
 * allocate a contiguous 1MB, we're probably in trouble anyway.
 */
#define IO_TLB_MIN_SLABS ((1<<20) >> IO_TLB_SHIFT)

enum swiotlb_force swiotlb_force;

/*
 * Max segment that we can provide which (if pages are contingous) will
 * not be bounced (unless SWIOTLB_FORCE is set).
 */
static unsigned int max_segment;

/*
 * We need to save away the original address corresponding to a mapped entry
 * for the sync operations.
 */
#define INVALID_PHYS_ADDR (~(phys_addr_t)0)

struct io_tlb_mem *io_tlb_default_mem;

static unsigned long default_nslabs = IO_TLB_DEFAULT_SIZE >> IO_TLB_SHIFT;
static unsigned long default_nareas;

/**
 * struct io_tlb_area - IO TLB memory area descriptor
 *
 * This is a single area with a single lock.
 *
 * @used:	The number of used IO TLB block.
 * @index:	The slot index to start searching in this area for next round.
 * @lock:	The lock to protect the above data structures in the map and
 *		unmap calls.
 */
struct io_tlb_area {
	unsigned long used;
	unsigned int index;
	spinlock_t lock;
};

static void swiotlb_adjust_nareas(unsigned int nareas)
{
	if (!is_power_of_2(nareas))
		nareas = roundup_pow_of_two(nareas);

	default_nareas = nareas;

	pr_info("area num %d.\n", nareas);
	/*
	 * Round up number of slabs to the next power of 2.
	 * The last area is going be smaller than the rest if
	 * default_nslabs is not power of two.
	 */
	if (nareas && !is_power_of_2(default_nslabs)) {
		default_nslabs = roundup_pow_of_two(default_nslabs);
		pr_info("SWIOTLB bounce buffer size roundup to %luMB",
			(default_nslabs << IO_TLB_SHIFT) >> 20);
	}
}

static int __init
setup_io_tlb_npages(char *str)
{
	if (isdigit(*str)) {
		/* avoid tail segment of size < IO_TLB_SEGSIZE */
		default_nslabs =
			ALIGN(simple_strtoul(str, &str, 0), IO_TLB_SEGSIZE);
	}
	if (*str == ',')
		++str;
	if (isdigit(*str))
		swiotlb_adjust_nareas(simple_strtoul(str, &str, 0));
	if (*str == ',')
		++str;
	if (!strcmp(str, "force")) {
		swiotlb_force = SWIOTLB_FORCE;
	} else if (!strcmp(str, "noforce")) {
		swiotlb_force = SWIOTLB_NO_FORCE;
		default_nslabs = 1;
	}

	return 0;
}
early_param("swiotlb", setup_io_tlb_npages);

unsigned long swiotlb_nr_tbl(void)
{
	return io_tlb_default_mem ? io_tlb_default_mem->nslabs : 0;
}
EXPORT_SYMBOL_GPL(swiotlb_nr_tbl);

unsigned int swiotlb_max_segment(void)
{
	return io_tlb_default_mem ? max_segment : 0;
}
EXPORT_SYMBOL_GPL(swiotlb_max_segment);

void swiotlb_set_max_segment(unsigned int val)
{
	if (swiotlb_force == SWIOTLB_FORCE)
		max_segment = 1;
	else
		max_segment = rounddown(val, PAGE_SIZE);
}

unsigned long swiotlb_size_or_default(void)
{
	return default_nslabs << IO_TLB_SHIFT;
}

void __init swiotlb_adjust_size(unsigned long size)
{
	/*
	 * If swiotlb parameter has not been specified, give a chance to
	 * architectures such as those supporting memory encryption to
	 * adjust/expand SWIOTLB size for their use.
	 */
	if (default_nslabs != IO_TLB_DEFAULT_SIZE >> IO_TLB_SHIFT)
		return;

	/*
	 * Round up number of slabs to the next power of 2.
	 * The last area is going be smaller than the rest if
	 * default_nslabs is not power of two.
	 */
	size = ALIGN(size, IO_TLB_SIZE);
	default_nslabs = ALIGN(size >> IO_TLB_SHIFT, IO_TLB_SEGSIZE);
	if (default_nareas) {
		default_nslabs = roundup_pow_of_two(default_nslabs);
		size = default_nslabs << IO_TLB_SHIFT;
	}

	pr_info("SWIOTLB bounce buffer size adjusted to %luMB", size >> 20);
}

void swiotlb_print_info(void)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;

	if (!mem) {
		pr_warn("No low mem\n");
		return;
	}

	pr_info("mapped [mem %pa-%pa] (%luMB)\n", &mem->start, &mem->end,
	       (mem->nslabs << IO_TLB_SHIFT) >> 20);
}

static inline unsigned long io_tlb_offset(unsigned long val)
{
	return val & (IO_TLB_SEGSIZE - 1);
}

static inline unsigned long nr_slots(u64 val)
{
	return DIV_ROUND_UP(val, IO_TLB_SIZE);
}

/*
 * Early SWIOTLB allocation may be too early to allow an architecture to
 * perform the desired operations.  This function allows the architecture to
 * call SWIOTLB when the operations are possible.  It needs to be called
 * before the SWIOTLB memory is used.
 */
void __init swiotlb_update_mem_attributes(void)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	void *vaddr;
	unsigned long bytes;

	if (!mem || mem->late_alloc)
		return;
	vaddr = phys_to_virt(mem->start);
	bytes = PAGE_ALIGN(mem->nslabs << IO_TLB_SHIFT);
	set_memory_decrypted((unsigned long)vaddr, bytes >> PAGE_SHIFT);
	memset(vaddr, 0, bytes);
}

int __init swiotlb_init_with_tbl(char *tlb, unsigned long nslabs, int verbose)
{
	unsigned long bytes = nslabs << IO_TLB_SHIFT, i;
	struct io_tlb_mem *mem;
	size_t alloc_size;

	alloc_size = PAGE_ALIGN(struct_size(mem, slots, nslabs));
	mem = memblock_alloc(alloc_size, PAGE_SIZE);
	if (!mem)
		panic("%s: Failed to allocate %zu bytes align=0x%lx\n",
		      __func__, alloc_size, PAGE_SIZE);

	mem->nslabs = nslabs;
	mem->start = __pa(tlb);
	mem->end = mem->start + bytes;

	/*
	 * default_nslabs maybe changed when adjust area number.
	 * So allocate bounce buffer after adjusting area number.
	 */
	if (!default_nareas)
		swiotlb_adjust_nareas(num_possible_cpus());

	mem->areas = memblock_alloc(sizeof(struct io_tlb_area) *
		default_nareas, SMP_CACHE_BYTES);
	if (!mem->areas)
		panic("%s: Failed to allocate mem->areas.\n", __func__);

	mem->nareas = default_nareas;
	mem->area_nslabs = nslabs / mem->nareas;

	for (i = 0; i < mem->nareas; i++) {
		spin_lock_init(&mem->areas[i].lock);
		mem->areas[i].index = 0;
	}

	for (i = 0; i < mem->nslabs; i++) {
		mem->slots[i].list = IO_TLB_SEGSIZE - io_tlb_offset(i);
		mem->slots[i].orig_addr = INVALID_PHYS_ADDR;
	}
	io_tlb_default_mem = mem;

	if (verbose)
		swiotlb_print_info();
	swiotlb_set_max_segment(mem->nslabs << IO_TLB_SHIFT);
	return 0;
}

/*
 * Statically reserve bounce buffer space and initialize bounce buffer data
 * structures for the software IO TLB used to implement the DMA API.
 */
void  __init
swiotlb_init(int verbose)
{
	size_t bytes = PAGE_ALIGN(default_nslabs << IO_TLB_SHIFT);
	void *tlb;

	/* Get IO TLB memory from the low pages */
	tlb = memblock_alloc_low(bytes, PAGE_SIZE);
	if (!tlb)
		goto fail;
	if (swiotlb_init_with_tbl(tlb, default_nslabs, verbose))
		goto fail_free_mem;
	return;

fail_free_mem:
	memblock_free_early(__pa(tlb), bytes);
fail:
	pr_warn("Cannot allocate buffer");
}

/*
 * Systems with larger DMA zones (those that don't support ISA) can
 * initialize the swiotlb later using the slab allocator if needed.
 * This should be just like above, but with some error catching.
 */
int
swiotlb_late_init_with_default_size(size_t default_size)
{
	unsigned long nslabs =
		ALIGN(default_size >> IO_TLB_SHIFT, IO_TLB_SEGSIZE);
	unsigned long bytes;
	unsigned char *vstart = NULL;
	unsigned int order;
	int rc = 0;

	/*
	 * Get IO TLB memory from the low pages
	 */
	order = get_order(nslabs << IO_TLB_SHIFT);
	nslabs = SLABS_PER_PAGE << order;
	bytes = nslabs << IO_TLB_SHIFT;

	while ((SLABS_PER_PAGE << order) > IO_TLB_MIN_SLABS) {
		vstart = (void *)__get_free_pages(GFP_DMA | __GFP_NOWARN,
						  order);
		if (vstart)
			break;
		order--;
	}

	if (!vstart)
		return -ENOMEM;

	if (order != get_order(bytes)) {
		pr_warn("only able to allocate %ld MB\n",
			(PAGE_SIZE << order) >> 20);
		nslabs = SLABS_PER_PAGE << order;
	}
	rc = swiotlb_late_init_with_tbl(vstart, nslabs);
	if (rc)
		free_pages((unsigned long)vstart, order);

	return rc;
}

int
swiotlb_late_init_with_tbl(char *tlb, unsigned long nslabs)
{
	unsigned long bytes = nslabs << IO_TLB_SHIFT, i;
	struct io_tlb_mem *mem;
	unsigned int area_order;

	mem = (void *)__get_free_pages(GFP_KERNEL,
		get_order(struct_size(mem, slots, nslabs)));
	if (!mem)
		return -ENOMEM;

	mem->nslabs = nslabs;
	mem->start = virt_to_phys(tlb);
	mem->end = mem->start + bytes;
	mem->late_alloc = 1;

	if (!default_nareas)
		swiotlb_adjust_nareas(num_possible_cpus());

	mem->nareas = default_nareas;
	mem->area_nslabs = nslabs / mem->nareas;

	area_order = get_order(array_size(sizeof(*mem->areas),
		default_nareas));
	mem->areas = (struct io_tlb_area *)
		__get_free_pages(GFP_KERNEL | __GFP_ZERO, area_order);
	if (!mem->areas)
		goto error_area;

	for (i = 0; i < mem->nareas; i++) {
		spin_lock_init(&mem->areas[i].lock);
		mem->areas[i].index = 0;
	}

	for (i = 0; i < mem->nslabs; i++) {
		mem->slots[i].list = IO_TLB_SEGSIZE - io_tlb_offset(i);
		mem->slots[i].orig_addr = INVALID_PHYS_ADDR;
	}

	set_memory_decrypted((unsigned long)tlb, bytes >> PAGE_SHIFT);
	memset(tlb, 0, bytes);

	io_tlb_default_mem = mem;
	swiotlb_print_info();

	swiotlb_set_max_segment(mem->nslabs << IO_TLB_SHIFT);

	return 0;

error_area:
	free_pages((unsigned long)mem, get_order(struct_size(mem, slots, nslabs)));
	return -ENOMEM;
}

void __init swiotlb_exit(void)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	size_t size, area_size;

	if (!mem)
		return;

	area_size = array_size(sizeof(*mem->areas), default_nareas);
	size = struct_size(mem, slots, mem->nslabs);
	if (mem->late_alloc){
		free_pages((unsigned long)mem->areas, get_order(area_size));
		free_pages((unsigned long)mem, get_order(size));
	} else {
		memblock_free_late(__pa(mem->areas), mem->nareas * sizeof(struct io_tlb_area));
		memblock_free_late(__pa(mem), PAGE_ALIGN(size));
	}

	io_tlb_default_mem = NULL;
}

/*
 * Bounce: copy the swiotlb buffer from or back to the original dma location
 */
static void swiotlb_bounce(phys_addr_t orig_addr, phys_addr_t tlb_addr,
			   size_t size, enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(orig_addr);
	unsigned char *vaddr = phys_to_virt(tlb_addr);

	if (PageHighMem(pfn_to_page(pfn))) {
		/* The buffer does not have a mapping.  Map it in and copy */
		unsigned int offset = orig_addr & ~PAGE_MASK;
		char *buffer;
		unsigned int sz = 0;
		unsigned long flags;

		while (size) {
			sz = min_t(size_t, PAGE_SIZE - offset, size);

			local_irq_save(flags);
			buffer = kmap_atomic(pfn_to_page(pfn));
			if (dir == DMA_TO_DEVICE)
				memcpy(vaddr, buffer + offset, sz);
			else
				memcpy(buffer + offset, vaddr, sz);
			kunmap_atomic(buffer);
			local_irq_restore(flags);

			size -= sz;
			pfn++;
			vaddr += sz;
			offset = 0;
		}
	} else if (dir == DMA_TO_DEVICE) {
		memcpy(vaddr, phys_to_virt(orig_addr), size);
	} else {
		memcpy(phys_to_virt(orig_addr), vaddr, size);
	}
}

static inline phys_addr_t slot_addr(phys_addr_t start, phys_addr_t idx)
{
	return start + (idx << IO_TLB_SHIFT);
}

/*
 * Return the offset into a iotlb slot required to keep the device happy.
 */
static unsigned int swiotlb_align_offset(struct device *dev, u64 addr)
{
	return addr & dma_get_min_align_mask(dev) & (IO_TLB_SIZE - 1);
}

/*
 * Carefully handle integer overflow which can occur when boundary_mask == ~0UL.
 */
static inline unsigned long get_max_slots(unsigned long boundary_mask)
{
	if (boundary_mask == ~0UL)
		return 1UL << (BITS_PER_LONG - IO_TLB_SHIFT);
	return nr_slots(boundary_mask + 1);
}

static unsigned int wrap_area_index(struct io_tlb_mem *mem, unsigned int index)
{
	if (index >= mem->area_nslabs)
		return 0;
	return index;
}

/*
 * Find a suitable number of IO TLB entries size that will fit this request and
 * allocate a buffer from that IO TLB pool.
 */
static int swiotlb_do_find_slots(struct io_tlb_mem *mem,
		struct io_tlb_area *area, int area_index,
		struct device *dev, phys_addr_t orig_addr,
		size_t alloc_size)
{
	unsigned long boundary_mask = dma_get_seg_boundary(dev);
	dma_addr_t tbl_dma_addr =
		phys_to_dma_unencrypted(dev, mem->start) & boundary_mask;
	unsigned long max_slots = get_max_slots(boundary_mask);
	unsigned int iotlb_align_mask =
		dma_get_min_align_mask(dev) & ~(IO_TLB_SIZE - 1);
	unsigned int nslots = nr_slots(alloc_size), stride;
	unsigned int index, index_nowrap = 0, wrap, count = 0, i;
	unsigned long flags;
	unsigned int slot_base;
	unsigned int slot_index;

	BUG_ON(!nslots);
	BUG_ON(area_index >= mem->nareas);

	/*
	 * For mappings with an alignment requirement don't bother looping to
	 * unaligned slots once we found an aligned one.  For allocations of
	 * PAGE_SIZE or larger only look for page aligned allocations.
	 */
	stride = (iotlb_align_mask >> IO_TLB_SHIFT) + 1;
	if (alloc_size >= PAGE_SIZE)
		stride = max(stride, stride << (PAGE_SHIFT - IO_TLB_SHIFT));

	spin_lock_irqsave(&area->lock, flags);
	if (unlikely(nslots > mem->area_nslabs - area->used))
		goto not_found;

	slot_base = area_index * mem->area_nslabs;
	index = wrap = wrap_area_index(mem, ALIGN(area->index, stride));
	do {
		slot_index = slot_base + index;
		if ((slot_addr(tbl_dma_addr, slot_index) & iotlb_align_mask) !=
		    (orig_addr & iotlb_align_mask)) {
			index = wrap_area_index(mem, index + 1);
			index_nowrap++;
			continue;
		}

		/*
		 * If we find a slot that indicates we have 'nslots' number of
		 * contiguous buffers, we allocate the buffers from that slot
		 * and mark the entries as '0' indicating unavailable.
		 */
		if (!iommu_is_span_boundary(slot_index, nslots,
					    nr_slots(tbl_dma_addr),
					    max_slots)) {
			if (mem->slots[slot_index].list >= nslots)
				goto found;
		}
		index = wrap_area_index(mem, index + stride);
		index_nowrap += stride;
	} while (index_nowrap < mem->area_nslabs);

not_found:
	spin_unlock_irqrestore(&area->lock, flags);
	return -1;

found:
	for (i = slot_index; i < slot_index + nslots; i++)
		mem->slots[i].list = 0;
	for (i = slot_index - 1;
	     io_tlb_offset(i) != IO_TLB_SEGSIZE - 1 &&
	     mem->slots[i].list; i--)
		mem->slots[i].list = ++count;

	/*
	 * Update the indices to avoid searching in the next round.
	 */
	if (index + nslots < mem->area_nslabs)
		area->index = index + nslots;
	else
		area->index = 0;
	area->used += nslots;
	spin_unlock_irqrestore(&area->lock, flags);
	return slot_index;
}

static int swiotlb_find_slots(struct device *dev, phys_addr_t orig_addr,
		size_t alloc_size)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	int start = raw_smp_processor_id() & ((1U << __fls(mem->nareas)) - 1);
	int i = start, index;

	do {
		index = swiotlb_do_find_slots(mem, mem->areas + i, i,
					      dev, orig_addr, alloc_size);
		if (index >= 0)
			return index;
		if (++i >= mem->nareas)
			i = 0;
	} while (i != start);

	return -1;
}

static unsigned long mem_used(struct io_tlb_mem *mem)
{
	int i;
	unsigned long used = 0;

	for (i = 0; i < mem->nareas; i++)
		used += mem->areas[i].used;
	return used;
}

phys_addr_t swiotlb_tbl_map_single(struct device *dev, phys_addr_t orig_addr,
		size_t mapping_size, size_t alloc_size,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	unsigned int offset = swiotlb_align_offset(dev, orig_addr);
	unsigned int i;
	int index;
	phys_addr_t tlb_addr;

	if (!mem)
		panic("Can not allocate SWIOTLB buffer earlier and can't now provide you with the DMA bounce buffer");

	if (cc_platform_has(CC_ATTR_MEM_ENCRYPT))
		pr_warn_once("Memory encryption is active and system is using DMA bounce buffers\n");

	if (mapping_size > alloc_size) {
		dev_warn_once(dev, "Invalid sizes (mapping: %zd bytes, alloc: %zd bytes)",
			      mapping_size, alloc_size);
		return (phys_addr_t)DMA_MAPPING_ERROR;
	}

	index = swiotlb_find_slots(dev, orig_addr, alloc_size + offset);
	if (index == -1) {
		if (!(attrs & DMA_ATTR_NO_WARN))
			dev_warn_ratelimited(dev,
	"swiotlb buffer is full (sz: %zd bytes), total %lu (slots), used %lu (slots)\n",
				 alloc_size, mem->nslabs, mem_used(mem));
		return (phys_addr_t)DMA_MAPPING_ERROR;
	}

	/*
	 * Save away the mapping from the original address to the DMA address.
	 * This is needed when we sync the memory.  Then we sync the buffer if
	 * needed.
	 */
	for (i = 0; i < nr_slots(alloc_size + offset); i++)
		mem->slots[index + i].orig_addr = slot_addr(orig_addr, i);

	tlb_addr = slot_addr(mem->start, index) + offset;
	/*
	 * When dir == DMA_FROM_DEVICE we could omit the copy from the orig
	 * to the tlb buffer, if we knew for sure the device will
	 * overwirte the entire current content. But we don't. Thus
	 * unconditional bounce may prevent leaking swiotlb content (i.e.
	 * kernel memory) to user-space.
	 */
	swiotlb_bounce(orig_addr, tlb_addr, mapping_size, DMA_TO_DEVICE);
	return tlb_addr;
}

/*
 * tlb_addr is the physical address of the bounce buffer to unmap.
 */
void swiotlb_tbl_unmap_single(struct device *hwdev, phys_addr_t tlb_addr,
			      size_t mapping_size, size_t alloc_size,
			      enum dma_data_direction dir, unsigned long attrs)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	unsigned long flags;
	unsigned int offset = swiotlb_align_offset(hwdev, tlb_addr);
	int i, count, nslots = nr_slots(alloc_size + offset);
	int index = (tlb_addr - offset - mem->start) >> IO_TLB_SHIFT;
	phys_addr_t orig_addr = mem->slots[index].orig_addr;
	int aindex = index / mem->area_nslabs;
	struct io_tlb_area *area = &mem->areas[aindex];

	/*
	 * First, sync the memory before unmapping the entry
	 */
	if (orig_addr != INVALID_PHYS_ADDR &&
	    !(attrs & DMA_ATTR_SKIP_CPU_SYNC) &&
	    ((dir == DMA_FROM_DEVICE) || (dir == DMA_BIDIRECTIONAL)))
		swiotlb_bounce(orig_addr, tlb_addr, mapping_size, DMA_FROM_DEVICE);

	/*
	 * Return the buffer to the free list by setting the corresponding
	 * entries to indicate the number of contiguous entries available.
	 * While returning the entries to the free list, we merge the entries
	 * with slots below and above the pool being returned.
	 */
	BUG_ON(aindex >= mem->nareas);

	spin_lock_irqsave(&area->lock, flags);
	if (index + nslots < ALIGN(index + 1, IO_TLB_SEGSIZE))
		count = mem->slots[index + nslots].list;
	else
		count = 0;

	/*
	 * Step 1: return the slots to the free list, merging the slots with
	 * superceeding slots
	 */
	for (i = index + nslots - 1; i >= index; i--) {
		mem->slots[i].list = ++count;
		mem->slots[i].orig_addr = INVALID_PHYS_ADDR;
	}

	/*
	 * Step 2: merge the returned slots with the preceding slots, if
	 * available (non zero)
	 */
	for (i = index - 1;
	     io_tlb_offset(i) != IO_TLB_SEGSIZE - 1 && mem->slots[i].list;
	     i--)
		mem->slots[i].list = ++count;
	area->used -= nslots;
	spin_unlock_irqrestore(&area->lock, flags);
}

void swiotlb_tbl_sync_single(struct device *hwdev, phys_addr_t tlb_addr,
			     size_t size, enum dma_data_direction dir,
			     enum dma_sync_target target)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;
	int index = (tlb_addr - mem->start) >> IO_TLB_SHIFT;
	phys_addr_t orig_addr = mem->slots[index].orig_addr;

	if (orig_addr == INVALID_PHYS_ADDR)
		return;

	orig_addr += (tlb_addr & (IO_TLB_SIZE - 1)) -
		swiotlb_align_offset(hwdev, orig_addr);

	switch (target) {
	case SYNC_FOR_CPU:
		if (likely(dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL))
			swiotlb_bounce(orig_addr, tlb_addr,
				       size, DMA_FROM_DEVICE);
		else
			BUG_ON(dir != DMA_TO_DEVICE);
		break;
	case SYNC_FOR_DEVICE:
		if (likely(dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL))
			swiotlb_bounce(orig_addr, tlb_addr,
				       size, DMA_TO_DEVICE);
		else
			BUG_ON(dir != DMA_FROM_DEVICE);
		break;
	default:
		BUG();
	}
}

/*
 * Create a swiotlb mapping for the buffer at @paddr, and in case of DMAing
 * to the device copy the data into it as well.
 */
dma_addr_t swiotlb_map(struct device *dev, phys_addr_t paddr, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t swiotlb_addr;
	dma_addr_t dma_addr;

	trace_swiotlb_bounced(dev, phys_to_dma(dev, paddr), size,
			      swiotlb_force);

	swiotlb_addr = swiotlb_tbl_map_single(dev, paddr, size, size, dir,
			attrs);
	if (swiotlb_addr == (phys_addr_t)DMA_MAPPING_ERROR)
		return DMA_MAPPING_ERROR;

	/* Ensure that the address returned is DMA'ble */
	dma_addr = phys_to_dma_unencrypted(dev, swiotlb_addr);
	if (unlikely(!dma_capable(dev, dma_addr, size, true))) {
		swiotlb_tbl_unmap_single(dev, swiotlb_addr, size, size, dir,
			attrs | DMA_ATTR_SKIP_CPU_SYNC);
		dev_WARN_ONCE(dev, 1,
			"swiotlb addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			&dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		return DMA_MAPPING_ERROR;
	}

	if (!dev_is_dma_coherent(dev) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(swiotlb_addr, size, dir);
	return dma_addr;
}

size_t swiotlb_max_mapping_size(struct device *dev)
{
	int min_align_mask = dma_get_min_align_mask(dev);
	int min_align = 0;

	/*
	 * swiotlb_find_slots() skips slots according to
	 * min align mask. This affects max mapping size.
	 * Take it into acount here.
	 */
	if (min_align_mask)
		min_align = roundup(min_align_mask, IO_TLB_SIZE);

	return ((size_t)IO_TLB_SIZE) * IO_TLB_SEGSIZE - min_align;
}

bool is_swiotlb_active(void)
{
	return io_tlb_default_mem != NULL;
}

#ifdef CONFIG_DEBUG_FS

static int io_tlb_used_get(void *data, u64 *val)
{
	*val = mem_used(io_tlb_default_mem);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_io_tlb_used, io_tlb_used_get, NULL, "%llu\n");

static int __init swiotlb_create_debugfs(void)
{
	struct io_tlb_mem *mem = io_tlb_default_mem;

	if (!mem)
		return 0;
	mem->debugfs = debugfs_create_dir("swiotlb", NULL);
	debugfs_create_ulong("io_tlb_nslabs", 0400, mem->debugfs, &mem->nslabs);
	debugfs_create_file("io_tlb_used", 0400, mem->debugfs, NULL,
			&fops_io_tlb_used);
	return 0;
}

late_initcall(swiotlb_create_debugfs);

#endif
