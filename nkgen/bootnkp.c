#include "internal.h"

#define ROM_HEADER_NUMBER_OF_FILES 48u
#define ROM_HEADER_PHYSICAL_LAST 12u
#define SECTOR_SIZE 512
#define CODE_ALIGNMENT 32

struct span {
    int64_t start;
    int64_t end;
};

struct appended {
    uint32_t extended_length;
    uint32_t data_address;
    uint32_t file_count;
    uint32_t physical_last;
    uint8_t header[ROM_HEADER_SIZE];
};

struct builder {
    const struct bundle_request *request;
    struct rom_image *image;
    const uint8_t *bytes;
    uint32_t length;
    struct span *spans;
    struct span *room;
    uint8_t *used;
    uint32_t span_count;
    uint32_t reserved_entry_offset;
    uint32_t entry_index;
    struct buffer text;
    int first_section;
};

static int name_in_list(const char *name, const char **list, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        if (names_equal(name, list[index])) return 1;
    }
    return 0;
}

static int skip_list_tested(const struct skip_list *skip_list, const uint8_t *sha256_digest)
{
    if (!sha256_digest) return 0;
    for (uint32_t index = 0; index < skip_list->tested_count; index++) {
        if (bytes_equal(skip_list->tested + index * 32, sha256_digest, 32)) return 1;
    }
    return 0;
}

static int compare_span_starts(const void *first, const void *second)
{
    const struct span *a = first;
    const struct span *b = second;
    if (a->start == b->start) return 0;
    return a->start < b->start ? -1 : 1;
}

static void add_section_ranges(struct buffer *ranges, const struct rom_module *module)
{
    for (uint32_t index = 0; index < module->section_count; index++) {
        const struct rom_section *section = &module->sections[index];
        if (section->data_offset == NO_OFFSET) continue;
        uint32_t size = section->virtual_size ? section->virtual_size : section->physical_size;
        struct span *range = buffer_push(ranges, sizeof(struct span));
        range->start = section->data_offset;
        range->end = section->data_offset + (section->physical_size < size ? section->physical_size : size);
    }
}

static void find_skip_spans(struct builder *builder, int tested)
{
    struct rom_image *image = builder->image;
    const struct skip_list *skip_list = builder->request->skip_list;
    struct rom_module *modules = image_modules(image);
    struct rom_file *files = image_files(image);
    struct buffer missing = { 0 };
    for (uint32_t index = 0; index < skip_list->module_count; index++) {
        if (!find_module(image, skip_list->modules[index])) buffer_format(&missing, "%s%s", missing.length ? ", " : "", skip_list->modules[index]);
    }
    for (uint32_t index = 0; index < skip_list->file_count; index++) {
        if (!find_file(image, skip_list->files[index])) buffer_format(&missing, "%s%s", missing.length ? ", " : "", skip_list->files[index]);
    }
    if (missing.length) {
        buffer_append(&missing, "", 1);
        log_line("note: skip list entries not in this image: %s", missing.data);
    }
    if (!tested) log_line("note: skip list was checked against a different image; turn skipping off if Windows CE misbehaves");

    struct buffer ranges = { 0 };
    for (uint32_t index = 0; index < image->header.nummods; index++) {
        if (name_in_list(modules[index].name, skip_list->modules, skip_list->module_count)) add_section_ranges(&ranges, &modules[index]);
    }
    for (uint32_t index = 0; index < image->header.numfiles; index++) {
        if (!name_in_list(files[index].name, skip_list->files, skip_list->file_count)) continue;
        struct span *range = buffer_push(&ranges, sizeof(struct span));
        range->start = files[index].load_offset;
        range->end = (int64_t)files[index].load_offset + files[index].compressed_size;
    }
    uint32_t range_count = ranges.length / sizeof(struct span);
    struct span *list = buffer_items(&ranges);
    uint32_t kept_ranges = 0;
    for (uint32_t index = 0; index < range_count; index++) {
        struct span aligned = { align_up(list[index].start, SECTOR_SIZE), align_down(list[index].end, SECTOR_SIZE) };
        if (aligned.end > aligned.start) list[kept_ranges++] = aligned;
    }
    stable_sort(list, kept_ranges, sizeof(struct span), compare_span_starts);
    uint32_t merged_count = 0;
    for (uint32_t index = 0; index < kept_ranges; index++) {
        if (merged_count && list[index].start <= list[merged_count - 1].end) {
            if (list[index].end > list[merged_count - 1].end) list[merged_count - 1].end = list[index].end;
        } else {
            list[merged_count++] = list[index];
        }
    }
    uint32_t span_count = 0;
    for (uint32_t index = 0; index < merged_count; index++) {
        if (list[index].end - list[index].start >= skip_list->minimum_span) list[span_count++] = list[index];
    }

    uint32_t region_count = 0;
    struct region *regions = list_used_regions(image, &region_count);
    struct buffer kept = { 0 };
    for (uint32_t index = 0; index < region_count; index++) {
        if (regions[index].kind == REGION_MODULE_SECTION) continue;
        if (regions[index].kind == REGION_FILE_DATA && name_in_list(files[regions[index].index].name, skip_list->files, skip_list->file_count)) continue;
        *(struct region *)buffer_push(&kept, sizeof(struct region)) = regions[index];
    }
    for (uint32_t index = 0; index < image->header.nummods; index++) {
        if (name_in_list(modules[index].name, skip_list->modules, skip_list->module_count)) continue;
        struct buffer module_ranges = { 0 };
        add_section_ranges(&module_ranges, &modules[index]);
        struct span *module_list = buffer_items(&module_ranges);
        for (uint32_t range = 0; range < module_ranges.length / sizeof(struct span); range++) {
            struct region *region = buffer_push(&kept, sizeof(struct region));
            region->start = module_list[range].start;
            region->end = module_list[range].end;
            region->kind = REGION_KEPT_MODULE;
            region->index = index;
        }
    }
    uint32_t kept_count = kept.length / sizeof(struct region);
    struct region *kept_list = buffer_items(&kept);
    for (uint32_t index = 0; index < span_count; index++) {
        for (uint32_t region = 0; region < kept_count; region++) {
            struct region *candidate = &kept_list[region];
            if (candidate->end > list[index].start && candidate->start < list[index].end && candidate->end > candidate->start) {
                fail(NKGEN_CHECK_FAILED, "Skip span %llx-%llx overlaps %s", (unsigned long long)list[index].start,
                    (unsigned long long)list[index].end, region_name(image, candidate));
            }
        }
    }
    builder->spans = list;
    builder->span_count = span_count;
}

static int64_t carve_span(struct builder *builder, uint32_t length, int64_t alignment)
{
    struct span *largest = 0;
    for (uint32_t index = 0; index < builder->span_count; index++) {
        struct span *candidate = &builder->room[index];
        if (!largest || candidate->end - candidate->start > largest->end - largest->start) largest = candidate;
    }
    int64_t offset = largest ? align_down(largest->end - length, alignment) : -1;
    check(largest && offset >= largest->start, "%u bytes do not fit before ulRAMStart %x or in a skipped span; turn skipping on",
        length, builder->image->header.ram_start);
    largest->end = offset;
    return offset;
}

static struct rom_file *template_file(struct rom_image *image, int uncompressed_only)
{
    struct rom_file *files = image_files(image);
    struct rom_file *first = 0;
    for (uint32_t index = 0; index < image->header.numfiles; index++) {
        if (uncompressed_only && files[index].compressed) continue;
        if (!first) first = &files[index];
        if (names_equal(files[index].name, "eddesktop.bmp")) return &files[index];
    }
    return first;
}

static void make_file_entry(struct builder *builder, uint8_t entry[FILES_ENTRY_SIZE], const struct rom_file *template, uint32_t length, uint32_t name_offset, uint32_t data_offset)
{
    copy_bytes(entry, builder->bytes + template->entry_offset, FILES_ENTRY_SIZE);
    write_u32(entry, 0, template->attributes);
    write_u32(entry, 12, length);
    write_u32(entry, 16, length);
    write_u32(entry, 20, name_offset + IMAGE_BASE);
    write_u32(entry, 24, data_offset + IMAGE_BASE);
}

static void append_bstart(struct builder *builder, struct appended *result, uint32_t reserved_entries)
{
    struct rom_image *image = builder->image;
    const struct rom_header *header = &image->header;
    const uint8_t *contents = builder->request->bstart;
    uint32_t contents_length = builder->request->bstart_length;
    struct rom_file *template = template_file(image, 1);
    check(template, "No uncompressed ROM file to copy attributes from");
    uint32_t header_offset = image_offset_of(image, image->rom_header_va, ROM_HEADER_SIZE);
    int64_t entry_offset = (int64_t)header->physlast - IMAGE_BASE;
    int64_t name_offset = entry_offset + (int64_t)FILES_ENTRY_SIZE * (1 + reserved_entries);
    static const char name[] = "bstart.exe";
    int64_t name_end = align_up(name_offset + (int64_t)sizeof(name), 4);
    int fits_at_end = align_up(name_end + contents_length, 4) + IMAGE_BASE <= header->ram_start;
    int64_t data_offset = fits_at_end ? name_end : carve_span(builder, contents_length, 4);
    int64_t new_end = fits_at_end ? align_up(data_offset + contents_length, 4) : name_end;
    check(new_end + IMAGE_BASE <= header->ram_start, "bstart.exe would end at %llx, past ulRAMStart %x",
        (unsigned long long)(new_end + IMAGE_BASE), header->ram_start);
    check(entry_offset >= 0 && new_end - entry_offset <= 0x1000000, "bstart.exe would be appended outside the image");
    uint32_t appended_length = (uint32_t)(new_end - entry_offset);
    uint8_t *appended = arena_allocate(appended_length);
    make_file_entry(builder, appended, template, contents_length, (uint32_t)name_offset, (uint32_t)data_offset);
    copy_bytes(appended + (name_offset - entry_offset), name, sizeof(name));
    if (fits_at_end) copy_bytes(appended + (data_offset - entry_offset), contents, contents_length);
    copy_bytes(result->header, builder->bytes + header_offset, ROM_HEADER_SIZE);
    write_u32(result->header, ROM_HEADER_NUMBER_OF_FILES, header->numfiles + 1);
    write_u32(result->header, ROM_HEADER_PHYSICAL_LAST, (uint32_t)(new_end + IMAGE_BASE));
    result->extended_length = (uint32_t)new_end;
    result->data_address = (uint32_t)(data_offset + IMAGE_BASE);
    result->file_count = header->numfiles + 1;
    result->physical_last = (uint32_t)(new_end + IMAGE_BASE);
    builder->reserved_entry_offset = (uint32_t)entry_offset + FILES_ENTRY_SIZE;

    patch_section_start(&builder->text, "bstart", builder->length, result->extended_length, builder->first_section);
    builder->first_section = 0;
    patch_records(&builder->text, header_offset, builder->bytes + header_offset, ROM_HEADER_SIZE, result->header, ROM_HEADER_SIZE);
    uint8_t *zeros = arena_allocate(appended_length > contents_length ? appended_length : contents_length);
    patch_records(&builder->text, entry_offset, zeros, appended_length, appended, appended_length);
    if (!fits_at_end) patch_records(&builder->text, data_offset, zeros, contents_length, contents, contents_length);
    registry_string_records(&builder->text, image, 2, "init", "Launch50", "bstart.exe");
}

static void append_code(struct builder *builder, const uint8_t *code, uint32_t code_length, const struct appended *previous, struct appended *result, const char *name)
{
    struct rom_image *image = builder->image;
    uint32_t header_offset = image_offset_of(image, image->rom_header_va, ROM_HEADER_SIZE);
    int64_t end_offset = align_up((int64_t)previous->physical_last - IMAGE_BASE, CODE_ALIGNMENT);
    *result = *previous;
    uint8_t *zeros = arena_allocate(code_length + 4);
    if (align_up(end_offset + code_length, 4) + IMAGE_BASE > image->header.ram_start) {
        int64_t data_offset = carve_span(builder, code_length, CODE_ALIGNMENT);
        result->data_address = (uint32_t)(data_offset + IMAGE_BASE);
        patch_section_start(&builder->text, name, builder->length, result->extended_length, 0);
        patch_records(&builder->text, data_offset, zeros, code_length, code, code_length);
        return;
    }
    int64_t data_offset = end_offset;
    int64_t new_end = align_up(data_offset + code_length, 4);
    check(new_end + IMAGE_BASE <= image->header.ram_start, "Code would end at %llx, past ulRAMStart %x",
        (unsigned long long)(new_end + IMAGE_BASE), image->header.ram_start);
    write_u32(result->header, ROM_HEADER_PHYSICAL_LAST, (uint32_t)(new_end + IMAGE_BASE));
    uint32_t appended_length = (uint32_t)(new_end - data_offset);
    uint8_t *appended = arena_allocate(appended_length);
    copy_bytes(appended, code, code_length);
    result->extended_length = (uint32_t)new_end;
    result->data_address = (uint32_t)(data_offset + IMAGE_BASE);
    patch_section_start(&builder->text, name, builder->length, result->extended_length, 0);
    patch_records(&builder->text, header_offset, previous->header, ROM_HEADER_SIZE, result->header, ROM_HEADER_SIZE);
    patch_records(&builder->text, data_offset, zeros, appended_length, appended, appended_length);
}

struct rom_entry {
    const char *name;
    struct rom_file *file;
    uint32_t name_offset;
    int found;
};

static struct rom_entry find_rom_entry(struct rom_image *image, const char *name)
{
    struct rom_entry entry = { 0 };
    struct rom_module *module = find_module(image, name);
    if (module) {
        entry.found = 1;
        entry.name = module->name;
        entry.name_offset = module->name_offset;
        return entry;
    }
    struct rom_file *file = find_file(image, name);
    if (file) {
        entry.found = 1;
        entry.name = file->name;
        entry.file = file;
    }
    return entry;
}

static void replace_module(struct builder *builder, const struct replacement *replacement, struct appended *previous, struct replaced_module *replaced)
{
    struct rom_image *image = builder->image;
    struct rom_entry existing = find_rom_entry(image, replacement->name);
    struct span *span = 0;
    for (uint32_t index = 0; index < builder->span_count; index++) {
        if (!builder->used[index] && builder->room[index].end - builder->room[index].start >= replacement->length) {
            span = &builder->room[index];
            builder->used[index] = 1;
            break;
        }
    }
    check(span, "No skipped span holds %s (%u bytes); replacing a module needs skipping on", replacement->name, replacement->length);
    char *section_name = text_format("module %s", replacement->name);
    uint32_t crc = crc32(replacement->contents, replacement->length);
    replaced->name = replacement->name;
    replaced->contents = replacement->contents;
    replaced->length = replacement->length;

    if (existing.file) {
        uint8_t entry[FILES_ENTRY_SIZE];
        const uint8_t *original = builder->bytes + existing.file->entry_offset;
        copy_bytes(entry, original, FILES_ENTRY_SIZE);
        write_u32(entry, 12, replacement->length);
        write_u32(entry, 16, replacement->length);
        write_u32(entry, 24, (uint32_t)(span->start + IMAGE_BASE));
        replaced->card_path = text_format("modules/%s", existing.name);
        patch_section_start(&builder->text, section_name, builder->length, previous->extended_length, 0);
        patch_load_line(&builder->text, (uint32_t)span->start, replacement->length, crc, replaced->card_path);
        patch_records(&builder->text, existing.file->entry_offset, original, FILES_ENTRY_SIZE, entry, FILES_ENTRY_SIZE);
        return;
    }

    uint32_t index = builder->entry_index++;
    const char *file_name = existing.found ? existing.name : replacement->name;
    uint32_t file_name_length = text_length(file_name);
    int32_t extension = -1;
    for (uint32_t position = 0; position < file_name_length; position++) {
        if (file_name[position] == '.') extension = (int32_t)position;
    }
    check(extension > 0, "%s has no extension", file_name);
    char *renamed = 0;
    if (existing.found) {
        renamed = text_copy(file_name);
        renamed[extension - 1] = '_';
    }
    uint8_t header_replacement[ROM_HEADER_SIZE];
    uint32_t header_offset = image_offset_of(image, image->rom_header_va, ROM_HEADER_SIZE);
    copy_bytes(header_replacement, previous->header, ROM_HEADER_SIZE);
    write_u32(header_replacement, ROM_HEADER_NUMBER_OF_FILES, previous->file_count + 1 + index);
    struct rom_file *template = template_file(image, 0);
    check(template, "No ROM file to copy attributes from");
    int64_t name_offset = span->start + align_up(replacement->length, 4);
    check(name_offset + file_name_length + 1 <= span->end, "%s and its name do not fit in the skipped span", replacement->name);
    uint8_t entry[FILES_ENTRY_SIZE];
    make_file_entry(builder, entry, template, replacement->length, (uint32_t)name_offset, (uint32_t)span->start);
    replaced->card_path = text_format("modules/%s", file_name);
    patch_section_start(&builder->text, section_name, builder->length, previous->extended_length, 0);
    patch_load_line(&builder->text, (uint32_t)span->start, replacement->length, crc, replaced->card_path);
    patch_records(&builder->text, header_offset, previous->header, ROM_HEADER_SIZE, header_replacement, ROM_HEADER_SIZE);
    uint8_t zeros[FILES_ENTRY_SIZE] = { 0 };
    patch_records(&builder->text, builder->reserved_entry_offset + (int64_t)FILES_ENTRY_SIZE * index, zeros, FILES_ENTRY_SIZE, entry, FILES_ENTRY_SIZE);
    uint8_t *name_zeros = arena_allocate(file_name_length + 1);
    patch_records(&builder->text, name_offset, name_zeros, file_name_length + 1, (const uint8_t *)file_name, file_name_length + 1);
    if (existing.found) {
        patch_records(&builder->text, existing.name_offset, (const uint8_t *)existing.name, file_name_length, (const uint8_t *)renamed, file_name_length);
    }
    copy_bytes(previous->header, header_replacement, ROM_HEADER_SIZE);
}

void make_bundle(const struct bundle_request *request, struct bundle_result *result)
{
    struct builder builder = { 0 };
    struct rom_image *image = request->image;
    builder.request = request;
    builder.image = image;
    builder.bytes = image->bytes;
    builder.length = image->length;
    builder.first_section = 1;
    fill_bytes(result, 0, sizeof(*result));

    int tested = skip_list_tested(request->skip_list, request->image_sha256);
    if (request->skipping) {
        step_begin("skip");
        find_skip_spans(&builder, tested);
        step_end("%u spans%s", builder.span_count, tested ? "" : "; skip list was checked against a different image");
    }
    builder.room = arena_allocate(builder.span_count * sizeof(struct span));
    builder.used = arena_allocate(builder.span_count);
    copy_bytes(builder.room, builder.spans, builder.span_count * sizeof(struct span));

    uint32_t replacement_count = request->replacement_count + (request->registry ? 1 : 0);
    struct replacement *replacements = arena_allocate(replacement_count * sizeof(struct replacement));
    copy_bytes(replacements, request->replacements, request->replacement_count * sizeof(struct replacement));
    if (request->registry) {
        step_begin("registry");
        report_progress("Merging registry", 0, 1);
        struct replacement *registry = &replacements[request->replacement_count];
        registry->name = "default.fdf";
        registry->contents = merge_registry(image, request->registry_entries, request->registry_entry_count, &registry->length);
        report_progress("Merging registry", 1, 1);
        step_end("default.fdf %u bytes, %u entries", registry->length, request->registry_entry_count);
    }

    uint32_t added_entries = 0;
    for (uint32_t index = 0; index < replacement_count; index++) {
        if (!find_rom_entry(image, replacements[index].name).file) added_entries++;
    }

    for (uint32_t index = 0; index < builder.span_count; index++) {
        buffer_format(&builder.text, "skip %llx %llx %x\n", (unsigned long long)builder.spans[index].start,
            (unsigned long long)builder.spans[index].end, builder.length);
        result->skipped_bytes += (uint64_t)(builder.spans[index].end - builder.spans[index].start);
    }
    result->span_count = builder.span_count;

    uint32_t section_total = 1 + (request->a7300_display ? 1 : 0) + replacement_count + request->word_patch_count;
    uint32_t section_done = 0;
    report_progress("Building patches", section_done++, section_total);
    step_begin("bstart");
    struct appended bstart;
    append_bstart(&builder, &bstart, added_entries);
    step_end("bstart.exe at %x, %u reserved file entries", bstart.data_address, added_entries);

    struct appended last = bstart;
    if (request->a7300_display) {
        report_progress("Building patches", section_done++, section_total);
        step_begin("a7300-display");
        check(a7300_display_image(request->image_sha256), "The A7300 display patch is only for the PW-A7300 and PW-G5300 main images");
        struct appended display;
        append_code(&builder, request->a7300_display, request->a7300_display_length, &bstart, &display, "a7300-display");
        a7300_display_records(&builder.text, image, display.data_address);
        last = bstart;
        copy_bytes(last.header, display.header, ROM_HEADER_SIZE);
        last.extended_length = display.extended_length;
        step_end("hook at %x, %u bytes", display.data_address, request->a7300_display_length);
    }

    result->replaced = arena_allocate(replacement_count * sizeof(struct replaced_module));
    result->replaced_count = replacement_count;
    for (uint32_t index = 0; index < replacement_count; index++) {
        report_progress("Building patches", section_done++, section_total);
        char *name = text_format("module %s", replacements[index].name);
        step_begin(name);
        replace_module(&builder, &replacements[index], &last, &result->replaced[index]);
        step_end("%u bytes, %s", replacements[index].length, result->replaced[index].card_path);
    }

    for (uint32_t index = 0; index < request->word_patch_count; index++) {
        const struct word_patch *patch = &request->word_patches[index];
        report_progress("Building patches", section_done++, section_total);
        char *name = text_format("words %s", patch->module);
        step_begin(name);
        for (uint32_t replacement = 0; replacement < replacement_count; replacement++) {
            check(!names_equal(replacements[replacement].name, patch->module), "%s is both replaced and word-patched", patch->module);
        }
        patch_section_start(&builder.text, name, builder.length, last.extended_length, 0);
        word_patch_records(&builder.text, image, patch);
        step_end("%u words", patch->count);
    }
    report_progress("Building patches", section_total, section_total);
    result->text = builder.text;
}
