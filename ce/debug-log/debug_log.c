#include <stdint.h>

#define DEBUG_LOG_MAGIC          0x474c4543u
#define DEBUG_LOG_BYTES          0x10000u
#define DEBUG_LOG_HEADER_BYTES   16u
#define DEBUG_LOG_CAPACITY       (DEBUG_LOG_BYTES - DEBUG_LOG_HEADER_BYTES)

struct debug_log {
    uint32_t magic;
    uint32_t written;
    uint32_t capacity;
    uint32_t reserved;
    uint8_t text[DEBUG_LOG_CAPACITY];
};

void debug_log_append(const uint16_t *text, uint32_t ring)
{
    volatile struct debug_log *log = (volatile struct debug_log *)ring;
    const uint16_t *character;

    if (log->magic != DEBUG_LOG_MAGIC || log->capacity != DEBUG_LOG_CAPACITY) {
        log->written = 0;
        log->capacity = DEBUG_LOG_CAPACITY;
        log->reserved = 0;
        log->magic = DEBUG_LOG_MAGIC;
    }
    for (character = text; *character; character++) {
        log->text[log->written % DEBUG_LOG_CAPACITY] = *character < 0x80 ? (uint8_t)*character : '?';
        log->written++;
    }
}
