#ifndef TEST_STUB_MAPMEM_H
#define TEST_STUB_MAPMEM_H

#include <stdlib.h>

#define FAKE_MEMORY_REGIONS 8

struct fake_region {
	unsigned long address;
	unsigned long length;
	unsigned char *memory;
};

static struct fake_region fake_regions[FAKE_MEMORY_REGIONS];
static unsigned int fake_region_count;

static inline unsigned char *fake_memory_add(unsigned long address, unsigned long length)
{
	struct fake_region *region = &fake_regions[fake_region_count++];

	region->address = address;
	region->length = length;
	region->memory = calloc(length, 1);
	return region->memory;
}

static inline void *map_sysmem(unsigned long address, unsigned long length)
{
	unsigned int index;

	for (index = 0; index < fake_region_count; index++)
		if (address >= fake_regions[index].address && address - fake_regions[index].address < fake_regions[index].length)
			return fake_regions[index].memory + (address - fake_regions[index].address);
	return (void *)address;
}

static inline unsigned long map_to_sysmem(const void *pointer)
{
	const unsigned char *bytes = pointer;
	unsigned int index;

	for (index = 0; index < fake_region_count; index++)
		if (bytes >= fake_regions[index].memory && bytes < fake_regions[index].memory + fake_regions[index].length)
			return fake_regions[index].address + (bytes - fake_regions[index].memory);
	return (unsigned long)pointer;
}

#endif
