#include "internal.h"

void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *first, const void *second, size_t length);

void *memcpy(void *destination, const void *source, size_t length)
{
    copy_bytes(destination, source, (uint32_t)length);
    return destination;
}

void *memmove(void *destination, const void *source, size_t length)
{
    copy_bytes(destination, source, (uint32_t)length);
    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    fill_bytes(destination, (uint8_t)value, (uint32_t)length);
    return destination;
}

int memcmp(const void *first, const void *second, size_t length)
{
    const uint8_t *a = first;
    const uint8_t *b = second;
    for (size_t index = 0; index < length; index++) {
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    }
    return 0;
}
