#include "internal.h"

#define SECTION_HEADER_SIZE 40u
#define DESCRIPTOR_SIZE 20u

struct pe_section {
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_size;
    uint32_t raw_pointer;
};

struct pe_descriptor {
    uint32_t lookup;
    uint32_t timestamp;
    uint32_t forwarder;
    uint32_t name;
    uint32_t first_thunk;
};

struct import_key {
    int by_ordinal;
    uint32_t ordinal;
    const char *name;
};

struct redirected_slot {
    uint32_t index;
    const struct import_redirect_entry *redirect;
    uint32_t hint_name_rva;
};

struct pe_file {
    const uint8_t *bytes;
    uint32_t length;
    uint32_t section_count;
    const struct pe_section *sections;
};

static uint8_t byte_or_zero(const uint8_t *bytes, uint32_t length, int64_t offset)
{
    return offset >= 0 && offset < (int64_t)length ? bytes[offset] : 0;
}

static int64_t file_offset(const struct pe_file *file, uint32_t rva)
{
    for (uint32_t index = 0; index < file->section_count; index++) {
        const struct pe_section *section = &file->sections[index];
        uint32_t extent = section->virtual_size > section->raw_size ? section->virtual_size : section->raw_size;
        if (rva >= section->virtual_address && (uint64_t)rva < (uint64_t)section->virtual_address + extent) {
            return (int64_t)rva - section->virtual_address + section->raw_pointer;
        }
    }
    fail(NKGEN_CHECK_FAILED, "RVA 0x%08x is not inside a section", rva);
}

static const char *read_string(const struct pe_file *file, uint32_t rva)
{
    int64_t start = file_offset(file, rva);
    int64_t end = index_of_byte(file->bytes, file->length, 0, start);
    if (end < 0 || start >= end) return "";
    return (const char *)file->bytes + start;
}

static struct import_key import_key(const struct pe_file *file, uint32_t entry)
{
    struct import_key key = { 0, 0, 0 };
    if (entry & 0x80000000u) {
        key.by_ordinal = 1;
        key.ordinal = entry & 0xffff;
    } else {
        key.name = read_string(file, entry + 2);
    }
    return key;
}

static const struct import_redirect_entry *find_redirect(const struct import_redirect *redirect, struct import_key key)
{
    for (uint32_t index = 0; index < redirect->count; index++) {
        const struct import_redirect_entry *entry = &redirect->imports[index];
        if (entry->by_ordinal != key.by_ordinal) continue;
        if (key.by_ordinal ? entry->ordinal == key.ordinal : text_equal(entry->key, key.name)) return entry;
    }
    return 0;
}

static uint32_t add_string(struct buffer *strings, uint32_t *strings_rva, const uint8_t *data, uint32_t length)
{
    uint32_t rva = *strings_rva;
    uint32_t padded = length + length % 2;
    uint8_t *target = buffer_push(strings, padded);
    copy_bytes(target, data, length);
    *strings_rva += padded;
    return rva;
}

struct dll_bytes redirect_imports(struct dll_bytes original, const struct import_redirect *redirect)
{
    const uint8_t *bytes = original.data;
    uint32_t length = original.length;
    check(byte_or_zero(bytes, length, 0) == 'M' && byte_or_zero(bytes, length, 1) == 'Z', "Not an MZ executable");
    uint32_t pe_offset = read_u32(bytes, length, 0x3c);
    check(byte_or_zero(bytes, length, pe_offset) == 'P' && byte_or_zero(bytes, length, (int64_t)pe_offset + 1) == 'E'
        && byte_or_zero(bytes, length, (int64_t)pe_offset + 2) == 0 && byte_or_zero(bytes, length, (int64_t)pe_offset + 3) == 0,
        "Missing PE signature");
    int64_t file_header = (int64_t)pe_offset + 4;
    uint32_t section_count = read_u16(bytes, length, file_header + 2);
    uint32_t optional_size = read_u16(bytes, length, file_header + 16);
    int64_t optional = file_header + 20;
    check(read_u16(bytes, length, optional) == 0x10b, "Expected a PE32 optional header");
    uint32_t section_alignment = read_u32(bytes, length, optional + 32);
    uint32_t file_alignment = read_u32(bytes, length, optional + 36);
    uint32_t size_of_headers = read_u32(bytes, length, optional + 60);
    int64_t import_directory = optional + 96 + 8;
    int64_t bound_directory = optional + 96 + 8 * 11;
    int64_t section_table = optional + optional_size;

    struct pe_section *sections = arena_allocate(section_count * sizeof(struct pe_section));
    for (uint32_t index = 0; index < section_count; index++) {
        int64_t header = section_table + (int64_t)index * SECTION_HEADER_SIZE;
        sections[index].virtual_size = read_u32(bytes, length, header + 8);
        sections[index].virtual_address = read_u32(bytes, length, header + 12);
        sections[index].raw_size = read_u32(bytes, length, header + 16);
        sections[index].raw_pointer = read_u32(bytes, length, header + 20);
    }
    struct pe_file file = { bytes, length, section_count, sections };

    check(read_u32(bytes, length, bound_directory) == 0, "Bound imports are not supported");
    struct buffer descriptor_list = { 0 };
    for (int64_t offset = file_offset(&file, read_u32(bytes, length, import_directory)); ; offset += DESCRIPTOR_SIZE) {
        struct pe_descriptor descriptor;
        descriptor.lookup = read_u32(bytes, length, offset);
        descriptor.timestamp = read_u32(bytes, length, offset + 4);
        descriptor.forwarder = read_u32(bytes, length, offset + 8);
        descriptor.name = read_u32(bytes, length, offset + 12);
        descriptor.first_thunk = read_u32(bytes, length, offset + 16);
        if (!descriptor.name) break;
        *(struct pe_descriptor *)buffer_push(&descriptor_list, sizeof(descriptor)) = descriptor;
    }
    uint32_t descriptor_count = descriptor_list.length / sizeof(struct pe_descriptor);
    struct pe_descriptor *descriptors = buffer_items(&descriptor_list);
    struct pe_descriptor *source = 0;
    for (uint32_t index = 0; index < descriptor_count && !source; index++) {
        if (names_equal(read_string(&file, descriptors[index].name), redirect->from)) source = &descriptors[index];
    }
    check(source, "%s is not imported", redirect->from);

    struct buffer lookup_list = { 0 };
    for (uint64_t rva = source->lookup ? source->lookup : source->first_thunk; ; rva += 4) {
        check(rva <= 0xffffffffu, "RVA 0x%08llx is not inside a section", (unsigned long long)rva);
        uint32_t entry = read_u32(bytes, length, file_offset(&file, (uint32_t)rva));
        if (!entry) break;
        *(uint32_t *)buffer_push(&lookup_list, 4) = entry;
    }
    uint32_t lookup_count = lookup_list.length / 4;
    uint32_t *lookups = buffer_items(&lookup_list);

    struct buffer slot_list = { 0 };
    for (uint32_t index = 0; index < lookup_count; index++) {
        const struct import_redirect_entry *found = find_redirect(redirect, import_key(&file, lookups[index]));
        if (!found) continue;
        struct redirected_slot *slot = buffer_push(&slot_list, sizeof(struct redirected_slot));
        slot->index = index;
        slot->redirect = found;
    }
    uint32_t slot_count = slot_list.length / sizeof(struct redirected_slot);
    struct redirected_slot *slots = buffer_items(&slot_list);
    for (uint32_t index = 0; index < slot_count; index++) {
        for (uint32_t other = 0; other < index; other++) check(slots[other].redirect != slots[index].redirect, "Import listed twice");
    }
    for (uint32_t index = 0; index < redirect->count; index++) {
        int found = 0;
        for (uint32_t slot = 0; slot < slot_count; slot++) found |= slots[slot].redirect == &redirect->imports[index];
        if (found) continue;
        if (redirect->imports[index].by_ordinal) fail(NKGEN_CHECK_FAILED, "%u is not imported from %s", redirect->imports[index].ordinal, redirect->from);
        fail(NKGEN_CHECK_FAILED, "%s is not imported from %s", redirect->imports[index].key, redirect->from);
    }
    uint32_t substitute = 0;
    int has_substitute = 0;
    for (uint32_t index = 0; index < lookup_count && !has_substitute; index++) {
        if (!find_redirect(redirect, import_key(&file, lookups[index]))) {
            substitute = lookups[index];
            has_substitute = 1;
        }
    }
    check(has_substitute, "No remaining import to stand in for redirected slots");

    int64_t new_header_end = section_table + ((int64_t)section_count + 1) * SECTION_HEADER_SIZE;
    check(new_header_end <= size_of_headers, "No room for another section header");
    int64_t first_raw = -1;
    for (uint32_t index = 0; index < section_count; index++) {
        if (sections[index].raw_pointer && (first_raw < 0 || sections[index].raw_pointer < first_raw)) first_raw = sections[index].raw_pointer;
    }
    check(first_raw < 0 || new_header_end <= first_raw, "Section header would overlap section data");
    check(section_count > 0, "No sections");
    check(section_alignment && file_alignment, "Section or file alignment is zero");

    const struct pe_section *last = &sections[0];
    for (uint32_t index = 1; index < section_count; index++) {
        if (!(last->virtual_address > sections[index].virtual_address)) last = &sections[index];
    }
    uint32_t last_extent = last->virtual_size > last->raw_size ? last->virtual_size : last->raw_size;
    int64_t section_rva = align_up((int64_t)last->virtual_address + last_extent, section_alignment);
    int64_t descriptors_size = ((int64_t)descriptor_count + slot_count + 1) * DESCRIPTOR_SIZE;
    int64_t source_lookup_rva = section_rva + descriptors_size;
    int64_t redirect_lookup_rva = source_lookup_rva + ((int64_t)lookup_count + 1) * 4;
    uint32_t strings_rva = (uint32_t)(redirect_lookup_rva + (int64_t)slot_count * 8);
    uint32_t strings_start = strings_rva;
    struct buffer strings = { 0 };
    uint32_t to_length = text_length(redirect->to);
    uint8_t *target_name = arena_allocate(to_length + 1);
    copy_bytes(target_name, redirect->to, to_length);
    uint32_t target_name_rva = add_string(&strings, &strings_rva, target_name, to_length + 1);
    for (uint32_t index = 0; index < slot_count; index++) {
        uint32_t name_length = text_length(slots[index].redirect->name);
        uint8_t *hint_name = arena_allocate(2 + name_length + 1);
        copy_bytes(hint_name + 2, slots[index].redirect->name, name_length);
        slots[index].hint_name_rva = add_string(&strings, &strings_rva, hint_name, 2 + name_length + 1);
    }
    int64_t section_size = (int64_t)strings_rva - section_rva;
    int64_t padded_size = align_up(section_size, file_alignment);
    int64_t raw_pointer = align_up(length, file_alignment);
    check(raw_pointer + padded_size <= 0x7fffffff, "Redirected executable is too large");
    uint8_t *section = arena_allocate((uint32_t)padded_size);

    uint32_t cursor = 0;
    for (uint32_t index = 0; index < descriptor_count; index++) {
        const struct pe_descriptor *descriptor = &descriptors[index];
        write_u32(section, cursor, descriptor == source ? (uint32_t)source_lookup_rva : descriptor->lookup);
        write_u32(section, cursor + 4, descriptor->timestamp);
        write_u32(section, cursor + 8, descriptor->forwarder);
        write_u32(section, cursor + 12, descriptor->name);
        write_u32(section, cursor + 16, descriptor->first_thunk);
        cursor += DESCRIPTOR_SIZE;
    }
    for (uint32_t index = 0; index < slot_count; index++) {
        write_u32(section, cursor, (uint32_t)(redirect_lookup_rva + (int64_t)index * 8));
        write_u32(section, cursor + 12, target_name_rva);
        write_u32(section, cursor + 16, source->first_thunk + slots[index].index * 4);
        cursor += DESCRIPTOR_SIZE;
    }
    for (uint32_t index = 0; index < lookup_count; index++) {
        int redirected = 0;
        for (uint32_t slot = 0; slot < slot_count; slot++) redirected |= slots[slot].index == index;
        write_u32(section, (uint32_t)(source_lookup_rva - section_rva) + index * 4, redirected ? substitute : lookups[index]);
    }
    for (uint32_t index = 0; index < slot_count; index++) {
        write_u32(section, (uint32_t)(redirect_lookup_rva - section_rva) + index * 8, slots[index].hint_name_rva);
    }
    copy_bytes(section + (strings_start - section_rva), strings.data, strings.length);

    uint32_t patched_length = (uint32_t)(raw_pointer + padded_size);
    uint8_t *patched = arena_allocate(patched_length);
    copy_bytes(patched, bytes, length);
    copy_bytes(patched + raw_pointer, section, (uint32_t)padded_size);
    int64_t new_header = section_table + (int64_t)section_count * SECTION_HEADER_SIZE;
    static const char section_name[8] = ".brnimp";
    for (uint32_t index = 0; index < SECTION_HEADER_SIZE; index++) {
        int64_t position = new_header + index;
        if (position >= 0 && position < patched_length) patched[position] = index < 8 ? (uint8_t)section_name[index] : 0;
    }
    write_u32_within(patched, patched_length, new_header + 8, (uint32_t)section_size);
    write_u32_within(patched, patched_length, new_header + 12, (uint32_t)section_rva);
    write_u32_within(patched, patched_length, new_header + 16, (uint32_t)padded_size);
    write_u32_within(patched, patched_length, new_header + 20, (uint32_t)raw_pointer);
    write_u32_within(patched, patched_length, new_header + 36, 0x40000040u);
    write_u16(patched, (uint32_t)file_header + 2, section_count + 1);
    write_u32(patched, (uint32_t)optional + 56, (uint32_t)align_up(section_rva + section_size, section_alignment));
    write_u32(patched, (uint32_t)import_directory, (uint32_t)section_rva);
    write_u32(patched, (uint32_t)import_directory + 4, (uint32_t)descriptors_size);
    struct dll_bytes result = { patched, patched_length };
    return result;
}
