#include "internal.h"

#define DRIVER_TEXT_ADDRESS 0xc06f1000u
#define PANEL_WRITE 0xc06f72e4u
#define FRAME_TRANSFER 0xc06f70fcu
#define TRAMPOLINE_ADDRESS 0xc06f1dc0u
#define MOVE_POINTER 0xc06fa770u
#define MOVE_POINTER_ENTRY_OFFSET 4u
#define PANEL_UPDATE_SLOT 0xc06f1944u
#define PANEL_UPDATE 0xc06f63a4u
#define PANEL_UPDATE_ENTRY_OFFSET 8u
#define WRITE_DEBUG_STRING_ENTRY_OFFSET 12u
#define REFRESH_ENTRY_OFFSET 16u
#define REFRESH_CALL_SITE 0xc06f82b0u
#define NO_OPERATION 0xe1a00000u
#define OEM_GLOBAL_DATA_OFFSET 0xf0u
#define OEM_GLOBAL_VERSION 0x60000u
#define OEM_GLOBAL_WRITE_DEBUG_STRING 0x10u
#define KERNEL_WRITE_DEBUG_STRING 0x8020c700u
#define LOAD_PC_FROM_NEXT_WORD 0xe51ff004u
#define WORD_COUNT 14u

static const char driver_name[] = "ddraw_mx28.dll";
static const char trampoline_string[] = "C:\\WINCE600\\";

static const uint8_t display_images[2][32] = {
    {
        0x89, 0x4a, 0x22, 0x7c, 0xd6, 0xa2, 0x62, 0xb1, 0x57, 0xcb, 0x1a, 0x8d, 0xc6, 0xc9, 0xdb, 0xfa,
        0x52, 0x1f, 0x75, 0x7a, 0xb9, 0x0a, 0x7f, 0xa9, 0xe1, 0x8a, 0xb6, 0x0c, 0xef, 0xaa, 0x3d, 0x81,
    },
    {
        0x08, 0x44, 0x88, 0x3d, 0x47, 0xf5, 0xb0, 0xd7, 0xd5, 0x3b, 0x58, 0xf6, 0x76, 0x19, 0x45, 0x90,
        0x49, 0xbf, 0xb2, 0x13, 0xc2, 0x7c, 0x71, 0xf1, 0x43, 0x41, 0x4f, 0x79, 0xa8, 0x82, 0xa4, 0xca,
    },
};

struct display_word {
    uint32_t address;
    uint32_t original;
    uint32_t replacement;
    const char *purpose;
};

int a7300_display_image(const uint8_t sha256_digest[32])
{
    if (!sha256_digest) return 0;
    return bytes_equal(sha256_digest, display_images[0], 32) || bytes_equal(sha256_digest, display_images[1], 32);
}

static uint32_t branch(uint32_t from, uint32_t to, int link)
{
    return (link ? 0xeb000000u : 0xea000000u) | (((to - (from + 8)) >> 2) & 0x00ffffffu);
}

static void word_records(struct buffer *text, struct rom_image *image, uint32_t offset, uint32_t replacement)
{
    uint8_t bytes[4];
    write_u32(bytes, 0, replacement);
    read_u32(image->bytes, image->length, offset);
    patch_records(text, offset, image->bytes + offset, 4, bytes, 4);
}

void a7300_display_records(struct buffer *text, struct rom_image *image, uint32_t hook_address)
{
    struct rom_module *driver = find_module(image, driver_name);
    check(driver, "%s not found", driver_name);
    check(driver->section_count > 0, "%s has no sections", driver_name);
    struct rom_section *code = &driver->sections[0];
    check(code->real_address == DRIVER_TEXT_ADDRESS && !code->compressed, "%s code is not at 0x%08x or is compressed", driver_name, DRIVER_TEXT_ADDRESS);
    int64_t code_offset = code->data_offset == NO_OFFSET ? 0 : code->data_offset;

    const uint32_t tear_on_tail = TRAMPOLINE_ADDRESS;
    const uint32_t hook_trampoline = TRAMPOLINE_ADDRESS + 12;
    const uint32_t refresh_trampoline = TRAMPOLINE_ADDRESS + 20;
    const struct display_word words[WORD_COUNT] = {
        { 0xc06f6328u, 0xe3a01028u, 0xe3a01008u, "MADCTL data 28 -> 08: no row/column exchange" },
        { 0xc06f6390u, 0xe3a01034u, 0xe3a01035u, "tearing effect off (34) -> on (35)" },
        { 0xc06f49c0u, 0xe59f0060u, 0xea00000bu, "TE watchdog: skip the panel re-initialisation, which the hook TE wait can trigger falsely" },
        { 0xc06f63a0u, 0xe12fff1eu, branch(0xc06f63a0u, tear_on_tail, 0), "end of panel init: send the 35 parameter" },
        { 0xc06f655cu, branch(0xc06f655cu, FRAME_TRANSFER, 1), branch(0xc06f655cu, hook_trampoline, 1), "frame transfer through the portrait hook" },
        { MOVE_POINTER, 0xe92d41f0u, LOAD_PC_FROM_NEXT_WORD, "MovePointer: jump to the hook, which also marks the cursor areas dirty" },
        { MOVE_POINTER + 4, 0xe1a07002u, hook_address + MOVE_POINTER_ENTRY_OFFSET, "MovePointer hook address" },
        { PANEL_UPDATE_SLOT, PANEL_UPDATE, hook_address + PANEL_UPDATE_ENTRY_OFFSET, "panel controller update (vtable slot 6): send only the dirty rectangle" },
        { REFRESH_CALL_SITE, 0xe28d1000u, 0xe1a00006u, "refresh thread: pass the driver to the hook" },
        { REFRESH_CALL_SITE + 4, 0xe1a00006u, branch(REFRESH_CALL_SITE + 4, refresh_trampoline, 1), "refresh thread: send merged dirty tiles instead of one bounding rectangle" },
        { REFRESH_CALL_SITE + 8, 0xebfffefdu, NO_OPERATION, "refresh thread: original collect call" },
        { REFRESH_CALL_SITE + 12, 0xe28d1000u, NO_OPERATION, "refresh thread: original argument" },
        { REFRESH_CALL_SITE + 16, 0xe1a00006u, NO_OPERATION, "refresh thread: original argument" },
        { REFRESH_CALL_SITE + 20, 0xebffff85u, NO_OPERATION, "refresh thread: original send call" },
    };
    for (uint32_t index = 0; index < WORD_COUNT; index++) {
        const struct display_word *word = &words[index];
        check(word->address >= code->real_address && (uint64_t)word->address + 4 <= (uint64_t)code->real_address + code->virtual_size,
            "0x%08x is outside %s code", word->address, driver_name);
        int64_t offset = code_offset + word->address - code->real_address;
        uint32_t actual = read_u32(image->bytes, image->length, offset);
        check(actual == word->original, "0x%08x (%s) is 0x%08x, expected 0x%08x", word->address, word->purpose, actual, word->original);
        word_records(text, image, (uint32_t)offset, word->replacement);
    }

    struct rom_module *kernel = find_module(image, "nk.exe");
    check(kernel, "nk.exe not found");
    check(kernel->section_count > 2 && !kernel->sections[2].compressed && kernel->sections[2].data_offset != NO_OFFSET, "nk.exe initialised data not found");
    int64_t oem_global = kernel->sections[2].data_offset + OEM_GLOBAL_DATA_OFFSET;
    uint32_t version = read_u32(image->bytes, image->length, oem_global);
    check(version == OEM_GLOBAL_VERSION, "OEMGLOBAL version at 0x%08llx is 0x%08x", (unsigned long long)oem_global, version);
    check(read_u32(image->bytes, image->length, oem_global + OEM_GLOBAL_WRITE_DEBUG_STRING) == KERNEL_WRITE_DEBUG_STRING,
        "OEMGLOBAL pfnWriteDebugString is not the expected OEMWriteDebugString");
    word_records(text, image, (uint32_t)(oem_global + OEM_GLOBAL_WRITE_DEBUG_STRING), hook_address + WRITE_DEBUG_STRING_ENTRY_OFFSET);

    check(TRAMPOLINE_ADDRESS + 4 <= (uint64_t)code->real_address + code->virtual_size, "0x%08x is outside %s code", TRAMPOLINE_ADDRESS, driver_name);
    int64_t trampoline_offset = code_offset + TRAMPOLINE_ADDRESS - code->real_address;
    uint32_t string_length = sizeof(trampoline_string) - 1;
    for (uint32_t index = 0; index < string_length * 2; index++) {
        uint8_t expected = index % 2 ? 0 : (uint8_t)trampoline_string[index / 2];
        if (trampoline_offset + index >= image->length) break;
        check(image->bytes[trampoline_offset + index] == expected, "0x%08x does not hold the expected debug string", TRAMPOLINE_ADDRESS);
    }
    uint8_t trampolines[28];
    write_u32(trampolines, 0, 0xe3a00001u);
    write_u32(trampolines, 4, 0xe3a01000u);
    write_u32(trampolines, 8, branch(tear_on_tail + 8, PANEL_WRITE, 0));
    write_u32(trampolines, 12, LOAD_PC_FROM_NEXT_WORD);
    write_u32(trampolines, 16, hook_address);
    write_u32(trampolines, 20, LOAD_PC_FROM_NEXT_WORD);
    write_u32(trampolines, 24, hook_address + REFRESH_ENTRY_OFFSET);
    check(trampoline_offset + (int64_t)sizeof(trampolines) <= image->length, "Replacement must be the same size as the original");
    patch_records(text, trampoline_offset, image->bytes + trampoline_offset, sizeof(trampolines), trampolines, sizeof(trampolines));
}
