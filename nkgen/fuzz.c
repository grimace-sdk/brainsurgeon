#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define PAK_HEADER_FIELDS 16u
#define BLOB_RECORD_SIZE 12u

static uint64_t random_state = 0x9e3779b97f4a7c15ull;

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return (uint32_t)(random_state >> 16);
}

static uint32_t random_below(uint32_t limit)
{
    return limit ? next_random() % limit : 0;
}

static unsigned char *read_whole_file(const char *path, uint32_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "%s: cannot open\n", path);
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "%s: cannot read\n", path);
        exit(1);
    }
    fclose(file);
    *length = (uint32_t)size;
    return data;
}

static uint32_t word_at(const unsigned char *data, uint32_t offset)
{
    return (uint32_t)data[offset] | (uint32_t)data[offset + 1] << 8 | (uint32_t)data[offset + 2] << 16 | (uint32_t)data[offset + 3] << 24;
}

static void set_word(unsigned char *data, uint32_t offset, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) data[offset + index] = (unsigned char)(value >> (index * 8));
}

static void reseal_pak(unsigned char *pak, uint32_t length)
{
    uint32_t blobs = word_at(pak, 56);
    uint32_t count = word_at(pak, 60);
    for (uint32_t index = 0; index < count; index++) {
        uint64_t record = (uint64_t)blobs + (uint64_t)index * BLOB_RECORD_SIZE;
        if (record + BLOB_RECORD_SIZE > length) break;
        uint32_t offset = word_at(pak, (uint32_t)record);
        uint32_t blob_length = word_at(pak, (uint32_t)record + 4);
        if ((uint64_t)offset + blob_length > length) continue;
        set_word(pak, (uint32_t)record + 8, crc32(pak + offset, blob_length));
    }
    set_word(pak, 12, length);
    set_word(pak, 16, crc32(pak + 20, length - 20));
}

static uint32_t identity_offset(const unsigned char *image, uint32_t length)
{
    if (length < 0x48) return 0;
    uint32_t offset = word_at(image, 0x44) - 0x80200000u;
    return offset < length ? offset : 0;
}

static void mutate_image(unsigned char *image, uint32_t length, uint32_t table_start)
{
    uint32_t changes = 1 + random_below(4);
    for (uint32_t change = 0; change < changes; change++) {
        uint32_t offset;
        if (random_below(4)) offset = table_start + random_below(length - table_start);
        else offset = random_below(length);
        if (random_below(2)) image[offset] ^= (unsigned char)(1u << random_below(8));
        else image[offset] = (unsigned char)next_random();
    }
}

static void mutate_pak(unsigned char *pak, uint32_t length)
{
    if (random_below(2)) {
        uint32_t field = 2 + random_below(PAK_HEADER_FIELDS - 2);
        uint32_t offset = 8 + field * 4;
        uint32_t value = word_at(pak, offset);
        uint32_t choice = random_below(4);
        if (choice == 0) value ^= 1u << random_below(32);
        else if (choice == 1) value += random_below(64) - 32;
        else if (choice == 2) value = next_random();
        else value = 0xffffffffu - random_below(16);
        set_word(pak, offset, value);
    } else {
        uint32_t changes = 1 + random_below(8);
        for (uint32_t change = 0; change < changes; change++) pak[72 + random_below(length - 72)] ^= (unsigned char)(1u << random_below(8));
    }
    reseal_pak(pak, length);
}

static void ignore_log(void *context, const char *line)
{
    (void)context;
    (void)line;
}

int main(int argc, char **argv)
{
    const char *image_path = 0;
    const char *pak_path = 0;
    uint32_t iterations = 2000;
    for (int index = 1; index + 1 < argc; index += 2) {
        if (!strcmp(argv[index], "--image")) image_path = argv[index + 1];
        else if (!strcmp(argv[index], "--pak")) pak_path = argv[index + 1];
        else if (!strcmp(argv[index], "--iterations")) iterations = (uint32_t)strtoul(argv[index + 1], 0, 10);
        else if (!strcmp(argv[index], "--seed")) random_state ^= strtoull(argv[index + 1], 0, 10);
    }
    if (!image_path || !pak_path) {
        fprintf(stderr, "Usage: nkgen-fuzz --image NK_IMAGE --pak nkgen.pak [--iterations N] [--seed N]\n\n"
            "Runs the generator on mutated copies of the image and pak (truncations, flipped bytes, bad header fields\n"
            "with the CRCs fixed up). Every run must return a result; built with sanitizers, any bad access stops it.\n");
        return 2;
    }
    uint32_t image_length = 0;
    uint32_t pak_length = 0;
    unsigned char *image = read_whole_file(image_path, &image_length);
    unsigned char *pak = read_whole_file(pak_path, &pak_length);
    uint32_t table_start = identity_offset(image, image_length);
    crc32_prepare();
    uint32_t results[5] = { 0 };

    for (uint32_t iteration = 0; iteration < iterations; iteration++) {
        uint32_t kind = iteration % 4;
        uint32_t trial_image_length = kind == 1 ? random_below(image_length + 1) : image_length;
        unsigned char *trial_image = malloc(trial_image_length ? trial_image_length : 1);
        unsigned char *trial_pak = malloc(pak_length);
        memcpy(trial_image, image, trial_image_length);
        memcpy(trial_pak, pak, pak_length);
        if (kind == 0) mutate_image(trial_image, trial_image_length, table_start);
        if (kind == 2) mutate_pak(trial_pak, pak_length);
        if (kind == 3) {
            mutate_image(trial_image, trial_image_length, table_start);
            mutate_pak(trial_pak, pak_length);
        }
        uint32_t arena_length = random_below(8) ? 8u << 20 : 4096u + random_below(256u << 10);
        uint8_t *arena = malloc(arena_length);
        struct nkgen_call call;
        memset(&call, 0, sizeof(call));
        call.magic = NKGEN_CALL_MAGIC;
        call.abi_version = NKGEN_ABI_VERSION;
        call.size = sizeof(call);
        call.image = trial_image;
        call.image_length = trial_image_length;
        call.pak = trial_pak;
        call.pak_length = pak_length;
        call.arena = arena;
        call.arena_length = arena_length;
        call.flags = random_below(4);
        call.log = ignore_log;
        int32_t result = nkgen_generate(&call);
        if (result < 0 || result > NKGEN_ARENA_FULL) {
            fprintf(stderr, "iteration %u: unexpected result %d\n", iteration, result);
            return 1;
        }
        if (result == NKGEN_OK) {
            for (uint32_t index = 0; index < call.output_count; index++) {
                const struct nkgen_output *output = &call.outputs[index];
                if (!output->path || crc32(output->data, output->length) != output->crc32) {
                    fprintf(stderr, "iteration %u: output %u is inconsistent\n", iteration, index);
                    return 1;
                }
            }
        } else if (!call.message[0]) {
            fprintf(stderr, "iteration %u: result %d with no message\n", iteration, result);
            return 1;
        }
        results[result]++;
        free(arena);
        free(trial_image);
        free(trial_pak);
    }
    printf("%u runs: %u generated, %u bad pak, %u refused, %u arena full\n", iterations, results[NKGEN_OK], results[NKGEN_BAD_PAK],
        results[NKGEN_CHECK_FAILED], results[NKGEN_ARENA_FULL]);
    free(image);
    free(pak);
    return 0;
}
