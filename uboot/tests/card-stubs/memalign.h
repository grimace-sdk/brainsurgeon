#ifndef TEST_STUB_MEMALIGN_H
#define TEST_STUB_MEMALIGN_H

#include <stdlib.h>

#define ARCH_DMA_MINALIGN 64
#define ALLOC_CACHE_ALIGN_BUFFER(type, name, size) type name[size] __attribute__((aligned(ARCH_DMA_MINALIGN)))

static inline void *memalign(size_t alignment, size_t size)
{
	void *pointer = NULL;

	return posix_memalign(&pointer, alignment, size) ? NULL : pointer;
}

static inline void *malloc_cache_aligned(size_t size)
{
	return memalign(ARCH_DMA_MINALIGN, size);
}

#endif
