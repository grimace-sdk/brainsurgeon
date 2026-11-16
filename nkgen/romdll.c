#include "internal.h"

#define LDR_LITERAL_MASK 0x0f7f0000u
#define LDR_LITERAL_VALUE 0x051f0000u
#define FILE_ALIGNMENT 0x200
#define PAGE_SIZE 0x1000
#define UNIT_COUNT 9u
#define CODE_FLAGS 0x20000020u
#define RELOC_FLAGS 0x42000040u

struct unit {
    uint32_t rva;
    uint32_t size;
};

struct dll_section {
    uint32_t virtual_size;
    uint32_t rva;
    uint32_t raw_size;
    uint32_t data_pointer;
    uint32_t real_address;
    uint32_t flags;
    int code;
    int moved;
    const char *name;
    const uint8_t *data;
    uint32_t data_length;
    uint32_t file_size;
    uint32_t file_pointer;
};

struct range {
    int64_t start;
    int64_t end;
};

struct memory {
    uint8_t *bytes;
    uint32_t length;
};

struct descriptor {
    uint32_t lookup;
    uint32_t first_thunk;
};

static uint32_t memory_u32(const struct memory *memory, int64_t offset)
{
    return read_u32(memory->bytes, memory->length, offset);
}

static int compare_ranges(const void *first, const void *second)
{
    const struct range *a = first;
    const struct range *b = second;
    if (a->start == b->start) return 0;
    return a->start < b->start ? -1 : 1;
}

static int compare_words(const void *first, const void *second)
{
    uint32_t a = *(const uint32_t *)first;
    uint32_t b = *(const uint32_t *)second;
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

static int inside_ranges(const struct range *ranges, uint32_t count, int64_t rva)
{
    int64_t low = 0;
    int64_t high = (int64_t)count - 1;
    while (low <= high) {
        int64_t middle = (low + high) / 2;
        if (rva < ranges[middle].start) high = middle - 1;
        else if (rva >= ranges[middle].end) low = middle + 1;
        else return 1;
    }
    return 0;
}

static uint32_t list_descriptors(const struct memory *memory, const struct unit *imports, struct buffer *descriptors)
{
    descriptors->length = 0;
    for (int64_t descriptor = imports->rva; imports->rva && descriptor + 20 <= (int64_t)imports->rva + imports->size; descriptor += 20) {
        if (!memory_u32(memory, descriptor + 12)) break;
        struct descriptor *entry = buffer_push(descriptors, sizeof(struct descriptor));
        entry->lookup = memory_u32(memory, descriptor);
        entry->first_thunk = memory_u32(memory, descriptor + 16);
    }
    return descriptors->length / sizeof(struct descriptor);
}

static const char *section_name(const struct dll_section *section, const struct unit *units)
{
    if (section->rva == units[2].rva) return ".rsrc";
    if (section->rva == units[3].rva) return ".pdata";
    if (section->flags & 0x20) return ".text";
    if (section->flags & 0x80000000u) return ".data";
    return ".rdata";
}

static struct range *code_ranges_from_pdata(const struct memory *memory, struct range pdata, uint32_t base, uint32_t *count)
{
    struct buffer ranges = { 0 };
    for (int64_t offset = 0; offset + 8 <= pdata.end - pdata.start; offset += 8) {
        uint32_t begin = memory_u32(memory, pdata.start + offset);
        uint32_t flags = memory_u32(memory, pdata.start + offset + 4);
        uint32_t length = ((flags >> 8) & 0x3fffff) * 4;
        if (begin < base || !length) continue;
        struct range *range = buffer_push(&ranges, sizeof(struct range));
        range->start = (int64_t)begin - base;
        range->end = (int64_t)begin - base + length;
    }
    *count = ranges.length / sizeof(struct range);
    stable_sort(ranges.data, *count, sizeof(struct range), compare_ranges);
    return buffer_items(&ranges);
}

static int excluded(const struct range *exclusions, uint32_t count, int64_t rva)
{
    for (uint32_t index = 0; index < count; index++) {
        if (rva >= exclusions[index].start && rva < exclusions[index].end) return 1;
    }
    return 0;
}

static int64_t scan_end(const struct dll_section *section)
{
    return (int64_t)section->rva + align_up(section->raw_size, 4);
}

static uint32_t *recover_relocations(const char *module_name, const struct memory *memory, uint32_t base, uint32_t size_of_image,
    const struct dll_section *sections, uint32_t section_count, const struct range *code_ranges, uint32_t code_range_count,
    const struct range *exclusions, uint32_t exclusion_count, const struct range *pdata, uint32_t *relocation_count)
{
    uint32_t word_count = memory->length / 4;
    uint8_t *literal_targets = arena_allocate(word_count / 8 + 1);
    for (uint32_t index = 0; index < section_count; index++) {
        const struct dll_section *section = &sections[index];
        if (!section->code) continue;
        for (int64_t rva = section->rva; rva + 4 <= scan_end(section); rva += 4) {
            if (!inside_ranges(code_ranges, code_range_count, rva)) continue;
            uint32_t word = memory_u32(memory, rva);
            if ((word & LDR_LITERAL_MASK) != LDR_LITERAL_VALUE || word >> 28 == 0xf) continue;
            int64_t offset = word & 0xfff;
            int64_t target = rva + 8 + (word & 0x00800000u ? offset : -offset);
            if (target < 0 || target % 4 != 0 || target / 4 >= word_count) continue;
            literal_targets[target / 32] |= (uint8_t)(1u << ((target / 4) % 8));
        }
    }
    struct buffer relocations = { 0 };
    uint32_t pdata_relocations = 0;
    uint64_t image_end = (uint64_t)base + size_of_image;
    for (uint32_t index = 0; index < section_count; index++) {
        const struct dll_section *section = &sections[index];
        for (int64_t rva = section->rva; rva + 4 <= scan_end(section); rva += 4) {
            if (excluded(exclusions, exclusion_count, rva)) continue;
            uint32_t value = memory_u32(memory, rva);
            int in_image = value >= base && value < image_end;
            if (in_image && !(value & 0xffff)) {
                in_image = 0;
                for (uint32_t start = 0; start < section_count; start++) {
                    if ((uint64_t)base + sections[start].rva == value) in_image = 1;
                }
            }
            if (!in_image) continue;
            if (pdata && rva >= pdata->start && rva < pdata->end) {
                if ((rva - pdata->start) % 8 == 0) {
                    *(uint32_t *)buffer_push(&relocations, 4) = (uint32_t)rva;
                    pdata_relocations++;
                }
                continue;
            }
            if (section->code && inside_ranges(code_ranges, code_range_count, rva)) {
                if (rva % 4 == 0 && (literal_targets[rva / 32] & (1u << ((rva / 4) % 8)))) *(uint32_t *)buffer_push(&relocations, 4) = (uint32_t)rva;
                continue;
            }
            *(uint32_t *)buffer_push(&relocations, 4) = (uint32_t)rva;
        }
    }
    *relocation_count = relocations.length / 4;
    stable_sort(relocations.data, *relocation_count, 4, compare_words);
    int64_t pdata_size = pdata ? pdata->end - pdata->start : 0;
    check(pdata_size % 8 == 0 && pdata_relocations == pdata_size / 8, "%s: not every .pdata entry was relocated", module_name);
    return buffer_items(&relocations);
}

static struct buffer build_relocation_section(const uint32_t *relocations, uint32_t count)
{
    struct buffer section = { 0 };
    uint32_t index = 0;
    while (index < count) {
        uint32_t page = relocations[index] & ~(uint32_t)0xfff;
        uint32_t end = index;
        while (end < count && (relocations[end] & ~(uint32_t)0xfff) == page) end++;
        uint32_t entries = end - index;
        if (entries % 2) entries++;
        uint8_t *block = buffer_push(&section, 8 + entries * 2);
        write_u32(block, 0, page);
        write_u32(block, 4, 8 + entries * 2);
        for (uint32_t offset = index; offset < end; offset++) write_u16(block, 8 + (offset - index) * 2, 0x3000 | (relocations[offset] & 0xfff));
        index = end;
    }
    return section;
}

struct dll_bytes extract_rom_dll(struct rom_image *image, const char *module_name, uint32_t spare_sections)
{
    const uint8_t *bytes = image->bytes;
    uint32_t length = image->length;
    struct rom_module *module = find_module_or_fail(image, module_name);
    uint32_t e32 = module->e32_offset;
    uint32_t image_flags = read_u16(bytes, length, e32 + 2);
    uint32_t entry_rva = read_u32(bytes, length, e32 + 4);
    uint32_t virtual_base = read_u32(bytes, length, e32 + 8);
    uint32_t subsystem_major = read_u16(bytes, length, e32 + 12);
    uint32_t subsystem_minor = read_u16(bytes, length, e32 + 14);
    uint32_t stack_max = read_u32(bytes, length, e32 + 16);
    uint32_t virtual_size = read_u32(bytes, length, e32 + 20);
    uint32_t timestamp = read_u32(bytes, length, e32 + 32);
    struct unit units[UNIT_COUNT];
    for (uint32_t unit = 0; unit < UNIT_COUNT; unit++) {
        units[unit].rva = read_u32(bytes, length, e32 + 36 + unit * 8);
        units[unit].size = read_u32(bytes, length, e32 + 40 + unit * 8);
    }
    uint32_t subsystem = read_u16(bytes, length, e32 + 108);

    uint32_t section_count = module->section_count;
    struct dll_section *sections = arena_allocate((section_count + 1) * sizeof(struct dll_section));
    for (uint32_t index = 0; index < section_count; index++) {
        const struct rom_section *source = &module->sections[index];
        check(!source->compressed, "%s section %u is compressed", module_name, index);
        struct dll_section *section = &sections[index];
        section->virtual_size = source->virtual_size;
        section->rva = source->rva;
        section->raw_size = source->physical_size;
        section->data_pointer = source->data_pointer;
        section->real_address = source->real_address;
        section->flags = source->flags;
        section->code = (source->flags & CODE_FLAGS) != 0;
        section->moved = (uint64_t)source->real_address != (uint64_t)virtual_base + source->rva;
    }

    int64_t reloc_rva = align_up(virtual_size, PAGE_SIZE);
    check(reloc_rva <= 0x7fffffff, "%s is too large to extract", module_name);
    struct memory memory = { arena_allocate((uint32_t)reloc_rva), (uint32_t)reloc_rva };
    for (uint32_t index = 0; index < section_count; index++) {
        struct dll_section *section = &sections[index];
        if (!section->raw_size) continue;
        uint32_t start = image_offset_of(image, section->data_pointer, section->raw_size);
        check(section->rva <= memory.length, "%s section %u is outside the module", module_name, index);
        uint32_t copy_length = section->raw_size < memory.length - section->rva ? section->raw_size : memory.length - section->rva;
        copy_bytes(memory.bytes + section->rva, bytes + start, copy_length);
    }

    struct buffer descriptor_list = { 0 };
    uint32_t descriptor_count = list_descriptors(&memory, &units[1], &descriptor_list);
    struct descriptor *descriptors = buffer_items(&descriptor_list);
    for (uint32_t index = 0; index < descriptor_count; index++) {
        uint32_t lookup = descriptors[index].lookup;
        uint32_t first_thunk = descriptors[index].first_thunk;
        if (!lookup || lookup == first_thunk) continue;
        for (int64_t slot = 0; memory_u32(&memory, (int64_t)lookup + slot * 4); slot++) {
            write_u32_within(memory.bytes, memory.length, (int64_t)first_thunk + slot * 4, memory_u32(&memory, (int64_t)lookup + slot * 4));
        }
    }

    int any_moved = 0;
    for (uint32_t index = 0; index < section_count; index++) any_moved |= sections[index].moved;
    if (any_moved) {
        for (uint32_t index = 0; index < section_count; index++) {
            const struct dll_section *section = &sections[index];
            for (int64_t rva = section->rva; rva + 4 <= (int64_t)section->rva + section->raw_size; rva += 4) {
                uint32_t value = memory_u32(&memory, rva);
                for (uint32_t candidate = 0; candidate < section_count; candidate++) {
                    const struct dll_section *moved = &sections[candidate];
                    if (!moved->moved || value < moved->real_address || (uint64_t)value >= (uint64_t)moved->real_address + moved->virtual_size) continue;
                    write_u32(memory.bytes, (uint32_t)rva, virtual_base + moved->rva + (value - moved->real_address));
                    break;
                }
            }
        }
    }

    struct buffer exclusion_list = { 0 };
    const uint32_t excluded_units[4] = { 0, 1, 2, 6 };
    for (uint32_t index = 0; index < 4; index++) {
        const struct unit *unit = &units[excluded_units[index]];
        if (!unit->rva || !unit->size) continue;
        struct range *range = buffer_push(&exclusion_list, sizeof(struct range));
        range->start = unit->rva;
        range->end = (int64_t)unit->rva + unit->size;
    }
    descriptor_count = list_descriptors(&memory, &units[1], &descriptor_list);
    descriptors = buffer_items(&descriptor_list);
    for (uint32_t index = 0; index < descriptor_count; index++) {
        uint32_t tables[2] = { descriptors[index].lookup, descriptors[index].first_thunk };
        for (uint32_t table = 0; table < 2; table++) {
            if (!tables[table]) continue;
            int64_t end = tables[table];
            while (end + 4 <= memory.length && memory_u32(&memory, end)) end += 4;
            struct range *range = buffer_push(&exclusion_list, sizeof(struct range));
            range->start = tables[table];
            range->end = end + 4;
        }
    }
    struct range pdata = { units[3].rva, (int64_t)units[3].rva + units[3].size };
    int has_pdata = units[3].rva && units[3].size;
    uint32_t code_range_count = 0;
    struct range *code_ranges = has_pdata ? code_ranges_from_pdata(&memory, pdata, virtual_base, &code_range_count) : 0;
    uint32_t relocation_count = 0;
    uint32_t *relocations = recover_relocations(module_name, &memory, virtual_base, virtual_size, sections, section_count,
        code_ranges, code_range_count, buffer_items(&exclusion_list), exclusion_list.length / sizeof(struct range),
        has_pdata ? &pdata : 0, &relocation_count);
    struct buffer reloc_section = build_relocation_section(relocations, relocation_count);

    uint32_t all_count = section_count + 1;
    for (uint32_t index = 0; index < section_count; index++) {
        struct dll_section *section = &sections[index];
        section->name = section_name(section, units);
        int64_t data_end = (int64_t)section->rva + section->raw_size;
        if (data_end > memory.length) data_end = memory.length;
        section->data = memory.bytes + (section->rva < memory.length ? section->rva : memory.length);
        section->data_length = data_end > section->rva ? (uint32_t)(data_end - section->rva) : 0;
    }
    struct dll_section *reloc = &sections[section_count];
    reloc->name = ".reloc";
    reloc->rva = (uint32_t)reloc_rva;
    reloc->virtual_size = reloc_section.length;
    reloc->raw_size = reloc_section.length;
    reloc->flags = RELOC_FLAGS;
    reloc->data = reloc_section.data;
    reloc->data_length = reloc_section.length;

    int64_t header_size = align_up(0x40 + 4 + 20 + 224 + ((int64_t)all_count + spare_sections) * 40, FILE_ALIGNMENT);
    int64_t file_pointer = header_size;
    for (uint32_t index = 0; index < all_count; index++) {
        struct dll_section *section = &sections[index];
        section->file_size = (uint32_t)align_up(section->data_length, FILE_ALIGNMENT);
        section->file_pointer = section->file_size ? (uint32_t)file_pointer : 0;
        file_pointer += section->file_size;
    }
    check(file_pointer <= 0x7fffffff, "%s is too large to extract", module_name);

    uint32_t pe_length = (uint32_t)file_pointer;
    uint8_t *pe = arena_allocate(pe_length);
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32(pe, 0x3c, 0x40);
    pe[0x40] = 'P';
    pe[0x41] = 'E';
    const uint32_t file_header = 0x44;
    write_u16(pe, file_header, image->header.cpu_type);
    write_u16(pe, file_header + 2, all_count);
    write_u32(pe, file_header + 4, timestamp);
    write_u16(pe, file_header + 16, 224);
    write_u16(pe, file_header + 18, (image_flags & ~0x0001u) | 0x0102u);
    const uint32_t optional = file_header + 20;
    uint32_t code_size = 0;
    uint32_t data_size = 0;
    for (uint32_t index = 0; index < all_count; index++) {
        if (sections[index].flags & 0x20) code_size += sections[index].file_size;
        else data_size += sections[index].file_size;
    }
    write_u16(pe, optional, 0x10b);
    write_u32(pe, optional + 4, code_size);
    write_u32(pe, optional + 8, data_size);
    write_u32(pe, optional + 16, entry_rva);
    write_u32(pe, optional + 20, 0x1000);
    write_u32(pe, optional + 28, virtual_base);
    write_u32(pe, optional + 32, 0x1000);
    write_u32(pe, optional + 36, FILE_ALIGNMENT);
    write_u16(pe, optional + 40, 4);
    write_u16(pe, optional + 48, subsystem_major);
    write_u16(pe, optional + 50, subsystem_minor);
    write_u32(pe, optional + 56, (uint32_t)align_up(reloc_rva + reloc_section.length, PAGE_SIZE));
    write_u32(pe, optional + 60, (uint32_t)header_size);
    write_u16(pe, optional + 68, subsystem);
    write_u32(pe, optional + 72, stack_max);
    write_u32(pe, optional + 76, 0x1000);
    write_u32(pe, optional + 80, 0x100000);
    write_u32(pe, optional + 84, 0x1000);
    write_u32(pe, optional + 92, 16);
    const uint32_t directory_units[6] = { 0, 1, 2, 3, 4, 6 };
    for (uint32_t index = 0; index < 6; index++) {
        uint32_t unit = directory_units[index];
        write_u32(pe, optional + 96 + unit * 8, units[unit].rva);
        write_u32(pe, optional + 100 + unit * 8, units[unit].size);
    }
    write_u32(pe, optional + 96 + 5 * 8, (uint32_t)reloc_rva);
    write_u32(pe, optional + 100 + 5 * 8, reloc_section.length);
    for (uint32_t index = 0; index < all_count; index++) {
        const struct dll_section *section = &sections[index];
        uint32_t header = optional + 224 + index * 40;
        copy_bytes(pe + header, section->name, text_length(section->name));
        write_u32(pe, header + 8, section->virtual_size);
        write_u32(pe, header + 12, section->rva);
        write_u32(pe, header + 16, section->file_size);
        write_u32(pe, header + 20, section->file_pointer);
        write_u32(pe, header + 36, section->flags & ~COMPRESSED_SECTION_FLAG);
        copy_bytes(pe + section->file_pointer, section->data, section->data_length);
    }
    struct dll_bytes result = { pe, pe_length };
    return result;
}

void patch_dll_bytes(struct dll_bytes dll, const struct rom_spec *spec)
{
    for (uint32_t index = 0; index < spec->patch_count; index++) {
        const struct byte_patch *patch = &spec->patches[index];
        int matches = (uint64_t)patch->offset + patch->length <= dll.length && bytes_equal(dll.data + patch->offset, patch->original, patch->length);
        if (!matches) {
            struct buffer expected = { 0 };
            buffer_hex(&expected, patch->original, patch->length);
            buffer_append(&expected, "", 1);
            fail(NKGEN_CHECK_FAILED, "%s 0x%08x: expected %s", spec->module, patch->offset, (const char *)expected.data);
        }
        copy_bytes(dll.data + patch->offset, patch->replacement, patch->length);
    }
}
