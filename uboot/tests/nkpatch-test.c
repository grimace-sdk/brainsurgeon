#include <common.h>
#include "nkpatch.c"

const char *test_bundle;
const char *test_bundle_path;
char test_size_path[MAXIMUM_PATH_LENGTH];
static unsigned int failures;
static const char *original = "000102030405060708090a0b0c0d0e0feeeeeeee";
static u32 progress_done, progress_total;

void brain_progress_update(u32 done, u32 total)
{
	progress_done = done;
	progress_total = total;
}

static void expect(bool condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

static void image_hex(const u8 *image, char *text)
{
	for (int index = 0; index < 20; index++)
		sprintf(text + index * 2, "%02x", image[index]);
}

static void check_patch(const char *name, const char *text, bool expect_valid, const char *expect_image)
{
	u8 image[24] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee };
	struct patch_result result = { 0 };
	char got[41];
	int verify;

	zeroed_end = 16;
	verify = run_patch_pass(text, strlen(text), image, 16, PASS_VERIFY, &result);
	if (!verify)
		run_patch_pass(text, strlen(text), image, 16, PASS_APPLY, &result);
	image_hex(image, got);
	expect((verify == 0) == expect_valid && !strcmp(got, expect_image), name);
}

static void check_bundle(void)
{
	u8 image[24] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xee, 0xee, 0xee, 0xee };
	struct nk_skip_span spans[8];
	char got[41];

	test_bundle = "skip 0 200 10\nskip 400 600 10\npatch first\nimage 10\n4 0405 aabb\npatch bad\nimage 10\n8 ff 00\n"
		      "patch extend\nimage 10 14\n10 00000000 11223344\n";
	brain_nk_bundle_load(0x10);
	expect(brain_nk_bundle_skip_spans(0x10, spans, 8) == 0, "bundle: span past the image is rejected");
	brain_nk_bundle_apply(image, 16);
	image_hex(image, got);
	expect(!strcmp(got, "00010203aabb060708090a0b0c0d0e0f11223344"), "bundle: good sections applied, bad one skipped");
	expect(progress_total == brain_nk_bundle_cost() && progress_done == progress_total,
	       "bundle: progress reaches the bundle cost");

	test_bundle = "skip 0 200 1000\nskip 400 600 1000\npatch p\nimage 1000\n";
	brain_nk_bundle_load(0x1000);
	expect(brain_nk_bundle_skip_spans(0x1000, spans, 8) == 2 && spans[0].end == 0x200 && spans[1].start == 0x400,
	       "bundle: skip spans read");
	expect(brain_nk_bundle_skip_spans(0x2000, spans, 8) == 0, "bundle: skip spans for another image ignored");

	test_bundle = "patch module\nimage 10\nload 0 4 0 modules/a.dll\n";
	test_bundle_path = "pwsh2-boot/nk-00000010/boot.nkp";
	expect(brain_nk_bundle_load(0x10) == 0, "bundle: per-image folder read");
	expect(brain_nk_bundle_cost() == strlen(test_bundle) * 7 / 10 + 133000, "bundle: a load costs more than its text");
	memset(image, 0, sizeof(image));
	brain_nk_bundle_apply(image, 16);
	expect(!strcmp(test_size_path, "pwsh2-boot/nk-00000010/modules/a.dll"), "bundle: loads resolved in its folder");
	expect(brain_nk_bundle_load(0x20) == -ENOENT, "bundle: another image's folder ignored");
	test_bundle_path = "pwsh2-boot/boot.nkp";
	expect(brain_nk_bundle_load(0x10) == 0, "bundle: falls back to boot.nkp");
	brain_nk_bundle_apply(image, 16);
	expect(!strcmp(test_size_path, "pwsh2-boot/modules/a.dll"), "bundle: fallback loads resolved in pwsh2-boot");
	test_bundle_path = NULL;

	test_bundle = NULL;
	expect(brain_nk_bundle_load(0x1000) == -ENOENT, "bundle: missing file");
}

int main(void)
{
	check_patch("valid", "image 10\r\n4 0405 aabb\n\nc 0c bb\n", true, "00010203aabb060708090a0bbb0d0e0feeeeeeee");
	check_patch("mismatch", "image 10\n4 0405 aabb\n8 ff 00\n", false, original);
	check_patch("wrong image", "image 20\n4 0405 aabb\n", false, original);
	check_patch("outside", "image 10\nf 0f10 0000\n", false, original);
	check_patch("malformed", "image 10\n4 040 aab\n", false, original);
	check_patch("extend", "image 10 14\n10 00000000 11223344\n", true, "000102030405060708090a0b0c0d0e0f11223344");
	check_patch("extend too far", "image 10 14\n12 000000 112233\n", false, "000102030405060708090a0b0c0d0e0f00000000");
	check_bundle();
	return failures ? 1 : 0;
}
