#include <common.h>
#include <sys/stat.h>
#include <zlib.h>

#define CONFIG_SHARP_WINCE_MODEL "flow"
#define CONFIG_SHARP_WINCE_NKGEN_ADDRESS 0x42000000
#define CONFIG_SHARP_WINCE_NKGEN_PAK_ADDRESS 0x42100000
#define CONFIG_SHARP_WINCE_NKGEN_ARENA_ADDRESS 0x42800000
#define CONFIG_SHARP_WINCE_NKGEN_ARENA_SIZE 0x1800000

#include "nkgen.c"
#include "nkpatch.c"

#define LOAD_ADDRESS 0x40200000
#define LOAD_REGION 0x1e00000
#define EXTENSION 0x10000
#define MAXIMUM_SPANS 64

int32_t nkgen_generate(void *call);

static unsigned int failures;
static u8 *image_file;
static u32 image_length;

static void expect(bool condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

void brain_progress_update(u32 done, u32 total)
{
}

void brain_progress_stage(const char *stage)
{
}

int brain_nkgen_enter(ulong start, ulong length, ulong entry, struct nkgen_call *generator_call)
{
	return nkgen_generate(generator_call);
}

static u8 *read_file(const char *path, u32 *length)
{
	FILE *file = fopen(path, "rb");
	u8 *data;
	long size;

	if (!file) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc(size ? size : 1);
	if (fread(data, 1, size, file) != (size_t)size)
		exit(2);
	fclose(file);
	*length = size;
	return data;
}

static void put_word(u8 *bytes, u32 value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
	bytes[2] = value >> 16;
	bytes[3] = value >> 24;
}

static void put_generator(void)
{
	u8 file[76];

	memset(file, 0, sizeof(file));
	memcpy(file, "NKGENBIN", 8);
	put_word(file + 8, 1);
	put_word(file + 12, 2);
	put_word(file + 16, 0x42000000);
	put_word(file + 20, sizeof(file));
	put_word(file + 24, 0x400);
	put_word(file + 28, 72);
	put_word(file + 36, 2);
	strcpy((char *)file + 40, "flow");
	put_word(file + 32, crc32(0, file + 72, sizeof(file) - 72));
	fake_card_put("pwsh2-boot/nkgen.bin", file, sizeof(file));
}

static int read_image_sectors(void *context, u32 first_sector, u32 sector_count, void *buffer)
{
	u64 start = (u64)first_sector * 512;

	if (start + (u64)sector_count * 512 > (u64)DIV_ROUND_UP(image_length, 512) * 512)
		return -EIO;
	memset(buffer, 0, sector_count * 512);
	memcpy(buffer, image_file + start, min((u64)sector_count * 512, image_length - start));
	return 0;
}

static u8 *load_region(void)
{
	return map_sysmem(LOAD_ADDRESS, LOAD_REGION);
}

static void clear_spans(u8 *image)
{
	struct nk_skip_span spans[MAXIMUM_SPANS];
	unsigned int count = brain_nk_bundle_skip_spans(image_length, spans, MAXIMUM_SPANS);
	unsigned int index;

	for (index = 0; index < count; index++)
		memset(image + spans[index].start, 0, spans[index].end - spans[index].start);
}

static unsigned int skipped_sections(void)
{
	const struct brain_nk_section *sections;
	unsigned int count = brain_nk_bundle_sections(&sections), index, skipped = 0;

	for (index = 0; index < count; index++)
		if (sections[index].problem) {
			printf("section %.*s skipped: %s at line %u\n", (int)sections[index].name_length, sections[index].name,
			       sections[index].problem, sections[index].line);
			skipped++;
		}
	return skipped;
}

static void start_boot(u32 identity)
{
	u8 *image = load_region();

	brain_boot_status_begin(image_length, &identity);
	brain_nk_bundle_clear();
	memcpy(image, image_file, image_length);
	memset(image + image_length, 0xcc, EXTENSION);
}

static const char *card_text_of(const char *path)
{
	static char text[0x2000];
	struct fake_file *file = fake_find(path);

	if (!file || file->length >= (loff_t)sizeof(text))
		return "";
	memcpy(text, file->data, file->length);
	text[file->length] = 0;
	return text;
}

static void dump_card(const char *directory)
{
	char path[512];
	unsigned int index;

	for (index = 0; index < FAKE_FILES; index++) {
		struct fake_file *file = &fake_files[index];
		FILE *output;

		if (!file->path[0])
			continue;
		snprintf(path, sizeof(path), "%s/%s", directory, file->path);
		if (file->directory) {
			mkdir(path, 0755);
			continue;
		}
		output = fopen(path, "wb");
		if (!output || fwrite(file->data, 1, file->length, output) != (size_t)file->length) {
			perror(path);
			exit(2);
		}
		fclose(output);
	}
}

int main(int argc, char **argv)
{
	u8 *first_boot;
	u8 *pak;
	u32 pak_size, identity = 0;
	unsigned int writes, sections_skipped;
	const char *status_text;

	if (argc < 3) {
		fprintf(stderr, "Usage: nkgen-flow-test NK_IMAGE nkgen.pak [CARD_DIRECTORY]\n");
		return 2;
	}
	image_file = read_file(argv[1], &image_length);
	pak = read_file(argv[2], &pak_size);
	fake_memory_add(LOAD_ADDRESS, LOAD_REGION);
	fake_memory_add(0x42000000, 0x100000);
	fake_memory_add(0x42100000, 0x700000);
	fake_memory_add(0x42800000, 0x1800000);
	fake_memory_add(0x45000000, 0x800000);
	put_generator();
	fake_card_put("pwsh2-boot/nkgen.pak", pak, pak_size);

	expect(!brain_image_identity(read_image_sectors, NULL, image_length, &identity), "flow: identity read from sectors");
	start_boot(identity);
	expect(brain_bundle_choose(image_length, false) == BRAIN_BUNDLE_GENERATE, "flow: first boot generates");
	expect(!brain_nkgen_generate(load_region(), image_length), "flow: generator agrees on the identity and succeeds");
	expect(!brain_nkgen_save(), "flow: outputs and cache.txt saved");
	clear_spans(load_region());
	brain_nk_bundle_apply(load_region(), image_length);
	sections_skipped = skipped_sections();
	expect(!sections_skipped, "flow: first boot applies every section from RAM");
	brain_boot_status_save();
	first_boot = malloc(image_length + EXTENSION);
	memcpy(first_boot, load_region(), image_length + EXTENSION);

	start_boot(identity);
	writes = fake_write_count;
	expect(brain_bundle_choose(image_length, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached"),
	       "flow: second boot uses the cache");
	clear_spans(load_region());
	brain_nk_bundle_apply(load_region(), image_length);
	expect(!skipped_sections(), "flow: second boot applies every section from the card");
	expect(!memcmp(first_boot, load_region(), image_length + EXTENSION), "flow: both boots leave the same image in RAM");
	brain_boot_status_save();
	status_text = card_text_of("pwsh2-boot/boot-status.txt");
	expect(strstr(status_text, "bundle=cached\n") != NULL, "flow: status says cached");
	expect(fake_write_count == writes + 1, "flow: second boot writes only the status");
	start_boot(identity);
	writes = fake_write_count;
	expect(brain_bundle_choose(image_length, true) == BRAIN_BUNDLE_GENERATE &&
		       !brain_nkgen_generate(load_region(), image_length) && !brain_nkgen_save(),
	       "flow: generating again onto the same card succeeds");
	expect(fake_write_count == writes + 2, "flow: generating again writes only nkgen.log and cache.txt");
	start_boot(identity);
	expect(brain_bundle_choose(image_length, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached"),
	       "flow: the cache is still valid after generating again");
	printf("%s", status_text);
	if (argc > 3)
		dump_card(argv[3]);
	return failures ? 1 : 0;
}
