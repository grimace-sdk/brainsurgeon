#include "internal.h"

#define FLAT_REGISTRY_SIGNATURE 0x1d8374b2u
#define REGISTRY_SZ 1u
#define REGISTRY_EXPAND_SZ 2u
#define REGISTRY_DWORD 4u
#define REGISTRY_MULTI_SZ 7u

struct utf16 {
    const uint8_t *units;
    uint32_t count;
};

struct registry_value {
    struct utf16 name;
    uint32_t type;
    const uint8_t *data;
    uint32_t data_size;
    int raw;
};

struct registry_key {
    uint32_t hive;
    struct utf16 name;
    struct buffer values;
};

static struct utf16 utf16_from_text(const char *text)
{
    uint32_t length = text_length(text);
    uint8_t *units = arena_allocate(length * 2);
    for (uint32_t index = 0; index < length; index++) units[index * 2] = (uint8_t)text[index];
    struct utf16 result = { units, length };
    return result;
}

static uint32_t unit_at(struct utf16 text, uint32_t index)
{
    return (uint32_t)text.units[index * 2] | (uint32_t)text.units[index * 2 + 1] << 8;
}

static int utf16_names_equal(struct utf16 first, struct utf16 second)
{
    if (first.count != second.count) return 0;
    for (uint32_t index = 0; index < first.count; index++) {
        if (fold_case(unit_at(first, index)) != fold_case(unit_at(second, index))) return 0;
    }
    return 1;
}

static char *utf16_to_text(struct utf16 text)
{
    char *result = arena_allocate(text.count + 1);
    for (uint32_t index = 0; index < text.count; index++) {
        uint32_t unit = unit_at(text, index);
        result[index] = unit < 0x80 && unit ? (char)unit : '?';
    }
    return result;
}

static struct utf16 registry_string(const uint8_t *bytes, uint32_t length, int64_t start, uint32_t characters, int64_t end)
{
    check(characters > 0 && start + (int64_t)characters * 2 <= end, "Registry string outside record");
    struct utf16 text = { bytes + start, characters };
    check(read_u16(bytes, length, start + (int64_t)(characters - 1) * 2) == 0, "Registry string lacks terminator");
    text.count--;
    return text;
}

static uint32_t parse_flat_registry(const uint8_t *bytes, uint32_t length, struct buffer *keys)
{
    check(read_u32(bytes, length, 0) == FLAT_REGISTRY_SIGNATURE, "Unexpected flat-registry signature");
    check(read_u32(bytes, length, 4) == length, "Flat-registry length mismatch");
    struct registry_key *current = 0;
    uint32_t offset = 8;
    uint32_t key_count = 0;
    while (offset < length) {
        check(offset + 4 <= length, "Truncated registry record");
        uint32_t record_length = read_u16(bytes, length, offset);
        uint32_t kind = read_u16(bytes, length, offset + 2);
        uint32_t start = offset + 4;
        uint32_t end = start + record_length;
        check(end <= length, "Registry record exceeds input");
        if (kind == 1) {
            check(record_length >= 4, "Registry key record too short");
            uint32_t characters = read_u16(bytes, length, start + 2);
            check(record_length == 4 + characters * 2, "Registry key record length mismatch");
            uint32_t hive = read_u16(bytes, length, start);
            struct utf16 name = registry_string(bytes, length, start + 4, characters, end);
            current = buffer_push(keys, sizeof(struct registry_key));
            current->hive = hive;
            current->name = name;
            key_count++;
        } else if (kind == 2) {
            check(current && record_length >= 6, "Registry value has no key");
            uint32_t type = read_u16(bytes, length, start);
            uint32_t characters = read_u16(bytes, length, start + 2);
            uint32_t size = read_u16(bytes, length, start + 4);
            check(record_length == 6 + characters * 2 + size, "Registry value record length mismatch");
            struct utf16 name = { bytes + start + 6, 0 };
            if (characters) name = registry_string(bytes, length, start + 6, characters, end);
            uint32_t data_start = start + 6 + characters * 2;
            if (type == REGISTRY_DWORD) {
                check(size == 4, "Registry DWORD is not 4 bytes");
            } else if (type == REGISTRY_SZ || type == REGISTRY_EXPAND_SZ || type == REGISTRY_MULTI_SZ) {
                check(size % 2 == 0, "Registry string has an odd length");
            }
            struct registry_value *value = buffer_push(&current->values, sizeof(struct registry_value));
            value->name = name;
            value->type = type;
            value->data = bytes + data_start;
            value->data_size = size;
            value->raw = 1;
        } else {
            fail(NKGEN_CHECK_FAILED, "Unsupported registry record kind %u at 0x%x", kind, offset);
        }
        offset = end;
    }
    check(offset == length, "Registry records do not end at the file end");
    return key_count;
}

static uint32_t value_count(const struct registry_key *key)
{
    return key->values.length / sizeof(struct registry_value);
}

static struct registry_value *value_at(const struct registry_key *key, uint32_t index)
{
    struct registry_value *values = buffer_items(&key->values);
    return &values[index];
}

static struct registry_key *find_key(struct buffer *keys, uint32_t hive, struct utf16 name)
{
    uint32_t count = keys->length / sizeof(struct registry_key);
    struct registry_key *list = buffer_items(keys);
    for (uint32_t index = 0; index < count; index++) {
        if (list[index].hive == hive && utf16_names_equal(list[index].name, name)) return &list[index];
    }
    return 0;
}

static struct registry_value *find_value(struct registry_key *key, struct utf16 name)
{
    for (uint32_t index = 0; index < value_count(key); index++) {
        if (utf16_names_equal(value_at(key, index)->name, name)) return value_at(key, index);
    }
    return 0;
}

static struct registry_key *add_key(struct buffer *keys, uint32_t hive, struct utf16 name)
{
    struct registry_key *key = buffer_push(keys, sizeof(struct registry_key));
    key->hive = hive;
    key->name = name;
    return key;
}

static void encode_value(struct registry_value *value, uint32_t type, uint32_t dword, const char *string)
{
    value->type = type;
    value->raw = 0;
    if (type == REGISTRY_DWORD) {
        uint8_t *data = arena_allocate(4);
        write_u32(data, 0, dword);
        value->data = data;
        value->data_size = 4;
        return;
    }
    struct utf16 encoded = utf16_from_text(string);
    uint8_t *data = arena_allocate(encoded.count * 2 + 2);
    copy_bytes(data, encoded.units, encoded.count * 2);
    value->data = data;
    value->data_size = encoded.count * 2 + 2;
}

static void set_registry_value(struct buffer *keys, uint32_t hive, const char *key_path, const char *name, uint32_t type, uint32_t dword, const char *string)
{
    struct utf16 key_name = utf16_from_text(key_path);
    struct registry_key *key = find_key(keys, hive, key_name);
    if (!key) key = add_key(keys, hive, key_name);
    struct utf16 value_name = utf16_from_text(name);
    struct registry_value *value = find_value(key, value_name);
    if (!value) {
        value = buffer_push(&key->values, sizeof(struct registry_value));
        value->name = value_name;
    }
    encode_value(value, type, dword, string);
}

static void apply_entries(struct buffer *keys, const struct registry_entry *entries, uint32_t entry_count)
{
    for (uint32_t index = 0; index < entry_count; index++) {
        const struct registry_entry *entry = &entries[index];
        if (entry->kind == REGISTRY_KEY) {
            struct utf16 name = utf16_from_text(entry->key);
            if (!find_key(keys, entry->hive, name)) add_key(keys, entry->hive, name);
            continue;
        }
        set_registry_value(keys, entry->hive, entry->key, entry->name, entry->type, entry->dword, entry->string);
    }
}

static uint64_t registry_length(struct buffer *keys)
{
    uint32_t count = keys->length / sizeof(struct registry_key);
    struct registry_key *list = buffer_items(keys);
    uint64_t total = 8;
    for (uint32_t index = 0; index < count; index++) {
        total += 4 + 4 + ((uint64_t)list[index].name.count + 1) * 2;
        for (uint32_t value_index = 0; value_index < value_count(&list[index]); value_index++) {
            struct registry_value *value = value_at(&list[index], value_index);
            total += 4 + 6 + (value->name.count ? ((uint64_t)value->name.count + 1) * 2 : 0) + value->data_size;
        }
    }
    return total;
}

static void pad_to_word(struct buffer *keys)
{
    if (registry_length(keys) % 4 == 0) return;
    uint32_t count = keys->length / sizeof(struct registry_key);
    struct registry_key *list = buffer_items(keys);
    for (uint32_t index = count; index > 0; index--) {
        struct registry_key *key = &list[index - 1];
        for (uint32_t value_index = value_count(key); value_index > 0; value_index--) {
            struct registry_value *value = value_at(key, value_index - 1);
            if ((value->type == REGISTRY_SZ || value->type == REGISTRY_EXPAND_SZ) && !value->raw) {
                uint8_t *data = arena_allocate(value->data_size + 2);
                copy_bytes(data, value->data, value->data_size);
                value->data = data;
                value->data_size += 2;
                value->raw = 1;
                return;
            }
        }
    }
    fail(NKGEN_CHECK_FAILED, "No string value to pad the registry to a 4-byte length");
}

static void write_record_header(struct buffer *out, uint32_t body_length, uint32_t kind)
{
    uint8_t header[4];
    write_u16(header, 0, body_length);
    write_u16(header, 2, kind);
    buffer_append(out, header, 4);
}

static const uint8_t *write_registry(struct buffer *keys, uint32_t *written_length)
{
    pad_to_word(keys);
    uint64_t total = registry_length(keys);
    check(total <= 0x7fffffffu, "Registry of %llu bytes is too large", (unsigned long long)total);
    struct buffer out = { 0 };
    buffer_reserve(&out, (uint32_t)total);
    uint8_t prefix[8];
    write_u32(prefix, 0, FLAT_REGISTRY_SIGNATURE);
    write_u32(prefix, 4, (uint32_t)total);
    buffer_append(&out, prefix, 8);
    uint32_t count = keys->length / sizeof(struct registry_key);
    struct registry_key *list = buffer_items(keys);
    static const uint8_t terminator[2] = { 0, 0 };
    for (uint32_t index = 0; index < count; index++) {
        struct registry_key *key = &list[index];
        uint32_t name_bytes = (key->name.count + 1) * 2;
        write_record_header(&out, 4 + name_bytes, 1);
        uint8_t key_header[4];
        write_u16(key_header, 0, key->hive);
        write_u16(key_header, 2, name_bytes / 2);
        buffer_append(&out, key_header, 4);
        buffer_append(&out, key->name.units, key->name.count * 2);
        buffer_append(&out, terminator, 2);
        for (uint32_t value_index = 0; value_index < value_count(key); value_index++) {
            struct registry_value *value = value_at(key, value_index);
            uint32_t value_name_bytes = value->name.count ? (value->name.count + 1) * 2 : 0;
            check(value->data_size <= 0xffff && value_name_bytes + value->data_size + 6 <= 0xffff, "%s\\%s is too large",
                utf16_to_text(key->name), utf16_to_text(value->name));
            write_record_header(&out, 6 + value_name_bytes + value->data_size, 2);
            uint8_t value_header[6];
            write_u16(value_header, 0, value->type);
            write_u16(value_header, 2, value_name_bytes / 2);
            write_u16(value_header, 4, value->data_size);
            buffer_append(&out, value_header, 6);
            if (value_name_bytes) {
                buffer_append(&out, value->name.units, value->name.count * 2);
                buffer_append(&out, terminator, 2);
            }
            buffer_append(&out, value->data, value->data_size);
        }
    }
    *written_length = out.length;
    return out.data;
}

const uint8_t *merge_registry(struct rom_image *image, const struct registry_entry *entries, uint32_t entry_count, uint32_t *merged_length)
{
    struct rom_file *file = find_file_or_fail(image, "default.fdf");
    const uint8_t *contents = read_file_contents(image, file);
    struct buffer keys = { 0 };
    parse_flat_registry(contents, file->real_size, &keys);
    set_registry_value(&keys, 2, "init", "Launch50", REGISTRY_SZ, 0, "bstart.exe");
    apply_entries(&keys, entries, entry_count);
    return write_registry(&keys, merged_length);
}

void registry_string_records(struct buffer *text, struct rom_image *image, uint32_t hive, const char *key_path, const char *value_name, const char *new_text)
{
    struct rom_file *file = find_file_or_fail(image, "default.fdf");
    check(!file->compressed, "default.fdf is compressed in this image; the registry cannot be patched in place");
    const uint8_t *contents = read_file_contents(image, file);
    struct buffer keys = { 0 };
    parse_flat_registry(contents, file->real_size, &keys);
    struct registry_key *key = find_key(&keys, hive, utf16_from_text(key_path));
    check(key, "Registry key %s not found", key_path);
    struct registry_value *value = find_value(key, utf16_from_text(value_name));
    check(value, "Registry value %s\\%s not found", key_path, value_name);
    struct utf16 encoded = utf16_from_text(new_text);
    uint32_t encoded_length = encoded.count * 2 + 2;
    check(encoded_length <= value->data_size, "%s does not fit in %u bytes", new_text, value->data_size);
    uint32_t offset = file->load_offset + (uint32_t)(value->data - contents);
    uint8_t *replacement = arena_allocate(value->data_size);
    copy_bytes(replacement, encoded.units, encoded.count * 2);
    patch_records(text, offset, image->bytes + offset, value->data_size, replacement, value->data_size);
}
