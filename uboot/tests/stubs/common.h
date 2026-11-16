#ifndef TEST_STUB_COMMON_H
#define TEST_STUB_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef unsigned long ulong;

#define min(first, second) ((first) < (second) ? (first) : (second))
#define DIV_ROUND_UP(value, divisor) (((value) + (divisor) - 1) / (divisor))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
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

#endif
