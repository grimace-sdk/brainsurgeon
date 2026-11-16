#include "internal.h"

static struct generator *active;
static uint32_t crc32_table[256];

void generator_start(struct generator *generator, struct nkgen_call *call)
{
    fill_bytes(generator, 0, sizeof(*generator));
    generator->call = call;
    generator->arena = call->arena;
    generator->arena_length = call->arena_length;
    active = generator;
    crc32_prepare();
}

void generator_stop(void)
{
    active = 0;
}

struct generator *active_generator(void)
{
    return active;
}

void fail(int32_t code, const char *format, ...)
{
    struct buffer message = { (uint8_t *)active->call->message, 0, sizeof(active->call->message) - 1, 1 };
    va_list arguments;
    va_start(arguments, format);
    buffer_format_list(&message, format, arguments);
    va_end(arguments);
    message.data[message.length] = 0;
    active->call->result = code;
    return_to_failure_point(active->failure);
}

void *arena_allocate(uint32_t size)
{
    uint32_t start = (uint32_t)align_up(active->arena_used, 8);
    if (start > active->arena_length || size > active->arena_length - start) {
        fail(NKGEN_ARENA_FULL, "Arena full: %u bytes wanted, %u of %u used", size, active->arena_used, active->arena_length);
    }
    active->arena_used = start + size;
    if (active->arena_used > active->arena_peak) active->arena_peak = active->arena_used;
    fill_bytes(active->arena + start, 0, size);
    return active->arena + start;
}

void *arena_copy(const void *data, uint32_t size)
{
    void *copy = arena_allocate(size);
    copy_bytes(copy, data, size);
    return copy;
}

char *text_format(const char *format, ...)
{
    struct buffer text = { 0 };
    va_list arguments;
    va_start(arguments, format);
    buffer_format_list(&text, format, arguments);
    va_end(arguments);
    buffer_append(&text, "", 1);
    return (char *)text.data;
}

char *text_copy(const char *text)
{
    return arena_copy(text, text_length(text) + 1);
}

void log_line(const char *format, ...)
{
    uint32_t mark = active->arena_used;
    struct buffer line = { 0 };
    va_list arguments;
    va_start(arguments, format);
    buffer_format_list(&line, format, arguments);
    va_end(arguments);
    buffer_append(&line, "", 1);
    if (active->call->log) active->call->log(active->call->context, (const char *)line.data);
    active->arena_used = mark;
}

void report_progress(const char *stage, uint32_t done, uint32_t total)
{
    if (active->call->progress) active->call->progress(active->call->context, stage, done, total);
}

void step_begin(const char *name)
{
    struct step_list *list = &active->steps;
    if (list->count == list->capacity) {
        uint32_t capacity = list->capacity ? list->capacity * 2 : 16;
        struct nkgen_step *steps = arena_allocate(capacity * sizeof(*steps));
        if (list->count) copy_bytes(steps, list->steps, list->count * sizeof(*steps));
        list->steps = steps;
        list->capacity = capacity;
    }
    list->steps[list->count].name = text_copy(name);
    list->steps[list->count].result = -1;
    list->steps[list->count].detail = "";
    list->open = (int32_t)list->count;
    list->count++;
}

void step_end(const char *format, ...)
{
    struct step_list *list = &active->steps;
    struct buffer detail = { 0 };
    va_list arguments;
    va_start(arguments, format);
    buffer_format_list(&detail, format, arguments);
    va_end(arguments);
    buffer_append(&detail, "", 1);
    list->steps[list->open].result = NKGEN_OK;
    list->steps[list->open].detail = (const char *)detail.data;
    log_line("step %s: %s", list->steps[list->open].name, detail.data);
    list->open = -1;
}

void step_fail_open(int32_t result, const char *detail)
{
    struct step_list *list = &active->steps;
    if (list->open < 0) return;
    list->steps[list->open].result = result;
    list->steps[list->open].detail = detail;
    list->open = -1;
}

void buffer_reserve(struct buffer *buffer, uint32_t extra)
{
    if (buffer->fixed || buffer->capacity - buffer->length >= extra) return;
    uint32_t capacity = buffer->capacity ? buffer->capacity : 64;
    while (capacity - buffer->length < extra) {
        if (capacity > 0x7fffffffu) fail(NKGEN_ARENA_FULL, "Buffer of %u bytes is too large", buffer->length);
        capacity *= 2;
    }
    uint8_t *data = arena_allocate(capacity);
    if (buffer->length) copy_bytes(data, buffer->data, buffer->length);
    buffer->data = data;
    buffer->capacity = capacity;
}

void buffer_append(struct buffer *buffer, const void *data, uint32_t length)
{
    buffer_reserve(buffer, length);
    if (buffer->fixed && length > buffer->capacity - buffer->length) length = buffer->capacity - buffer->length;
    copy_bytes(buffer->data + buffer->length, data, length);
    buffer->length += length;
}

void *buffer_push(struct buffer *buffer, uint32_t size)
{
    buffer_reserve(buffer, size);
    void *item = buffer->data + buffer->length;
    fill_bytes(item, 0, size);
    buffer->length += size;
    return item;
}

void *buffer_items(const struct buffer *buffer)
{
    return buffer->data;
}

void buffer_text(struct buffer *buffer, const char *text)
{
    buffer_append(buffer, text, text_length(text));
}

static void buffer_number(struct buffer *buffer, uint64_t value, uint32_t base, int negative, uint32_t width, char pad)
{
    char digits[24];
    uint32_t count = 0;
    do {
        uint32_t digit = (uint32_t)(value % base);
        digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value);
    if (negative) buffer_append(buffer, "-", 1);
    while (width > count + (negative ? 1u : 0u)) {
        buffer_append(buffer, &pad, 1);
        width--;
    }
    while (count) buffer_append(buffer, &digits[--count], 1);
}

void buffer_format_list(struct buffer *buffer, const char *format, va_list arguments)
{
    const char *cursor = format;
    while (*cursor) {
        if (*cursor != '%') {
            buffer_append(buffer, cursor++, 1);
            continue;
        }
        cursor++;
        char pad = ' ';
        uint32_t width = 0;
        int long_long = 0;
        if (*cursor == '0') {
            pad = '0';
            cursor++;
        }
        while (*cursor >= '0' && *cursor <= '9') width = width * 10 + (uint32_t)(*cursor++ - '0');
        if (cursor[0] == 'l' && cursor[1] == 'l') {
            long_long = 1;
            cursor += 2;
        }
        char conversion = *cursor ? *cursor++ : 0;
        if (conversion == 's') {
            const char *text = va_arg(arguments, const char *);
            buffer_text(buffer, text ? text : "");
        } else if (conversion == 'c') {
            char character = (char)va_arg(arguments, int);
            buffer_append(buffer, &character, 1);
        } else if (conversion == 'd') {
            int64_t value = long_long ? va_arg(arguments, long long) : va_arg(arguments, int32_t);
            buffer_number(buffer, value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value, 10, value < 0, width, pad);
        } else if (conversion == 'u' || conversion == 'x') {
            uint64_t value = long_long ? va_arg(arguments, unsigned long long) : va_arg(arguments, uint32_t);
            buffer_number(buffer, value, conversion == 'x' ? 16 : 10, 0, width, pad);
        } else if (conversion == '%') {
            buffer_append(buffer, "%", 1);
        }
    }
}

void buffer_format(struct buffer *buffer, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    buffer_format_list(buffer, format, arguments);
    va_end(arguments);
}

void buffer_hex(struct buffer *buffer, const uint8_t *data, uint32_t length)
{
    static const char digits[] = "0123456789abcdef";
    buffer_reserve(buffer, length * 2);
    for (uint32_t index = 0; index < length; index++) {
        char pair[2] = { digits[data[index] >> 4], digits[data[index] & 15] };
        buffer_append(buffer, pair, 2);
    }
}

uint32_t read_u16(const uint8_t *data, uint32_t length, int64_t offset)
{
    check(offset >= 0 && offset + 2 <= (int64_t)length, "Read of 2 bytes at %lld outside %u", (long long)offset, length);
    return (uint32_t)data[offset] | (uint32_t)data[offset + 1] << 8;
}

uint32_t read_u32(const uint8_t *data, uint32_t length, int64_t offset)
{
    check(offset >= 0 && offset + 4 <= (int64_t)length, "Read of 4 bytes at %lld outside %u", (long long)offset, length);
    return get_u32(data + offset);
}

uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void write_u16(uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
}

void write_u32(uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
    data[offset + 2] = (uint8_t)(value >> 16);
    data[offset + 3] = (uint8_t)(value >> 24);
}

void write_u32_within(uint8_t *data, uint32_t length, int64_t offset, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        int64_t position = offset + index;
        if (position >= 0 && position < (int64_t)length) data[position] = (uint8_t)(value >> (index * 8));
    }
}

void copy_bytes(void *destination, const void *source, uint32_t length)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    if (to == from || !length) return;
    if (to < from) {
        for (uint32_t index = 0; index < length; index++) to[index] = from[index];
    } else {
        for (uint32_t index = length; index > 0; index--) to[index - 1] = from[index - 1];
    }
}

void fill_bytes(void *destination, uint8_t value, uint32_t length)
{
    uint8_t *to = destination;
    for (uint32_t index = 0; index < length; index++) to[index] = value;
}

int bytes_equal(const void *first, const void *second, uint32_t length)
{
    const uint8_t *a = first;
    const uint8_t *b = second;
    for (uint32_t index = 0; index < length; index++) {
        if (a[index] != b[index]) return 0;
    }
    return 1;
}

uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    return length;
}

int text_equal(const char *first, const char *second)
{
    uint32_t length = text_length(first);
    return length == text_length(second) && bytes_equal(first, second, length);
}

int text_ends_with(const char *text, const char *ending)
{
    uint32_t length = text_length(text);
    uint32_t ending_length = text_length(ending);
    return length >= ending_length && bytes_equal(text + length - ending_length, ending, ending_length);
}

uint32_t fold_case(uint32_t character)
{
    if (character >= 'A' && character <= 'Z') return character + 32;
    if (character >= 0xc0 && character <= 0xde && character != 0xd7) return character + 32;
    return character;
}

int names_equal(const char *first, const char *second)
{
    const uint8_t *a = (const uint8_t *)first;
    const uint8_t *b = (const uint8_t *)second;
    while (*a && *b) {
        if (fold_case(*a) != fold_case(*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int64_t align_down(int64_t value, int64_t alignment)
{
    int64_t quotient = value / alignment;
    if (value % alignment != 0 && value < 0) quotient--;
    return quotient * alignment;
}

int64_t align_up(int64_t value, int64_t alignment)
{
    int64_t quotient = value / alignment;
    if (value % alignment != 0 && value > 0) quotient++;
    return quotient * alignment;
}

int64_t index_of_byte(const uint8_t *data, uint32_t length, uint8_t value, int64_t start)
{
    for (int64_t index = start < 0 ? 0 : start; index < (int64_t)length; index++) {
        if (data[index] == value) return index;
    }
    return -1;
}

void crc32_prepare(void)
{
    for (uint32_t index = 0; index < 256; index++) {
        uint32_t value = index;
        for (uint32_t bit = 0; bit < 8; bit++) value = value & 1 ? 0xedb88320u ^ (value >> 1) : value >> 1;
        crc32_table[index] = value;
    }
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t value = ~crc;
    for (uint32_t index = 0; index < length; index++) value = crc32_table[(value ^ data[index]) & 0xff] ^ (value >> 8);
    return ~value;
}

uint32_t crc32(const uint8_t *data, uint32_t length)
{
    return crc32_update(0, data, length);
}

void stable_sort(void *items, uint32_t count, uint32_t size, int (*compare)(const void *first, const void *second))
{
    if (count < 2) return;
    uint32_t mark = active->arena_used;
    uint8_t *source = items;
    uint8_t *target = arena_allocate(count * size);
    for (uint32_t width = 1; width < count; width *= 2) {
        for (uint32_t left = 0; left < count; left += 2 * width) {
            uint32_t middle = left + width < count ? left + width : count;
            uint32_t right = left + 2 * width < count ? left + 2 * width : count;
            uint32_t first = left;
            uint32_t second = middle;
            uint32_t output = left;
            while (first < middle || second < right) {
                int take_first = second >= right || (first < middle && compare(source + first * size, source + second * size) <= 0);
                uint32_t from = take_first ? first++ : second++;
                copy_bytes(target + output++ * size, source + from * size, size);
            }
        }
        uint8_t *swap = source;
        source = target;
        target = swap;
    }
    if (source != items) copy_bytes(items, source, count * size);
    active->arena_used = mark;
}
