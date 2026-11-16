#ifndef TEST_STUB_MAPMEM_H
#define TEST_STUB_MAPMEM_H

#include <common.h>

extern const char *test_bundle;

static inline void *map_sysmem(unsigned long address, unsigned long length)
{
	return (void *)test_bundle;
}

static inline unsigned long map_to_sysmem(const void *pointer)
{
	return (unsigned long)pointer;
}

#endif
