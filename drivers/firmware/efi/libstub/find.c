// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitmap.h>
#include <linux/minmax.h>

/*
 * Common helper for find_next_bit_stub() function family
 * @FETCH: The expression that fetches and pre-processes each word of bitmap(s)
 * @MUNGE: The expression that post-processes a word containing found bit (may be empty)
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 */
#define FIND_NEXT_BIT(FETCH, MUNGE, size, start)				\
({										\
	unsigned long mask, idx, tmp, sz = (size), __start = (start);		\
										\
	if (unlikely(__start >= sz))						\
		goto out;							\
										\
	mask = MUNGE(BITMAP_FIRST_WORD_MASK(__start));				\
	idx = __start / BITS_PER_LONG;						\
										\
	for (tmp = (FETCH) & mask; !tmp; tmp = (FETCH)) {			\
		if ((idx + 1) * BITS_PER_LONG >= sz)				\
			goto out;						\
		idx++;								\
	}									\
										\
	sz = min(idx * BITS_PER_LONG + __ffs(MUNGE(tmp)), sz);			\
out:										\
	sz;									\
})

unsigned long _find_next_bit_stub(const unsigned long *addr, unsigned long nbits,
				  unsigned long start)
{
	return FIND_NEXT_BIT(addr[idx], /* nop */, nbits, start);
}

unsigned long _find_next_zero_bit_stub(const unsigned long *addr, unsigned long nbits,
					 unsigned long start)
{
	return FIND_NEXT_BIT(~addr[idx], /* nop */, nbits, start);
}

/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit_stub(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	return _find_next_bit_stub(addr, size, offset);
}

unsigned long find_next_zero_bit_stub(const unsigned long *addr, unsigned long size,
				      unsigned long offset)
{
	return _find_next_zero_bit_stub(addr, size, offset);
}

/*
 * As find.h has include extern _find_next_bit declaration, while there's not
 * implement of _find_next_bit in compressed vmlinux, we implment a dummy one which
 * won't be used anywhere to avoid build issue
 */
unsigned long _find_next_bit(const unsigned long *addr1,
			     const unsigned long *addr2, unsigned long nbits,
			     unsigned long start, unsigned long invert, unsigned long le)
{
	return 0;
}
