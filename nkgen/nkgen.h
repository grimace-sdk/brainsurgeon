#ifndef NKGEN_H
#define NKGEN_H

#include <stdint.h>

#define NKGEN_HEADER_MAGIC "NKGENBIN"
#define NKGEN_HEADER_VERSION 1u
#define NKGEN_ABI_VERSION 1u
#define NKGEN_PAK_FORMAT 1u
#define NKGEN_CALL_MAGIC 0x4347484eu
#define NKGEN_HEADER_SIZE 72u

#define NKGEN_FLAG_NO_PROFILE 1u
#define NKGEN_FLAG_SKIP 2u

enum nkgen_result {
    NKGEN_OK = 0,
    NKGEN_BAD_CALL = 1,
    NKGEN_BAD_PAK = 2,
    NKGEN_CHECK_FAILED = 3,
    NKGEN_ARENA_FULL = 4,
};

struct nkgen_header {
    char magic[8];
    uint32_t header_version;
    uint32_t abi_version;
    uint32_t link_address;
    uint32_t file_length;
    uint32_t bss_length;
    uint32_t entry_offset;
    uint32_t payload_crc32;
    uint32_t pak_format;
    char version[32];
};

struct nkgen_output {
    const char *path;
    const uint8_t *data;
    uint32_t length;
    uint32_t crc32;
};

struct nkgen_step {
    const char *name;
    int32_t result;
    const char *detail;
};

struct nkgen_call {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size;
    const uint8_t *image;
    uint32_t image_length;
    const uint8_t *pak;
    uint32_t pak_length;
    uint8_t *arena;
    uint32_t arena_length;
    uint32_t flags;
    void (*progress)(void *context, const char *stage, uint32_t done, uint32_t total);
    void (*log)(void *context, const char *line);
    void *context;
    int32_t result;
    uint32_t output_count;
    const struct nkgen_output *outputs;
    uint32_t step_count;
    const struct nkgen_step *steps;
    uint32_t known;
    const char *profile;
    uint32_t identity_crc32;
    uint8_t image_sha256[32];
    char message[160];
};

int32_t nkgen_generate(struct nkgen_call *call);

#endif
