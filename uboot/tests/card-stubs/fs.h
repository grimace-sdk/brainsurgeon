#ifndef TEST_STUB_FS_H
#define TEST_STUB_FS_H

#include <mapmem.h>
#include <strings.h>

#define FAKE_FILES 256
#define FAKE_PATH_LENGTH 160

struct fake_file {
	char path[FAKE_PATH_LENGTH];
	unsigned char *data;
	loff_t length;
	bool directory;
	unsigned int writes;
	unsigned int written_at;
};

static struct fake_file fake_files[FAKE_FILES];
static unsigned int fake_write_count;
static const char *fake_failing_write;

static inline struct fake_file *fake_find(const char *path)
{
	unsigned int index;

	for (index = 0; index < FAKE_FILES; index++)
		if (fake_files[index].path[0] && !strcasecmp(fake_files[index].path, path))
			return &fake_files[index];
	return NULL;
}

static inline struct fake_file *fake_create(const char *path)
{
	unsigned int index;

	for (index = 0; index < FAKE_FILES; index++)
		if (!fake_files[index].path[0]) {
			memset(&fake_files[index], 0, sizeof(fake_files[index]));
			snprintf(fake_files[index].path, FAKE_PATH_LENGTH, "%s", path);
			return &fake_files[index];
		}
	return NULL;
}

static inline bool fake_parent_exists(const char *path)
{
	char parent[FAKE_PATH_LENGTH];
	const char *slash = strrchr(path, '/');
	struct fake_file *directory;

	if (!slash)
		return true;
	snprintf(parent, sizeof(parent), "%.*s", (int)(slash - path), path);
	directory = fake_find(parent);
	return directory && directory->directory;
}

static inline void fake_card_clear(void)
{
	unsigned int index;

	for (index = 0; index < FAKE_FILES; index++) {
		free(fake_files[index].data);
		memset(&fake_files[index], 0, sizeof(fake_files[index]));
	}
	fake_write_count = 0;
	fake_failing_write = NULL;
}

static inline void fake_card_put(const char *path, const void *data, size_t length)
{
	struct fake_file *file = fake_find(path);
	const char *slash;
	char directory[FAKE_PATH_LENGTH];

	for (slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/')) {
		snprintf(directory, sizeof(directory), "%.*s", (int)(slash - path), path);
		if (!fake_find(directory))
			fake_create(directory)->directory = true;
	}
	if (!file)
		file = fake_create(path);
	free(file->data);
	file->data = malloc(length ? length : 1);
	memcpy(file->data, data, length);
	file->length = length;
}

static inline void fake_card_remove(const char *path)
{
	struct fake_file *file = fake_find(path);

	if (file) {
		free(file->data);
		memset(file, 0, sizeof(*file));
	}
}

static inline int fs_set_blk_dev(const char *interface, const char *device, int type)
{
	return 0;
}

static inline int fs_exists(const char *path)
{
	return fake_find(path) != NULL;
}

static inline int fs_size(const char *path, loff_t *size)
{
	struct fake_file *file = fake_find(path);

	if (!file || file->directory)
		return -ENOENT;
	*size = file->length;
	return 0;
}

static inline int fs_read(const char *path, unsigned long address, loff_t offset, loff_t length, loff_t *read)
{
	struct fake_file *file = fake_find(path);

	if (!file || file->directory || offset > file->length)
		return -ENOENT;
	if (!length || length > file->length - offset)
		length = file->length - offset;
	memcpy(map_sysmem(address, length), file->data + offset, length);
	*read = length;
	return 0;
}

static inline int fs_write(const char *path, unsigned long address, loff_t offset, loff_t length, loff_t *written)
{
	struct fake_file *file;

	if (fake_failing_write && !strcmp(fake_failing_write, path))
		return -ENOSPC;
	if (!fake_parent_exists(path) || offset)
		return -ENOENT;
	file = fake_find(path);
	if (file && file->directory)
		return -EISDIR;
	if (!file)
		file = fake_create(path);
	free(file->data);
	file->data = malloc(length ? length : 1);
	memcpy(file->data, map_sysmem(address, length), length);
	file->length = length;
	file->writes++;
	file->written_at = ++fake_write_count;
	*written = length;
	return 0;
}

static inline int fs_mkdir(const char *path)
{
	if (fake_find(path))
		return -EEXIST;
	if (!fake_parent_exists(path))
		return -ENOENT;
	fake_create(path)->directory = true;
	return 0;
}

static inline int fs_unlink(const char *path)
{
	struct fake_file *file = fake_find(path);

	if (!file || file->directory)
		return -ENOENT;
	fake_card_remove(path);
	return 0;
}

#endif
