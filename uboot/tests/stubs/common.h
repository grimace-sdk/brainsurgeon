#ifndef TEST_STUB_COMMON_H
#define TEST_STUB_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define loff_t uboot_loff_t
typedef long long loff_t;

#define FS_TYPE_FAT 1

static inline unsigned long simple_strtoul(const char *text, char **end, unsigned int base)
{
	return strtoul(text, end, base);
}

static inline unsigned long get_timer(unsigned long base)
{
	return 0;
}

static inline unsigned long timer_get_us(void)
{
	return 0;
}

static inline int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	character |= 0x20;
	return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

static inline int hex2bin(u8 *output, const char *input, size_t count)
{
	while (count--) {
		int high = hex_value(*input++);
		int low = hex_value(*input++);

		if (high < 0 || low < 0)
			return -1;
		*output++ = high << 4 | low;
	}
	return 0;
}

extern const char *test_bundle;
extern const char *test_bundle_path;
extern char test_size_path[];

static inline int fs_set_blk_dev(const char *interface, const char *device, int type)
{
	return 0;
}

static inline int fs_read(const char *name, unsigned long address, loff_t offset, loff_t length, loff_t *size)
{
	if (!test_bundle || (test_bundle_path && strcmp(name, test_bundle_path)))
		return -ENOENT;
	*size = strlen(test_bundle);
	return 0;
}

static inline void *map_sysmem(unsigned long address, unsigned long length)
{
	return (void *)test_bundle;
}

static inline unsigned long map_to_sysmem(const void *pointer)
{
	return (unsigned long)pointer;
}

static inline int fs_size(const char *name, loff_t *size)
{
	strcpy(test_size_path, name);
	return -ENOENT;
}

#endif
