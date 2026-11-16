#include "internal.h"

#define PAK_MAGIC "NKGENPAK"
#define PAK_CHECKSUM_START 20u
#define PAK_HEADER_SIZE 80u
#define IMAGE_RECORD_SIZE 48u
#define PROFILE_RECORD_SIZE 16u
#define OPERATION_RECORD_SIZE 16u
#define BLOB_RECORD_SIZE 12u
#define NO_ORDINAL 0xffffffffu

static uint32_t pak_word(const struct pak *pak, uint64_t offset)
{
    if (offset + 4 > pak->length) fail(NKGEN_BAD_PAK, "Pak read at %llu is outside the pak", (unsigned long long)offset);
    return get_u32(pak->data + offset);
}

static void check_table(const struct pak *pak, const char *name, uint32_t offset, uint32_t count, uint32_t record_size)
{
    if ((uint64_t)offset + (uint64_t)count * record_size > pak->length || offset % 4) {
        fail(NKGEN_BAD_PAK, "Pak %s table is outside the pak", name);
    }
}

static const char *pak_string(const struct pak *pak, uint32_t offset)
{
    if (offset < pak->strings_offset || offset >= pak->strings_offset + pak->strings_length) {
        fail(NKGEN_BAD_PAK, "Pak string offset %u is outside the string table", offset);
    }
    return (const char *)pak->data + offset;
}

void pak_open(struct pak *pak, const uint8_t *data, uint32_t length)
{
    fill_bytes(pak, 0, sizeof(*pak));
    pak->data = data;
    pak->length = length;
    if (length < PAK_HEADER_SIZE || !bytes_equal(data, PAK_MAGIC, 8)) fail(NKGEN_BAD_PAK, "Not a generator pak (no NKGENPAK magic)");
    if (pak_word(pak, 8) != NKGEN_PAK_FORMAT) fail(NKGEN_BAD_PAK, "Pak format %u is not %u", pak_word(pak, 8), NKGEN_PAK_FORMAT);
    if (pak_word(pak, 12) != length) fail(NKGEN_BAD_PAK, "Pak length %u differs from the file's %u", pak_word(pak, 12), length);
    pak->crc32 = pak_word(pak, 16);
    if (crc32(data + PAK_CHECKSUM_START, length - PAK_CHECKSUM_START) != pak->crc32) fail(NKGEN_BAD_PAK, "Pak CRC-32 does not match");
    pak->strings_offset = pak_word(pak, 24);
    pak->strings_length = pak_word(pak, 28);
    pak->images_offset = pak_word(pak, 32);
    pak->image_count = pak_word(pak, 36);
    pak->profiles_offset = pak_word(pak, 40);
    pak->profile_count = pak_word(pak, 44);
    pak->operations_offset = pak_word(pak, 48);
    pak->operation_count = pak_word(pak, 52);
    pak->blobs_offset = pak_word(pak, 56);
    pak->blob_count = pak_word(pak, 60);
    pak->bstart_blob = pak_word(pak, 64);
    pak->skip_list_blob = pak_word(pak, 68);
    pak->shared_first_operation = pak_word(pak, 72);
    pak->shared_operation_count = pak_word(pak, 76);
    if ((uint64_t)pak->strings_offset + pak->strings_length > length || !pak->strings_length || data[pak->strings_offset + pak->strings_length - 1] != 0) {
        fail(NKGEN_BAD_PAK, "Pak string table is outside the pak or unterminated");
    }
    pak->release = pak_string(pak, pak_word(pak, 20));
    check_table(pak, "image", pak->images_offset, pak->image_count, IMAGE_RECORD_SIZE);
    check_table(pak, "profile", pak->profiles_offset, pak->profile_count, PROFILE_RECORD_SIZE);
    check_table(pak, "operation", pak->operations_offset, pak->operation_count, OPERATION_RECORD_SIZE);
    check_table(pak, "blob", pak->blobs_offset, pak->blob_count, BLOB_RECORD_SIZE);
    for (uint32_t index = 0; index < pak->blob_count; index++) {
        uint32_t base = pak->blobs_offset + index * BLOB_RECORD_SIZE;
        uint32_t offset = pak_word(pak, base);
        uint32_t blob_length = pak_word(pak, base + 4);
        if ((uint64_t)offset + blob_length > length) fail(NKGEN_BAD_PAK, "Pak blob %u is outside the pak", index);
        if (crc32(data + offset, blob_length) != pak_word(pak, base + 8)) fail(NKGEN_BAD_PAK, "Pak blob %u CRC-32 does not match", index);
    }
    for (uint32_t index = 0; index < pak->operation_count; index++) {
        struct pak_operation operation = pak_operation(pak, index);
        if (operation.kind < PAK_MODULE_FILE || operation.kind > PAK_CARD_FILE_ROM) {
            fail(NKGEN_BAD_PAK, "Pak operation %u has unknown kind %u", index, operation.kind);
        }
    }
    if ((uint64_t)pak->shared_first_operation + pak->shared_operation_count > pak->operation_count) {
        fail(NKGEN_BAD_PAK, "Pak shared operations are out of range");
    }
    for (uint32_t index = 0; index < pak->shared_operation_count; index++) {
        struct pak_operation operation = pak_operation(pak, pak->shared_first_operation + index);
        if (operation.kind != PAK_CARD_FILE && operation.kind != PAK_CARD_FILE_ROM) {
            fail(NKGEN_BAD_PAK, "Pak shared operation %s is not a card file", operation.name);
        }
    }
    for (uint32_t index = 0; index < pak->profile_count; index++) pak_profile(pak, index);
    for (uint32_t index = 0; index < pak->image_count; index++) pak_image(pak, index);
    pak_blob(pak, pak->bstart_blob);
    pak_blob(pak, pak->skip_list_blob);
}

struct pak_image pak_image(const struct pak *pak, uint32_t index)
{
    uint32_t base = pak->images_offset + index * IMAGE_RECORD_SIZE;
    struct pak_image image;
    image.sha256 = pak->data + base;
    image.length = pak_word(pak, base + 32);
    image.profile = pak_word(pak, base + 36);
    image.device = pak_string(pak, pak_word(pak, base + 40));
    image.description = pak_string(pak, pak_word(pak, base + 44));
    if (image.profile >= pak->profile_count) fail(NKGEN_BAD_PAK, "Pak image %u names profile %u", index, image.profile);
    return image;
}

struct pak_profile pak_profile(const struct pak *pak, uint32_t index)
{
    uint32_t base = pak->profiles_offset + index * PROFILE_RECORD_SIZE;
    struct pak_profile profile;
    profile.id = pak_string(pak, pak_word(pak, base));
    profile.first_operation = pak_word(pak, base + 4);
    profile.operation_count = pak_word(pak, base + 8);
    if ((uint64_t)profile.first_operation + profile.operation_count > pak->operation_count) {
        fail(NKGEN_BAD_PAK, "Pak profile %u operations are out of range", index);
    }
    return profile;
}

struct pak_operation pak_operation(const struct pak *pak, uint32_t index)
{
    uint32_t base = pak->operations_offset + index * OPERATION_RECORD_SIZE;
    struct pak_operation operation;
    operation.kind = pak_word(pak, base);
    operation.name = pak_string(pak, pak_word(pak, base + 4));
    operation.blob = pak_word(pak, base + 8);
    if (operation.blob >= pak->blob_count) fail(NKGEN_BAD_PAK, "Pak operation %u names blob %u", index, operation.blob);
    return operation;
}

struct pak_blob pak_blob(const struct pak *pak, uint32_t index)
{
    if (index >= pak->blob_count) fail(NKGEN_BAD_PAK, "Pak blob %u does not exist", index);
    uint32_t base = pak->blobs_offset + index * BLOB_RECORD_SIZE;
    struct pak_blob blob = { pak->data + pak_word(pak, base), pak_word(pak, base + 4) };
    return blob;
}

struct blob_reader {
    const struct pak *pak;
    struct pak_blob blob;
    uint32_t index;
};

static struct blob_reader blob_reader(const struct pak *pak, uint32_t index)
{
    struct blob_reader reader = { pak, pak_blob(pak, index), index };
    return reader;
}

static uint32_t blob_word(const struct blob_reader *reader, uint64_t offset)
{
    if (offset + 4 > reader->blob.length) fail(NKGEN_BAD_PAK, "Pak blob %u is malformed", reader->index);
    return get_u32(reader->blob.data + offset);
}

static const char *blob_string(const struct blob_reader *reader, uint64_t offset)
{
    return pak_string(reader->pak, blob_word(reader, offset));
}

static void check_count(const struct blob_reader *reader, uint32_t count, uint32_t record_size)
{
    if ((uint64_t)count * record_size > reader->blob.length) fail(NKGEN_BAD_PAK, "Pak blob %u is malformed", reader->index);
}

void pak_skip_list(const struct pak *pak, struct skip_list *skip_list)
{
    struct blob_reader reader = blob_reader(pak, pak->skip_list_blob);
    skip_list->minimum_span = blob_word(&reader, 0);
    skip_list->module_count = blob_word(&reader, 4);
    skip_list->file_count = blob_word(&reader, 8);
    skip_list->tested_count = blob_word(&reader, 12);
    check_count(&reader, skip_list->module_count, 4);
    check_count(&reader, skip_list->file_count, 4);
    check_count(&reader, skip_list->tested_count, 32);
    skip_list->modules = arena_allocate(skip_list->module_count * sizeof(char *));
    skip_list->files = arena_allocate(skip_list->file_count * sizeof(char *));
    for (uint32_t index = 0; index < skip_list->module_count; index++) skip_list->modules[index] = blob_string(&reader, 16 + (uint64_t)index * 4);
    uint64_t files = 16 + (uint64_t)skip_list->module_count * 4;
    for (uint32_t index = 0; index < skip_list->file_count; index++) skip_list->files[index] = blob_string(&reader, files + (uint64_t)index * 4);
    uint64_t tested = files + (uint64_t)skip_list->file_count * 4;
    if (tested + (uint64_t)skip_list->tested_count * 32 > reader.blob.length) fail(NKGEN_BAD_PAK, "Pak blob %u is malformed", reader.index);
    skip_list->tested = reader.blob.data + tested;
}

void pak_rom_spec(const struct pak *pak, uint32_t blob, struct rom_spec *spec)
{
    struct blob_reader reader = blob_reader(pak, blob);
    spec->module = blob_string(&reader, 0);
    spec->spare_sections = blob_word(&reader, 4);
    spec->redirect_count = blob_word(&reader, 8);
    spec->patch_count = blob_word(&reader, 12);
    check_count(&reader, spec->redirect_count, 16);
    check_count(&reader, spec->patch_count, 20);
    if (spec->spare_sections > 64) fail(NKGEN_BAD_PAK, "Pak blob %u asks for %u spare sections", blob, spec->spare_sections);
    struct import_redirect *redirects = arena_allocate(spec->redirect_count * sizeof(*redirects));
    uint64_t import_start = 16 + (uint64_t)spec->redirect_count * 16;
    uint64_t import_total = 0;
    for (uint32_t index = 0; index < spec->redirect_count; index++) {
        uint64_t base = 16 + (uint64_t)index * 16;
        redirects[index].from = blob_string(&reader, base);
        redirects[index].to = blob_string(&reader, base + 4);
        uint32_t first = blob_word(&reader, base + 8);
        redirects[index].count = blob_word(&reader, base + 12);
        check_count(&reader, redirects[index].count, 12);
        struct import_redirect_entry *imports = arena_allocate(redirects[index].count * sizeof(*imports));
        for (uint32_t entry = 0; entry < redirects[index].count; entry++) {
            uint64_t record = import_start + ((uint64_t)first + entry) * 12;
            uint32_t ordinal = blob_word(&reader, record);
            imports[entry].by_ordinal = ordinal != NO_ORDINAL;
            imports[entry].ordinal = ordinal;
            imports[entry].key = imports[entry].by_ordinal ? 0 : blob_string(&reader, record + 4);
            imports[entry].name = blob_string(&reader, record + 8);
        }
        redirects[index].imports = imports;
        import_total += redirects[index].count;
    }
    struct byte_patch *patches = arena_allocate(spec->patch_count * sizeof(*patches));
    uint64_t patch_start = import_start + import_total * 12;
    for (uint32_t index = 0; index < spec->patch_count; index++) {
        uint64_t base = patch_start + (uint64_t)index * 20;
        patches[index].offset = blob_word(&reader, base);
        patches[index].length = blob_word(&reader, base + 4);
        uint32_t original = blob_word(&reader, base + 8);
        uint32_t replacement = blob_word(&reader, base + 12);
        if ((uint64_t)original + patches[index].length > reader.blob.length || (uint64_t)replacement + patches[index].length > reader.blob.length) {
            fail(NKGEN_BAD_PAK, "Pak blob %u is malformed", blob);
        }
        patches[index].original = reader.blob.data + original;
        patches[index].replacement = reader.blob.data + replacement;
        patches[index].purpose = blob_string(&reader, base + 16);
    }
    spec->redirects = redirects;
    spec->patches = patches;
}

uint32_t pak_registry(const struct pak *pak, uint32_t blob, const struct pak_image *image, struct buffer *entries)
{
    struct blob_reader reader = blob_reader(pak, blob);
    uint32_t count = blob_word(&reader, 0);
    check_count(&reader, count, 28);
    for (uint32_t index = 0; index < count; index++) {
        uint64_t base = 4 + (uint64_t)index * 28;
        struct registry_entry *entry = buffer_push(entries, sizeof(struct registry_entry));
        entry->kind = blob_word(&reader, base);
        entry->hive = blob_word(&reader, base + 4);
        entry->key = blob_string(&reader, base + 8);
        if (entry->kind == REGISTRY_KEY) continue;
        if (entry->kind != REGISTRY_VALUE) fail(NKGEN_BAD_PAK, "Pak blob %u has registry entry kind %u", blob, entry->kind);
        entry->name = blob_string(&reader, base + 12);
        entry->type = blob_word(&reader, base + 16);
        uint32_t value = blob_word(&reader, base + 20);
        uint32_t source = blob_word(&reader, base + 24);
        if (entry->type == 4) {
            entry->dword = value;
        } else if (entry->type == 1) {
            if (source == REGISTRY_DEVICE) entry->string = image->device;
            else if (source == REGISTRY_DESCRIPTION) entry->string = image->description;
            else entry->string = pak_string(pak, value);
        } else {
            fail(NKGEN_BAD_PAK, "Pak blob %u has registry type %u", blob, entry->type);
        }
    }
    return count;
}

void pak_word_patch(const struct pak *pak, uint32_t blob, struct word_patch *patch)
{
    struct blob_reader reader = blob_reader(pak, blob);
    patch->module = blob_string(&reader, 0);
    patch->count = blob_word(&reader, 4);
    check_count(&reader, patch->count, 16);
    struct word_change *words = arena_allocate(patch->count * sizeof(*words));
    for (uint32_t index = 0; index < patch->count; index++) {
        uint64_t base = 8 + (uint64_t)index * 16;
        words[index].address = blob_word(&reader, base);
        words[index].original = blob_word(&reader, base + 4);
        words[index].replacement = blob_word(&reader, base + 8);
        words[index].purpose = blob_string(&reader, base + 12);
    }
    patch->words = words;
}
