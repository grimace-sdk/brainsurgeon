#ifndef TEST_STUB_FS_H
#define TEST_STUB_FS_H

#include <common.h>
#include <mapmem.h>

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

static inline int fs_size(const char *name, loff_t *size)
{
	strcpy(test_size_path, name);
	return -ENOENT;
}

#endif
