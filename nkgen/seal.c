#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nkgen.h"

#define LINK_ADDRESS 0x42000000u
#define RESERVED_LENGTH 0x100000u

static uint32_t crc32_bytes(const unsigned char *data, size_t length)
{
    uint32_t value = 0xffffffffu;
    for (size_t index = 0; index < length; index++) {
        value ^= data[index];
        for (int bit = 0; bit < 8; bit++) value = value & 1 ? 0xedb88320u ^ (value >> 1) : value >> 1;
    }
    return ~value;
}

static uint32_t get_word(const unsigned char *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void put_word(unsigned char *data, uint32_t value)
{
    for (int index = 0; index < 4; index++) data[index] = (unsigned char)(value >> (index * 8));
}

static int refuse(const char *path, const char *reason)
{
    fprintf(stderr, "%s: %s\n", path, reason);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: nkgen-seal nkgen.bin\n\nChecks the generator header and fills in payload_crc32.\n");
        return 2;
    }
    const char *path = argv[1];
    FILE *file = fopen(path, "rb");
    if (!file) return refuse(path, "cannot open");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < NKGEN_HEADER_SIZE || size > (long)RESERVED_LENGTH) {
        fclose(file);
        return refuse(path, "size is outside the header size and the reserved 1 MB");
    }
    unsigned char *data = malloc((size_t)size);
    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) return refuse(path, "cannot read");

    uint32_t file_length = get_word(data + 20);
    uint32_t bss_length = get_word(data + 24);
    uint32_t entry_offset = get_word(data + 28);
    if (memcmp(data, NKGEN_HEADER_MAGIC, 8)) return refuse(path, "no NKGENBIN magic");
    if (get_word(data + 8) != NKGEN_HEADER_VERSION || get_word(data + 12) != NKGEN_ABI_VERSION) return refuse(path, "header or ABI version differs");
    if (get_word(data + 16) != LINK_ADDRESS) return refuse(path, "not linked at 0x42000000");
    if (file_length != (uint32_t)size) return refuse(path, "file_length differs from the file size");
    if ((uint64_t)file_length + bss_length > RESERVED_LENGTH) return refuse(path, "code, data and BSS exceed the reserved 1 MB");
    if (entry_offset < NKGEN_HEADER_SIZE || entry_offset >= file_length || entry_offset % 4) return refuse(path, "entry offset is outside the code");
    if (get_word(data + 36) != NKGEN_PAK_FORMAT) return refuse(path, "pak format differs");
    if (data[NKGEN_HEADER_SIZE - 1] != 0) return refuse(path, "version is not terminated");

    put_word(data + 32, crc32_bytes(data + NKGEN_HEADER_SIZE, (size_t)size - NKGEN_HEADER_SIZE));
    file = fopen(path, "wb");
    if (!file || fwrite(data, 1, (size_t)size, file) != (size_t)size || fclose(file) != 0) return refuse(path, "cannot write");
    printf("%s: %ld bytes, BSS %u bytes, entry offset 0x%x, payload CRC-32 %08x, version %s\n", path, size, bss_length, entry_offset,
        get_word(data + 32), (const char *)data + 40);
    free(data);
    return 0;
}
