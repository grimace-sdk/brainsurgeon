#include <common.h>
#include <zlib.h>

#define CONFIG_SHARP_WINCE_MODEL "test"
#define CONFIG_SHARP_WINCE_NKGEN_ADDRESS 0x42000000
#define CONFIG_SHARP_WINCE_NKGEN_PAK_ADDRESS 0x42100000
#define CONFIG_SHARP_WINCE_NKGEN_ARENA_ADDRESS 0x42800000
#define CONFIG_SHARP_WINCE_NKGEN_ARENA_SIZE 0x1800000

#include "nkgen.c"
#include "nkpatch.c"

#define TEST_IMAGE_LENGTH 0x1000
#define TEST_IDENTITY 0x12345678
#define TEST_FOLDER "pwsh2-boot/nk-12345678"
#define TEST_LENGTH_FOLDER "pwsh2-boot/nk-00001000"

enum fake_generator_mode {
	FAKE_GOOD,
	FAKE_FAILS,
	FAKE_BAD_PATH,
	FAKE_OTHER_IDENTITY,
};

static unsigned int failures;
static enum fake_generator_mode generator_mode;
static unsigned int generator_calls;
static char stages[256];
static const char module_bytes[] = "ABCD";
static const char *card_contents = "card";

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
	strncat(stages, stage, sizeof(stages) - strlen(stages) - 2);
	strcat(stages, ";");
}

static void put_word(u8 *bytes, u32 value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
	bytes[2] = value >> 16;
	bytes[3] = value >> 24;
}

static const char *test_bundle_text(void)
{
	static char text[256];

	snprintf(text, sizeof(text),
		 "skip 200 400 1000\npatch bstart\nimage 1000\n10 00 aa\npatch module test.dll\nimage 1000\n"
		 "load 200 4 %08lx modules/test.dll\n",
		 crc32(0, (const u8 *)module_bytes, 4));
	return text;
}

static char *arena_text(u8 **cursor, const char *text)
{
	char *copy = (char *)*cursor;

	strcpy(copy, text);
	*cursor += strlen(text) + 1;
	return copy;
}

static int fake_generate(struct nkgen_call *generator_call)
{
	static struct nkgen_step steps[2] = { { "identify", 0, "test image" }, { "skip", 0, "1 spans" } };
	u8 *cursor = generator_call->arena + 0x1000;
	struct nkgen_output *outputs = (struct nkgen_output *)generator_call->arena;
	const char *text = test_bundle_text();

	generator_calls++;
	generator_call->identity_crc32 = generator_mode == FAKE_OTHER_IDENTITY ? 1 : TEST_IDENTITY;
	generator_call->known = 1;
	generator_call->profile = "pwtest";
	generator_call->device = "PW-TEST";
	generator_call->release = "v9.9";
	generator_call->step_count = 2;
	generator_call->steps = steps;
	memset(generator_call->image_sha256, 0xab, 32);
	generator_call->log(generator_call->context, "fake generator");
	generator_call->progress(generator_call->context, "Identifying image", 1, 2);
	if (generator_mode == FAKE_FAILS) {
		strcpy(generator_call->message, "Skip span 1000-2000 overlaps test.dll");
		generator_call->result = 3;
		return 3;
	}
	outputs[0].path = arena_text(&cursor, TEST_FOLDER "/boot.nkp");
	outputs[0].data = (const u8 *)arena_text(&cursor, text);
	outputs[0].length = strlen(text);
	outputs[1].path = arena_text(&cursor, TEST_FOLDER "/modules/test.dll");
	outputs[1].data = (const u8 *)arena_text(&cursor, module_bytes);
	outputs[1].length = 4;
	outputs[2].path = arena_text(&cursor, generator_mode == FAKE_BAD_PATH ? "pwsh2-boot/../EDSA2EXE.BIN" :
							     "pwsh2-boot/pwtest/Windows/card.txt");
	outputs[2].data = (const u8 *)arena_text(&cursor, card_contents);
	outputs[2].length = 4;
	for (int index = 0; index < 3; index++)
		outputs[index].crc32 = crc32(0, outputs[index].data, outputs[index].length);
	generator_call->outputs = outputs;
	generator_call->output_count = 3;
	return 0;
}

static void generator_call_outputs_reverse(void)
{
	struct nkgen_output *outputs = (struct nkgen_output *)call.outputs;
	struct nkgen_output first = outputs[0];

	outputs[0] = outputs[2];
	outputs[2] = first;
}

int brain_nkgen_enter(ulong start, ulong length, ulong entry, struct nkgen_call *generator_call)
{
	expect(start == 0x42000000 && entry == 0x42000048 && length == 0x4c + 0x400, "enter: code range and entry");
	return fake_generate(generator_call);
}

static void put_generator(const char *version, u32 abi, bool corrupt)
{
	u8 file[0x4c];

	memset(file, 0, sizeof(file));
	memcpy(file, "NKGENBIN", 8);
	put_word(file + 8, 1);
	put_word(file + 12, abi);
	put_word(file + 16, 0x42000000);
	put_word(file + 20, sizeof(file));
	put_word(file + 24, 0x400);
	put_word(file + 28, 0x48);
	put_word(file + 36, 2);
	strcpy((char *)file + 40, version);
	put_word(file + 0x48, 0xe12fff1e);
	put_word(file + 32, crc32(0, file + 72, sizeof(file) - 72) ^ (corrupt ? 1 : 0));
	fake_card_put("pwsh2-boot/nkgen.bin", file, sizeof(file));
}

static void put_pak(u8 variant)
{
	u8 pak[96];

	memset(pak, variant, sizeof(pak));
	memcpy(pak, "NKGENPAK", 8);
	put_word(pak + 8, 2);
	put_word(pak + 12, sizeof(pak));
	put_word(pak + 16, crc32(0, pak + 20, sizeof(pak) - 20));
	fake_card_put("pwsh2-boot/nkgen.pak", pak, sizeof(pak));
}

static void put_length_bundle(void)
{
	const char *text = "patch bstart\nimage 1000\n10 00 bb\n";

	fake_card_put(TEST_LENGTH_FOLDER "/boot.nkp", text, strlen(text));
}

static void begin(void)
{
	u32 identity = TEST_IDENTITY;

	stages[0] = 0;
	generator_calls = 0;
	brain_boot_status_begin(TEST_IMAGE_LENGTH, &identity);
	brain_nk_bundle_clear();
}

static u8 *boot_image(void)
{
	static u8 image[TEST_IMAGE_LENGTH + 0x10000];

	memset(image, 0x11, TEST_IMAGE_LENGTH);
	memset(image + TEST_IMAGE_LENGTH, 0, 0x10000);
	memset(image + 0x200, 0, 0x200);
	image[0x10] = 0;
	brain_nk_bundle_apply(image, TEST_IMAGE_LENGTH);
	return image;
}

static const char *card_text(const char *path)
{
	static char text[0x2000];
	struct fake_file *file = fake_find(path);

	if (!file || file->length >= (loff_t)sizeof(text))
		return "";
	memcpy(text, file->data, file->length);
	text[file->length] = 0;
	return text;
}

static u8 *identity_image;
static u32 identity_image_length;
static bool identity_read_fails;

static int read_identity_image(void *context, u32 first_sector, u32 sector_count, void *buffer)
{
	u32 sectors = DIV_ROUND_UP(identity_image_length, 512);

	if (identity_read_fails || first_sector + sector_count > sectors)
		return -EIO;
	memset(buffer, 0, sector_count * 512);
	memcpy(buffer, identity_image + first_sector * 512,
	       min((u64)sector_count * 512, (u64)identity_image_length - first_sector * 512));
	return 0;
}

static int identity_of(u32 length, u32 *identity)
{
	identity_image_length = length;
	return brain_image_identity(read_identity_image, NULL, length, identity);
}

static void check_identity_reader(void)
{
	static u8 image[0x30000];
	u32 identity = 0;
	u8 *header = image + 0x2010;

	identity_image = image;
	memset(image, 0x5a, sizeof(image));
	memcpy(image + 0x40, "ECEC", 4);
	put_word(image + 0x44, 0x80202010);
	put_word(header + 16, 3);
	put_word(header + 48, 2);
	expect(!identity_of(sizeof(image), &identity) && identity == crc32(0, header, 84 + 3 * 32 + 2 * 28),
	       "identity: CRC-32 of the header and tables");
	put_word(header + 16, 400);
	put_word(header + 48, 1000);
	expect(!identity_of(sizeof(image), &identity) && identity == crc32(0, header, 84 + 400 * 32 + 1000 * 28),
	       "identity: tables past the first 16 KB are read");
	put_word(header + 48, 2000);
	expect(identity_of(sizeof(image), &identity) == -EINVAL, "identity: tables over 64 KB refused");
	put_word(header + 48, 0x7fffffff);
	expect(identity_of(sizeof(image), &identity) == -EINVAL, "identity: huge table count refused");
	put_word(header + 16, 3);
	put_word(header + 48, 2);
	expect(identity_of(0x2010 + 100, &identity) == -EINVAL, "identity: tables past the image end refused");
	expect(identity_of(0x2010 + 84 + 3 * 32 + 2 * 28, &identity) == 0, "identity: tables ending at the image end");
	put_word(image + 0x44, 0x80100000);
	expect(identity_of(sizeof(image), &identity) == -EINVAL, "identity: ROM header below the image refused");
	put_word(image + 0x44, 0x80240000);
	expect(identity_of(sizeof(image), &identity) == -EINVAL, "identity: ROM header past the image refused");
	put_word(image + 0x44, 0x80202010);
	memcpy(image + 0x40, "XXXX", 4);
	expect(identity_of(sizeof(image), &identity) == -EINVAL, "identity: no ECEC refused");
	memcpy(image + 0x40, "ECEC", 4);
	identity_read_fails = true;
	expect(identity_of(sizeof(image), &identity) == -EIO, "identity: read error reported");
	identity_read_fails = false;
}

static void check_lookup_without_generator(void)
{
	fake_card_clear();
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "none"),
	       "lookup: nothing on the card boots unpatched");
	put_length_bundle();
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "existing") &&
		       !strcmp(brain_nk_bundle_directory(), TEST_LENGTH_FOLDER),
	       "lookup: nk-LENGTH used with no generator");
	brain_boot_status_begin(TEST_IMAGE_LENGTH, NULL);
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	expect(!strcmp(brain_nk_bundle_directory(), TEST_LENGTH_FOLDER), "lookup: nk-LENGTH used when the identity is unreadable");
}

static void check_first_and_second_boot(void)
{
	struct fake_file *stamp, *bundle;
	unsigned int writes;
	const char *text;
	u8 *image;

	fake_card_clear();
	put_generator("v1.0", 2, false);
	put_pak(1);
	put_length_bundle();
	begin();
	generator_mode = FAKE_GOOD;
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_GENERATE, "first boot: generator chosen");
	expect(!brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH) &&
		       !strcmp(status.bundle, "generated"),
	       "first boot: generated");
	expect(!strncmp(stages, "Identifying image;", 18), "first boot: generator stage text shown");
	expect(!brain_nkgen_save(), "first boot: outputs saved");
	stamp = fake_find(TEST_FOLDER "/cache.txt");
	bundle = fake_find(TEST_FOLDER "/boot.nkp");
	expect(stamp && bundle && fake_find(TEST_FOLDER "/modules/test.dll") && fake_find("pwsh2-boot/pwtest/Windows/card.txt"),
	       "first boot: every output written");
	expect(stamp && stamp->written_at == fake_write_count, "first boot: cache.txt written last");
	expect(fake_misaligned_writes == 0, "first boot: every write is from a cache-aligned buffer");
	expect(fake_find(TEST_FOLDER "/nkgen.log") && strstr(card_text(TEST_FOLDER "/nkgen.log"), "fake generator"),
	       "first boot: nkgen.log has the generator's log");
	image = boot_image();
	expect(image[0x10] == 0xaa && !memcmp(image + 0x200, module_bytes, 4), "first boot: applied from RAM");
	expect(!brain_boot_status_save(), "first boot: status saved");
	text = card_text("pwsh2-boot/boot-status.txt");
	expect(strstr(text, "image=known\nprofile=pwtest\ndevice=PW-TEST\nimage-crc=12345678\nimage-length=1000\n") &&
		       strstr(text, "bundle=generated\nfolder=" TEST_FOLDER "\ngenerator=v1.0\nrelease=v9.9\nuboot=test\nerror=\n") &&
		       strstr(text, "step identify=ok: test image\nstep skip=ok: 1 spans\n") &&
		       strstr(text, "section bstart=applied\nsection module test.dll=applied\n") &&
		       strstr(text, "image-sha256=abababab"),
	       "first boot: status text");

	begin();
	writes = fake_write_count;
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached") &&
		       generator_calls == 0,
	       "second boot: cache used, generator not called");
	image = boot_image();
	expect(image[0x10] == 0xaa && !memcmp(image + 0x200, module_bytes, 4), "second boot: applied from the card");
	brain_boot_status_save();
	text = card_text("pwsh2-boot/boot-status.txt");
	expect(strstr(text, "bundle=cached\n") && strstr(text, "release=v9.9\n") && strstr(text, "device=PW-TEST\n") &&
		       strstr(text, "step skip=ok: 1 spans\n"),
	       "second boot: status from cache.txt");
	brain_boot_status_save();
	expect(fake_write_count == writes + 1, "second boot: unchanged status is not rewritten");

	put_pak(2);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_GENERATE, "new pak: regenerated");
	put_pak(1);
	put_generator("v1.1", 2, false);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_GENERATE, "new generator: regenerated");
	put_generator("v1.0", 2, false);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, true) == BRAIN_BUNDLE_GENERATE, "bootsharpce generate: regenerated");
	fake_card_put("pwsh2-boot/no-nkgen", "", 0);
	put_pak(3);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached"),
	       "no-nkgen: outdated cache used as it stands");
	fake_card_remove("pwsh2-boot/no-nkgen");
	fake_card_remove("pwsh2-boot/nkgen.bin");
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached"),
	       "no generator: cache used as it stands");
	put_generator("v1.0", 2, false);
	put_pak(1);
	fake_card_put(TEST_FOLDER "/boot.nkp", "patch x\n", 8);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_GENERATE, "changed boot.nkp: regenerated");
	fake_card_remove(TEST_FOLDER "/cache.txt");
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "existing") &&
		       !strcmp(brain_nk_bundle_directory(), TEST_FOLDER),
	       "boot.nkp without cache.txt: used as it stands");
}

static void check_failures(void)
{
	const char *text;
	u8 *image;

	fake_card_clear();
	put_generator("v1.0", 2, true);
	put_pak(1);
	put_length_bundle();
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "existing") &&
		       strstr(status.error, "CRC-32"),
	       "bad generator CRC: nk-LENGTH used, reason kept");
	put_generator("v1.0", 1, false);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && strstr(status.error, "ABI"),
	       "older generator ABI: refused");
	put_generator("v1.0", 2, false);
	fake_card_put("pwsh2-boot/nkgen.pak", "NKGENPAK", 8);
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && strstr(status.error, "nkgen.pak"),
	       "short pak: refused");
	put_pak(1);

	generator_mode = FAKE_FAILS;
	begin();
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	expect(brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH) &&
		       strstr(status.error, "overlaps test.dll"),
	       "generator check fails: message kept");
	brain_bundle_fallback(TEST_IMAGE_LENGTH);
	expect(!strcmp(status.bundle, "existing") && !strcmp(brain_nk_bundle_directory(), TEST_LENGTH_FOLDER) &&
		       strstr(card_text(TEST_FOLDER "/nkgen.log"), "overlaps test.dll") && !fake_find(TEST_FOLDER "/cache.txt"),
	       "generator check fails: nk-LENGTH used, nkgen.log written");
	fake_card_remove(TEST_LENGTH_FOLDER "/boot.nkp");
	begin();
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH);
	brain_bundle_fallback(TEST_IMAGE_LENGTH);
	expect(!strcmp(status.bundle, "failed"), "generator check fails with nothing else: bundle failed");

	generator_mode = FAKE_BAD_PATH;
	begin();
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	expect(brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH) &&
		       strstr(status.error, "not a path under") && !fake_find(TEST_FOLDER "/boot.nkp"),
	       "output outside pwsh2-boot: refused before writing");

	generator_mode = FAKE_OTHER_IDENTITY;
	begin();
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	expect(brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH) &&
		       strstr(status.error, "identity"),
	       "generator identity differs: refused");

	generator_mode = FAKE_GOOD;
	begin();
	brain_bundle_choose(TEST_IMAGE_LENGTH, false);
	brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH);
	fake_failing_write = "pwsh2-boot/pwtest/Windows/card.txt";
	fake_card_put("pwsh2-boot/pwtest/Windows/card.txt", "old", 3);
	expect(brain_nkgen_save() && !fake_find(TEST_FOLDER "/boot.nkp") && !fake_find(TEST_FOLDER "/modules/test.dll") &&
		       !fake_find(TEST_FOLDER "/cache.txt") && !fake_find("pwsh2-boot/pwtest/Windows/card.txt"),
	       "card write fails: written outputs and the failed file removed, no cache.txt");
	fake_failing_write = TEST_FOLDER "/modules/test.dll";
	fake_card_put("pwsh2-boot/pwtest/Windows/card.txt", "card", 4);
	generator_call_outputs_reverse();
	expect(brain_nkgen_save() && fake_find("pwsh2-boot/pwtest/Windows/card.txt") && !fake_find(TEST_FOLDER "/boot.nkp"),
	       "card write fails: complete card files outside the bundle folder kept");
	generator_call_outputs_reverse();
	fake_failing_write = NULL;
	image = boot_image();
	expect(image[0x10] == 0xaa && !memcmp(image + 0x200, module_bytes, 4), "card write fails: boot continues from RAM");
	brain_boot_status_save();
	text = card_text("pwsh2-boot/boot-status.txt");
	expect(strstr(text, "bundle=generated\n") && strstr(text, "error=writing " TEST_FOLDER "/modules/test.dll failed"),
	       "card write fails: status says so");
}

static unsigned int writes_of(const char *path)
{
	struct fake_file *file = fake_find(path);

	return file ? file->writes : 0;
}

static void generate_and_save(bool regenerate)
{
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, regenerate) == BRAIN_BUNDLE_GENERATE &&
		       !brain_nkgen_generate(map_sysmem(0x40200000, TEST_IMAGE_LENGTH), TEST_IMAGE_LENGTH) && !brain_nkgen_save(),
	       regenerate ? "rewrite: regenerated and saved" : "rewrite: generated and saved");
}

static void check_rewrite_only_changes(void)
{
	unsigned int before;

	fake_card_clear();
	put_generator("v1.0", 2, false);
	put_pak(1);
	generator_mode = FAKE_GOOD;
	card_contents = "card";
	generate_and_save(false);
	before = fake_write_count;
	generate_and_save(true);
	expect(writes_of(TEST_FOLDER "/boot.nkp") == 1 && writes_of(TEST_FOLDER "/modules/test.dll") == 1 &&
		       writes_of("pwsh2-boot/pwtest/Windows/card.txt") == 1,
	       "rewrite: identical outputs on the card are not written again");
	expect(fake_write_count == before + 2 && fake_find(TEST_FOLDER "/cache.txt")->written_at > before &&
		       fake_find(TEST_FOLDER "/nkgen.log")->written_at > before &&
		       strstr(card_text(TEST_FOLDER "/cache.txt"), "file pwsh2-boot/pwtest/Windows/card.txt 4 ") &&
		       strstr(card_text(TEST_FOLDER "/nkgen.log"), "unchanged " TEST_FOLDER "/boot.nkp") &&
		       strstr(card_text(TEST_FOLDER "/nkgen.log"), "0 of 3 files written"),
	       "rewrite: cache.txt and nkgen.log still written, every file listed");
	card_contents = "cart";
	generate_and_save(true);
	expect(writes_of("pwsh2-boot/pwtest/Windows/card.txt") == 2 && writes_of(TEST_FOLDER "/boot.nkp") == 1 &&
		       !strcmp(card_text("pwsh2-boot/pwtest/Windows/card.txt"), "cart"),
	       "rewrite: only the changed file is written");
	card_contents = "card";
	begin();
	expect(brain_bundle_choose(TEST_IMAGE_LENGTH, false) == BRAIN_BUNDLE_READY && !strcmp(status.bundle, "cached"),
	       "rewrite: next boot uses the cache");
}

static void check_stage_names(void)
{
	expect(generator_stages[ARRAY_SIZE(generator_stages) - 1].start + generator_stages[ARRAY_SIZE(generator_stages) - 1].share ==
		       GENERATOR_UNITS,
	       "progress: generator stages fill the band");
}

int main(void)
{
	fake_memory_add(0x40200000, TEST_IMAGE_LENGTH);
	fake_memory_add(0x42000000, 0x100000);
	fake_memory_add(0x42100000, 0x700000);
	fake_memory_add(0x42800000, 0x1800000);
	fake_memory_add(0x45000000, 0x800000);
	check_identity_reader();
	check_lookup_without_generator();
	check_first_and_second_boot();
	check_failures();
	check_rewrite_only_changes();
	check_stage_names();
	return failures ? 1 : 0;
}
