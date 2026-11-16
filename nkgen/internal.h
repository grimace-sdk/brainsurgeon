#ifndef NKGEN_INTERNAL_H
#define NKGEN_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "nkgen.h"

#if defined(NKGEN_HOSTED)
#include <setjmp.h>
typedef jmp_buf failure_point;
#define save_failure_point(point) setjmp(point)
#define return_to_failure_point(point) longjmp(point, 1)
#else
typedef uint32_t failure_point[10];
int nkgen_save(failure_point point) __attribute__((returns_twice));
void nkgen_restore(failure_point point) __attribute__((noreturn));
#define save_failure_point(point) nkgen_save(point)
#define return_to_failure_point(point) nkgen_restore(point)
#endif

#define IMAGE_BASE 0x80200000u
#define ROM_HEADER_SIZE 84u
#define TOC_ENTRY_SIZE 32u
#define FILES_ENTRY_SIZE 28u
#define COPY_ENTRY_SIZE 16u
#define O32_ENTRY_SIZE 24u
#define E32_SIZE 112u
#define COMPRESSED_FILE_ATTRIBUTE 0x800u
#define COMPRESSED_SECTION_FLAG 0x2000u
#define IDENTITY_LIMIT 65536u
#define NO_OFFSET (-1)

struct buffer {
    uint8_t *data;
    uint32_t length;
    uint32_t capacity;
    int fixed;
};

struct step_list {
    struct nkgen_step *steps;
    uint32_t count;
    uint32_t capacity;
    int32_t open;
};

struct generator {
    struct nkgen_call *call;
    uint8_t *arena;
    uint32_t arena_length;
    uint32_t arena_used;
    uint32_t arena_peak;
    failure_point failure;
    struct step_list steps;
};

void generator_start(struct generator *generator, struct nkgen_call *call);
void generator_stop(void);
struct generator *active_generator(void);
void fail(int32_t code, const char *format, ...) __attribute__((noreturn, format(printf, 2, 3)));
#define check(condition, ...) do { if (!(condition)) fail(NKGEN_CHECK_FAILED, __VA_ARGS__); } while (0)
void *arena_allocate(uint32_t size);
void *arena_copy(const void *data, uint32_t size);
char *text_format(const char *format, ...) __attribute__((format(printf, 1, 2)));
char *text_copy(const char *text);
void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
void report_progress(const char *stage, uint32_t done, uint32_t total);
void step_begin(const char *name);
void step_end(const char *format, ...) __attribute__((format(printf, 1, 2)));
void step_fail_open(int32_t result, const char *detail);

void buffer_reserve(struct buffer *buffer, uint32_t extra);
void buffer_append(struct buffer *buffer, const void *data, uint32_t length);
void buffer_text(struct buffer *buffer, const char *text);
void buffer_format(struct buffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
void buffer_format_list(struct buffer *buffer, const char *format, va_list arguments);
void buffer_hex(struct buffer *buffer, const uint8_t *data, uint32_t length);
void *buffer_push(struct buffer *buffer, uint32_t size);
void *buffer_items(const struct buffer *buffer);

uint32_t read_u16(const uint8_t *data, uint32_t length, int64_t offset);
uint32_t read_u32(const uint8_t *data, uint32_t length, int64_t offset);
void write_u16(uint8_t *data, uint32_t offset, uint32_t value);
void write_u32(uint8_t *data, uint32_t offset, uint32_t value);
void write_u32_within(uint8_t *data, uint32_t length, int64_t offset, uint32_t value);
uint32_t get_u32(const uint8_t *data);
void copy_bytes(void *destination, const void *source, uint32_t length);
void fill_bytes(void *destination, uint8_t value, uint32_t length);
int bytes_equal(const void *first, const void *second, uint32_t length);
uint32_t text_length(const char *text);
int text_equal(const char *first, const char *second);
int text_ends_with(const char *text, const char *ending);
uint32_t fold_case(uint32_t character);
int names_equal(const char *first, const char *second);
int64_t align_up(int64_t value, int64_t alignment);
int64_t align_down(int64_t value, int64_t alignment);
int64_t index_of_byte(const uint8_t *data, uint32_t length, uint8_t value, int64_t start);

void crc32_prepare(void);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t crc32(const uint8_t *data, uint32_t length);

void stable_sort(void *items, uint32_t count, uint32_t size, int (*compare)(const void *first, const void *second));

void sha256(const uint8_t *data, uint32_t length, uint8_t digest[32], const char *stage);

struct rom_header {
    uint32_t dllfirst;
    uint32_t dlllast;
    uint32_t physfirst;
    uint32_t physlast;
    uint32_t nummods;
    uint32_t ram_start;
    uint32_t ram_free;
    uint32_t ram_end;
    uint32_t copy_entries;
    uint32_t copy_offset;
    uint32_t numfiles;
    uint32_t cpu_type;
};

struct rom_section {
    uint32_t virtual_size;
    uint32_t rva;
    uint32_t physical_size;
    uint32_t data_pointer;
    int64_t data_offset;
    uint32_t real_address;
    uint32_t flags;
    int compressed;
};

struct rom_module {
    const char *name;
    uint32_t toc_offset;
    uint32_t e32_offset;
    uint32_t o32_offset;
    uint32_t name_offset;
    uint32_t section_count;
    struct rom_section *sections;
};

struct rom_file {
    const char *name;
    uint32_t entry_offset;
    uint32_t attributes;
    uint32_t real_size;
    uint32_t compressed_size;
    uint32_t load_offset;
    uint32_t name_offset;
    int compressed;
};

struct rom_image {
    const uint8_t *bytes;
    uint32_t length;
    uint32_t rom_header_va;
    uint32_t header_offset;
    struct rom_header header;
    int modules_listed;
    struct rom_module *modules;
    int files_listed;
    struct rom_file *files;
};

enum region_kind {
    REGION_ROM_HEADER,
    REGION_MODULE_TOC,
    REGION_COPY_ENTRY,
    REGION_COPY_SOURCE,
    REGION_MODULE_E32,
    REGION_MODULE_O32,
    REGION_MODULE_NAME,
    REGION_MODULE_SECTION,
    REGION_FILE_DATA,
    REGION_FILE_NAME,
    REGION_KEPT_MODULE,
};

struct region {
    int64_t start;
    int64_t end;
    uint32_t kind;
    uint32_t index;
    uint32_t section;
};

int is_rom_image(const uint8_t *bytes, uint32_t length);
uint32_t image_identity(const uint8_t *bytes, uint32_t length);
void open_image(struct rom_image *image, const uint8_t *bytes, uint32_t length);
uint32_t image_offset_of(const struct rom_image *image, uint64_t va, uint64_t length);
const char *image_c_string(const struct rom_image *image, uint32_t va);
uint32_t image_module_count(const struct rom_image *image);
uint32_t image_file_count(const struct rom_image *image);
struct rom_module *image_modules(struct rom_image *image);
struct rom_file *image_files(struct rom_image *image);
struct rom_module *find_module(struct rom_image *image, const char *name);
struct rom_file *find_file(struct rom_image *image, const char *name);
struct rom_file *find_file_or_fail(struct rom_image *image, const char *name);
struct rom_module *find_module_or_fail(struct rom_image *image, const char *name);
const uint8_t *read_file_contents(struct rom_image *image, struct rom_file *file);
struct region *list_used_regions(struct rom_image *image, uint32_t *count);
char *region_name(struct rom_image *image, const struct region *region);
uint32_t module_table_va(const struct rom_image *image);

void patch_records(struct buffer *text, int64_t image_offset, const uint8_t *original, uint32_t original_length, const uint8_t *replacement, uint32_t replacement_length);
void patch_section_start(struct buffer *text, const char *name, uint32_t image_length, uint32_t extended_length, int first);
void patch_load_line(struct buffer *text, uint32_t offset, uint32_t length, uint32_t crc, const char *path);

struct word_change {
    uint32_t address;
    uint32_t original;
    uint32_t replacement;
    const char *purpose;
};

struct word_patch {
    const char *module;
    uint32_t count;
    const struct word_change *words;
};

void word_patch_records(struct buffer *text, struct rom_image *image, const struct word_patch *patch);

enum registry_entry_kind {
    REGISTRY_KEY = 0,
    REGISTRY_VALUE = 1,
};

enum registry_source {
    REGISTRY_LITERAL = 0,
    REGISTRY_DEVICE = 1,
    REGISTRY_DESCRIPTION = 2,
};

struct registry_entry {
    uint32_t kind;
    uint32_t hive;
    const char *key;
    const char *name;
    uint32_t type;
    uint32_t dword;
    const char *string;
};

const uint8_t *merge_registry(struct rom_image *image, const struct registry_entry *entries, uint32_t entry_count, uint32_t *merged_length);
void registry_string_records(struct buffer *text, struct rom_image *image, uint32_t hive, const char *key_path, const char *value_name, const char *new_text);

struct import_redirect_entry {
    int by_ordinal;
    uint32_t ordinal;
    const char *key;
    const char *name;
};

struct import_redirect {
    const char *from;
    const char *to;
    uint32_t count;
    const struct import_redirect_entry *imports;
};

struct byte_patch {
    uint32_t offset;
    uint32_t length;
    const uint8_t *original;
    const uint8_t *replacement;
    const char *purpose;
};

struct rom_spec {
    const char *module;
    uint32_t spare_sections;
    uint32_t redirect_count;
    const struct import_redirect *redirects;
    uint32_t patch_count;
    const struct byte_patch *patches;
};

struct dll_bytes {
    uint8_t *data;
    uint32_t length;
};

struct dll_bytes extract_rom_dll(struct rom_image *image, const char *module_name, uint32_t spare_sections);
struct dll_bytes redirect_imports(struct dll_bytes original, const struct import_redirect *redirect);
void patch_dll_bytes(struct dll_bytes dll, const struct rom_spec *spec);

int a7300_display_image(const uint8_t sha256_digest[32]);
void a7300_display_records(struct buffer *text, struct rom_image *image, uint32_t hook_address);

struct replacement {
    const char *name;
    const uint8_t *contents;
    uint32_t length;
};

struct skip_list {
    uint32_t minimum_span;
    uint32_t module_count;
    const char **modules;
    uint32_t file_count;
    const char **files;
    uint32_t tested_count;
    const uint8_t *tested;
};

struct bundle_request {
    struct rom_image *image;
    const uint8_t *image_sha256;
    int skip_forced;
    const struct skip_list *skip_list;
    const uint8_t *bstart;
    uint32_t bstart_length;
    const uint8_t *a7300_display;
    uint32_t a7300_display_length;
    uint32_t replacement_count;
    const struct replacement *replacements;
    int registry;
    uint32_t registry_entry_count;
    const struct registry_entry *registry_entries;
    uint32_t word_patch_count;
    const struct word_patch *word_patches;
};

struct replaced_module {
    const char *name;
    const char *card_path;
    const uint8_t *contents;
    uint32_t length;
};

struct bundle_result {
    struct buffer text;
    uint32_t replaced_count;
    struct replaced_module *replaced;
    uint32_t span_count;
    uint64_t skipped_bytes;
};

void make_bundle(const struct bundle_request *request, struct bundle_result *result);

struct pak_image {
    const uint8_t *sha256;
    uint32_t length;
    uint32_t profile;
    const char *device;
    const char *description;
};

struct pak_operation {
    uint32_t kind;
    const char *name;
    uint32_t blob;
};

struct pak_profile {
    const char *id;
    uint32_t first_operation;
    uint32_t operation_count;
};

struct pak_blob {
    const uint8_t *data;
    uint32_t length;
};

struct pak {
    const uint8_t *data;
    uint32_t length;
    uint32_t crc32;
    const char *release;
    uint32_t image_count;
    uint32_t profile_count;
    uint32_t operation_count;
    uint32_t blob_count;
    uint32_t images_offset;
    uint32_t profiles_offset;
    uint32_t operations_offset;
    uint32_t blobs_offset;
    uint32_t strings_offset;
    uint32_t strings_length;
    uint32_t bstart_blob;
    uint32_t skip_list_blob;
};

enum pak_operation_kind {
    PAK_MODULE_FILE = 1,
    PAK_MODULE_ROM = 2,
    PAK_REGISTRY = 3,
    PAK_WORD_PATCH = 4,
    PAK_A7300_DISPLAY = 5,
    PAK_CARD_FILE = 6,
    PAK_CARD_FILE_ROM = 7,
};

void pak_open(struct pak *pak, const uint8_t *data, uint32_t length);
struct pak_image pak_image(const struct pak *pak, uint32_t index);
struct pak_profile pak_profile(const struct pak *pak, uint32_t index);
struct pak_operation pak_operation(const struct pak *pak, uint32_t index);
struct pak_blob pak_blob(const struct pak *pak, uint32_t index);
void pak_skip_list(const struct pak *pak, struct skip_list *skip_list);
void pak_rom_spec(const struct pak *pak, uint32_t blob, struct rom_spec *spec);
uint32_t pak_registry(const struct pak *pak, uint32_t blob, const struct pak_image *image, struct buffer *entries);
void pak_word_patch(const struct pak *pak, uint32_t blob, struct word_patch *patch);

#endif
