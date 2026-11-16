#include "internal.h"

#define MAXIMUM_RECORD_BYTES 64u

void patch_records(struct buffer *text, int64_t image_offset, const uint8_t *original, uint32_t original_length, const uint8_t *replacement, uint32_t replacement_length)
{
    check(replacement_length == original_length, "Replacement must be the same size as the original");
    uint32_t index = 0;
    while (index < original_length) {
        if (original[index] == replacement[index]) {
            index++;
            continue;
        }
        uint32_t start = index;
        while (index < original_length && original[index] != replacement[index] && index - start < MAXIMUM_RECORD_BYTES) index++;
        buffer_format(text, "%llx ", (unsigned long long)(image_offset + start));
        buffer_hex(text, original + start, index - start);
        buffer_text(text, " ");
        buffer_hex(text, replacement + start, index - start);
        buffer_text(text, "\n");
    }
}

void patch_section_start(struct buffer *text, const char *name, uint32_t image_length, uint32_t extended_length, int first)
{
    if (!first) buffer_text(text, "\n");
    buffer_format(text, "patch %s\n", name);
    if (extended_length) buffer_format(text, "image %x %x\n", image_length, extended_length);
    else buffer_format(text, "image %x\n", image_length);
}

void patch_load_line(struct buffer *text, uint32_t offset, uint32_t length, uint32_t crc, const char *path)
{
    buffer_format(text, "load %x %x %x %s\n", offset, length, crc, path);
}

void word_patch_records(struct buffer *text, struct rom_image *image, const struct word_patch *patch)
{
    struct rom_module *module = find_module(image, patch->module);
    check(module, "%s not found", patch->module);
    for (uint32_t index = 0; index < patch->count; index++) {
        const struct word_change *word = &patch->words[index];
        check(word->address % 4 == 0, "0x%08x is not word aligned", word->address);
        struct rom_section *section = 0;
        for (uint32_t candidate = 0; candidate < module->section_count; candidate++) {
            struct rom_section *data = &module->sections[candidate];
            if (data->data_offset != NO_OFFSET && word->address >= data->real_address
                && (uint64_t)word->address + 4 <= (uint64_t)data->real_address + data->virtual_size) {
                section = data;
                break;
            }
        }
        check(section, "0x%08x (%s) is outside %s's sections", word->address, word->purpose, patch->module);
        check(!section->compressed, "0x%08x (%s) is in a compressed section", word->address, word->purpose);
        int64_t offset = section->data_offset + word->address - section->real_address;
        uint32_t actual = read_u32(image->bytes, image->length, offset);
        check(actual == word->original, "0x%08x (%s) is 0x%08x, expected 0x%08x", word->address, word->purpose, actual, word->original);
        uint8_t replacement[4];
        write_u32(replacement, 0, word->replacement);
        patch_records(text, offset, image->bytes + offset, 4, replacement, 4);
    }
}
