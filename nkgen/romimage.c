#include "internal.h"

int is_rom_image(const uint8_t *bytes, uint32_t length)
{
    return length > 0x44 && bytes_equal(bytes + 0x40, "ECEC", 4);
}

uint32_t image_identity(const uint8_t *bytes, uint32_t length)
{
    check(is_rom_image(bytes, length), "Missing ECEC signature: not a Windows CE ROM image");
    int64_t offset = (int64_t)read_u32(bytes, length, 0x44) - IMAGE_BASE;
    check(offset >= 0 && offset + ROM_HEADER_SIZE <= (int64_t)length, "ROM header is outside the image");
    uint64_t range = ROM_HEADER_SIZE + (uint64_t)read_u32(bytes, length, offset + 16) * TOC_ENTRY_SIZE
        + (uint64_t)read_u32(bytes, length, offset + 48) * FILES_ENTRY_SIZE;
    check(range <= IDENTITY_LIMIT && (uint64_t)offset + range <= length,
        "ROM header and tables (%llu bytes) are over %u bytes or run past the image", (unsigned long long)range, IDENTITY_LIMIT);
    return crc32(bytes + offset, (uint32_t)range);
}

uint32_t image_offset_of(const struct rom_image *image, uint64_t va, uint64_t length)
{
    int64_t offset = (int64_t)va - IMAGE_BASE;
    check(offset >= 0 && (uint64_t)offset + length <= image->length, "VA 0x%08llx (+%llu) outside image",
        (unsigned long long)va, (unsigned long long)length);
    return (uint32_t)offset;
}

const char *image_c_string(const struct rom_image *image, uint32_t va)
{
    uint32_t start = image_offset_of(image, va, 1);
    int64_t end = index_of_byte(image->bytes, image->length, 0, start);
    check(end >= 0, "Unterminated string at 0x%08x", va);
    return (const char *)image->bytes + start;
}

void open_image(struct rom_image *image, const uint8_t *bytes, uint32_t length)
{
    check(is_rom_image(bytes, length), "Missing ECEC signature: not a Windows CE ROM image");
    fill_bytes(image, 0, sizeof(*image));
    image->bytes = bytes;
    image->length = length;
    image->rom_header_va = read_u32(bytes, length, 0x44);
    uint32_t base = image_offset_of(image, image->rom_header_va, ROM_HEADER_SIZE);
    struct rom_header *header = &image->header;
    image->header_offset = base;
    header->dllfirst = read_u32(bytes, length, base);
    header->dlllast = read_u32(bytes, length, base + 4);
    header->physfirst = read_u32(bytes, length, base + 8);
    header->physlast = read_u32(bytes, length, base + 12);
    header->nummods = read_u32(bytes, length, base + 16);
    header->ram_start = read_u32(bytes, length, base + 20);
    header->ram_free = read_u32(bytes, length, base + 24);
    header->ram_end = read_u32(bytes, length, base + 28);
    header->copy_entries = read_u32(bytes, length, base + 32);
    header->copy_offset = read_u32(bytes, length, base + 36);
    header->numfiles = read_u32(bytes, length, base + 48);
    header->cpu_type = read_u16(bytes, length, base + 68);
    check(header->physfirst == IMAGE_BASE, "Image is not based at 0x80200000");
}

uint32_t module_table_va(const struct rom_image *image)
{
    return image->rom_header_va + ROM_HEADER_SIZE;
}

static uint64_t file_table_va(const struct rom_image *image)
{
    return (uint64_t)module_table_va(image) + (uint64_t)image->header.nummods * TOC_ENTRY_SIZE;
}

uint32_t image_module_count(const struct rom_image *image)
{
    return image->header.nummods;
}

uint32_t image_file_count(const struct rom_image *image)
{
    return image->header.numfiles;
}

struct rom_module *image_modules(struct rom_image *image)
{
    if (image->modules_listed) return image->modules;
    const uint8_t *bytes = image->bytes;
    uint32_t length = image->length;
    uint32_t count = image->header.nummods;
    check((uint64_t)count * TOC_ENTRY_SIZE <= length, "VA 0x%08x (+%u) outside image", module_table_va(image), TOC_ENTRY_SIZE);
    struct rom_module *modules = arena_allocate(count * sizeof(*modules));
    for (uint32_t index = 0; index < count; index++) {
        struct rom_module *module = &modules[index];
        uint32_t toc = image_offset_of(image, (uint64_t)module_table_va(image) + (uint64_t)index * TOC_ENTRY_SIZE, TOC_ENTRY_SIZE);
        uint32_t e32_va = read_u32(bytes, length, toc + 20);
        uint32_t o32_va = read_u32(bytes, length, toc + 24);
        uint32_t e32 = image_offset_of(image, e32_va, E32_SIZE);
        uint32_t object_count = read_u16(bytes, length, e32);
        module->sections = arena_allocate(object_count * sizeof(struct rom_section));
        module->section_count = object_count;
        for (uint32_t section_index = 0; section_index < object_count; section_index++) {
            struct rom_section *section = &module->sections[section_index];
            uint32_t o32 = image_offset_of(image, (uint64_t)o32_va + (uint64_t)section_index * O32_ENTRY_SIZE, O32_ENTRY_SIZE);
            section->virtual_size = read_u32(bytes, length, o32);
            section->rva = read_u32(bytes, length, o32 + 4);
            section->physical_size = read_u32(bytes, length, o32 + 8);
            section->data_pointer = read_u32(bytes, length, o32 + 12);
            section->data_offset = section->physical_size ? (int64_t)image_offset_of(image, section->data_pointer, section->physical_size) : NO_OFFSET;
            section->real_address = read_u32(bytes, length, o32 + 16);
            section->flags = read_u32(bytes, length, o32 + 20);
            section->compressed = (section->flags & COMPRESSED_SECTION_FLAG) != 0;
        }
        uint32_t name_va = read_u32(bytes, length, toc + 16);
        module->name = image_c_string(image, name_va);
        module->name_offset = image_offset_of(image, name_va, 1);
        module->toc_offset = toc;
        module->e32_offset = e32;
        module->o32_offset = image_offset_of(image, o32_va, (uint64_t)(object_count ? object_count : 1) * O32_ENTRY_SIZE);
    }
    image->modules = modules;
    image->modules_listed = 1;
    return modules;
}

struct rom_file *image_files(struct rom_image *image)
{
    if (image->files_listed) return image->files;
    const uint8_t *bytes = image->bytes;
    uint32_t length = image->length;
    uint32_t count = image->header.numfiles;
    check((uint64_t)count * FILES_ENTRY_SIZE <= length, "VA 0x%08llx (+%u) outside image", (unsigned long long)file_table_va(image), FILES_ENTRY_SIZE);
    struct rom_file *files = arena_allocate(count * sizeof(*files));
    for (uint32_t index = 0; index < count; index++) {
        struct rom_file *file = &files[index];
        uint32_t entry = image_offset_of(image, file_table_va(image) + (uint64_t)index * FILES_ENTRY_SIZE, FILES_ENTRY_SIZE);
        file->entry_offset = entry;
        file->attributes = read_u32(bytes, length, entry);
        file->real_size = read_u32(bytes, length, entry + 12);
        file->compressed_size = read_u32(bytes, length, entry + 16);
        uint32_t load_va = read_u32(bytes, length, entry + 24);
        uint32_t name_va = read_u32(bytes, length, entry + 20);
        file->name = image_c_string(image, name_va);
        file->name_offset = image_offset_of(image, name_va, 1);
        file->load_offset = image_offset_of(image, load_va, file->compressed_size ? file->compressed_size : 1);
        file->compressed = (file->attributes & COMPRESSED_FILE_ATTRIBUTE) != 0;
    }
    image->files = files;
    image->files_listed = 1;
    return files;
}

struct rom_module *find_module(struct rom_image *image, const char *name)
{
    struct rom_module *modules = image_modules(image);
    for (uint32_t index = 0; index < image->header.nummods; index++) {
        if (names_equal(modules[index].name, name)) return &modules[index];
    }
    return 0;
}

struct rom_file *find_file(struct rom_image *image, const char *name)
{
    struct rom_file *files = image_files(image);
    for (uint32_t index = 0; index < image->header.numfiles; index++) {
        if (names_equal(files[index].name, name)) return &files[index];
    }
    return 0;
}

struct rom_module *find_module_or_fail(struct rom_image *image, const char *name)
{
    struct rom_module *module = find_module(image, name);
    check(module, "%s is not a ROM module", name);
    return module;
}

struct rom_file *find_file_or_fail(struct rom_image *image, const char *name)
{
    struct rom_file *file = find_file(image, name);
    check(file, "%s is not a ROM file", name);
    return file;
}

const uint8_t *read_file_contents(struct rom_image *image, struct rom_file *file)
{
    check(!file->compressed, "%s is compressed; LZX is not supported by the generator", file->name);
    check(file->compressed_size == file->real_size, "%s stored size differs from its size", file->name);
    return image->bytes + file->load_offset;
}

static void add_region(struct buffer *regions, int64_t start, int64_t end, uint32_t kind, uint32_t index, uint32_t section)
{
    struct region *region = buffer_push(regions, sizeof(struct region));
    region->start = start;
    region->end = end;
    region->kind = kind;
    region->index = index;
    region->section = section;
}

static int compare_regions(const void *first, const void *second)
{
    const struct region *a = first;
    const struct region *b = second;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    if (a->end != b->end) return a->end < b->end ? -1 : 1;
    return 0;
}

struct region *list_used_regions(struct rom_image *image, uint32_t *count)
{
    struct buffer regions = { 0 };
    const uint8_t *bytes = image->bytes;
    uint32_t length = image->length;
    add_region(&regions, image->header_offset, image->header_offset + ROM_HEADER_SIZE, REGION_ROM_HEADER, 0, 0);
    int64_t toc_start = image_offset_of(image, module_table_va(image), 1);
    int64_t toc_end = image_offset_of(image, file_table_va(image), 1) + (int64_t)image->header.numfiles * FILES_ENTRY_SIZE;
    add_region(&regions, toc_start, toc_end, REGION_MODULE_TOC, 0, 0);
    for (uint32_t index = 0; index < image->header.copy_entries; index++) {
        uint32_t entry = image_offset_of(image, (uint64_t)image->header.copy_offset + (uint64_t)index * COPY_ENTRY_SIZE, COPY_ENTRY_SIZE);
        uint32_t source = read_u32(bytes, length, entry);
        uint32_t copy_length = read_u32(bytes, length, entry + 8);
        uint32_t source_offset = image_offset_of(image, source, copy_length ? copy_length : 1);
        add_region(&regions, entry, entry + COPY_ENTRY_SIZE, REGION_COPY_ENTRY, index, 0);
        add_region(&regions, source_offset, (int64_t)source_offset + copy_length, REGION_COPY_SOURCE, index, 0);
    }
    struct rom_module *modules = image_modules(image);
    for (uint32_t index = 0; index < image->header.nummods; index++) {
        struct rom_module *module = &modules[index];
        uint32_t name_length = text_length(module->name);
        add_region(&regions, module->e32_offset, module->e32_offset + E32_SIZE, REGION_MODULE_E32, index, 0);
        add_region(&regions, module->o32_offset, module->o32_offset + (int64_t)module->section_count * O32_ENTRY_SIZE, REGION_MODULE_O32, index, 0);
        add_region(&regions, module->name_offset, (int64_t)module->name_offset + name_length + 1, REGION_MODULE_NAME, index, 0);
        for (uint32_t section = 0; section < module->section_count; section++) {
            struct rom_section *data = &module->sections[section];
            if (data->data_offset == NO_OFFSET) continue;
            add_region(&regions, data->data_offset, data->data_offset + data->physical_size, REGION_MODULE_SECTION, index, section);
        }
    }
    struct rom_file *files = image_files(image);
    for (uint32_t index = 0; index < image->header.numfiles; index++) {
        struct rom_file *file = &files[index];
        add_region(&regions, file->load_offset, (int64_t)file->load_offset + file->compressed_size, REGION_FILE_DATA, index, 0);
        add_region(&regions, file->name_offset, (int64_t)file->name_offset + text_length(file->name) + 1, REGION_FILE_NAME, index, 0);
    }
    *count = regions.length / sizeof(struct region);
    stable_sort(regions.data, *count, sizeof(struct region), compare_regions);
    return buffer_items(&regions);
}

char *region_name(struct rom_image *image, const struct region *region)
{
    switch (region->kind) {
    case REGION_ROM_HEADER: return text_copy("ROMHDR");
    case REGION_MODULE_TOC: return text_copy("module TOC");
    case REGION_COPY_ENTRY: return text_format("copy entry %u", region->index);
    case REGION_COPY_SOURCE: return text_format("copy source %u", region->index);
    case REGION_MODULE_E32: return text_format("%s e32", image->modules[region->index].name);
    case REGION_MODULE_O32: return text_format("%s o32", image->modules[region->index].name);
    case REGION_MODULE_NAME: return text_format("%s name", image->modules[region->index].name);
    case REGION_MODULE_SECTION: return text_format("%s section %u", image->modules[region->index].name, region->section);
    case REGION_FILE_DATA: return text_copy(image->files[region->index].name);
    case REGION_FILE_NAME: return text_format("%s name", image->files[region->index].name);
    default: return text_copy(image->modules[region->index].name);
    }
}
