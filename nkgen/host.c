#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "nkgen.h"

#define DEFAULT_ARENA_MEGABYTES 24u

struct host_log {
    FILE *console;
    char *text;
    size_t length;
    size_t capacity;
    int quiet;
};

static const char *usage =
    "Usage: nkgen-host --image NK_IMAGE --pak nkgen.pak [--card CARD_DIRECTORY] [--no-profile] [--skip] [--arena-megabytes N] [--quiet]\n"
    "\n"
    "Runs the on-device generator on the host: writes boot.nkp, its modules and the profile's card files into CARD_DIRECTORY,\n"
    "with nkgen.log in the bundle folder. --no-profile makes the bstart-only bundle; --skip turns skipping on for any image.\n";

static unsigned char *read_whole_file(const char *path, uint32_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || size > 0x7fffffffL) {
        fprintf(stderr, "%s: cannot read\n", path);
        exit(1);
    }
    unsigned char *data = malloc(size ? (size_t)size : 1);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "%s: cannot read\n", path);
        exit(1);
    }
    fclose(file);
    *length = (uint32_t)size;
    return data;
}

static void make_directories(const char *path)
{
    char *copy = strdup(path);
    for (char *cursor = copy + 1; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "%s: %s\n", copy, strerror(errno));
            exit(1);
        }
        *cursor = '/';
    }
    free(copy);
}

static void write_whole_file(const char *path, const void *data, size_t length)
{
    make_directories(path);
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(data, 1, length, file) != length || fclose(file) != 0) {
        fprintf(stderr, "%s: cannot write\n", path);
        exit(1);
    }
}

static void append_log(struct host_log *log, const char *line)
{
    size_t length = strlen(line);
    if (log->length + length + 2 > log->capacity) {
        log->capacity = (log->capacity + length + 2) * 2;
        log->text = realloc(log->text, log->capacity);
    }
    memcpy(log->text + log->length, line, length);
    log->length += length;
    log->text[log->length++] = '\n';
    log->text[log->length] = 0;
}

static void on_log(void *context, const char *line)
{
    struct host_log *log = context;
    append_log(log, line);
    if (!log->quiet) fprintf(log->console, "%s\n", line);
}

static void on_progress(void *context, const char *stage, uint32_t done, uint32_t total)
{
    (void)context;
    (void)stage;
    (void)done;
    (void)total;
}

int main(int argc, char **argv)
{
    const char *image_path = 0;
    const char *pak_path = 0;
    const char *card = 0;
    uint32_t flags = 0;
    uint32_t arena_megabytes = DEFAULT_ARENA_MEGABYTES;
    int quiet = 0;
    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        const char *value = index + 1 < argc ? argv[index + 1] : 0;
        if (!strcmp(argument, "--image") && value) image_path = argv[++index];
        else if (!strcmp(argument, "--pak") && value) pak_path = argv[++index];
        else if (!strcmp(argument, "--card") && value) card = argv[++index];
        else if (!strcmp(argument, "--arena-megabytes") && value) arena_megabytes = (uint32_t)strtoul(argv[++index], 0, 10);
        else if (!strcmp(argument, "--no-profile")) flags |= NKGEN_FLAG_NO_PROFILE;
        else if (!strcmp(argument, "--skip")) flags |= NKGEN_FLAG_SKIP;
        else if (!strcmp(argument, "--quiet")) quiet = 1;
        else {
            fputs(usage, strcmp(argument, "--help") ? stderr : stdout);
            return strcmp(argument, "--help") ? 2 : 0;
        }
    }
    if (!image_path || !pak_path || !arena_megabytes) {
        fputs(usage, stderr);
        return 2;
    }

    struct nkgen_call call;
    memset(&call, 0, sizeof(call));
    struct host_log log = { stdout, 0, 0, 0, quiet };
    call.magic = NKGEN_CALL_MAGIC;
    call.abi_version = NKGEN_ABI_VERSION;
    call.size = sizeof(call);
    call.image = read_whole_file(image_path, &call.image_length);
    call.pak = read_whole_file(pak_path, &call.pak_length);
    call.arena_length = arena_megabytes * 1024u * 1024u;
    call.arena = malloc(call.arena_length);
    call.flags = flags;
    call.progress = on_progress;
    call.log = on_log;
    call.context = &log;
    if (!call.arena) {
        fprintf(stderr, "cannot allocate the arena\n");
        return 1;
    }

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int32_t result = nkgen_generate(&call);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double milliseconds = (double)(end.tv_sec - start.tv_sec) * 1000.0 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;

    for (uint32_t index = 0; index < call.step_count; index++) {
        char line[512];
        snprintf(line, sizeof(line), "step %s=%s%s%s", call.steps[index].name, call.steps[index].result ? "failed" : "ok",
            call.steps[index].detail[0] ? ": " : "", call.steps[index].detail);
        append_log(&log, line);
    }
    if (result != NKGEN_OK) {
        fprintf(stderr, "Error: %s\n", call.message);
        return 1;
    }
    if (card) {
        for (uint32_t index = 0; index < call.output_count; index++) {
            const struct nkgen_output *output = &call.outputs[index];
            size_t path_length = strlen(card) + strlen(output->path) + 2;
            char *path = malloc(path_length);
            snprintf(path, path_length, "%s/%s", card, output->path);
            write_whole_file(path, output->data, output->length);
            free(path);
        }
        size_t path_length = strlen(card) + 40;
        char *path = malloc(path_length);
        snprintf(path, path_length, "%s/pwsh2-boot/nk-%08x/nkgen.log", card, call.identity_crc32);
        write_whole_file(path, log.text, log.length);
        free(path);
    }
    if (!quiet) {
        for (uint32_t index = 0; index < call.output_count; index++) {
            printf("%s: %u bytes, crc32 %08x\n", call.outputs[index].path, call.outputs[index].length, call.outputs[index].crc32);
        }
        printf("%s, identity %08x, %.0f ms\n", call.known ? call.profile : "unknown image", call.identity_crc32, milliseconds);
    }
    free(log.text);
    free(call.arena);
    free((void *)call.image);
    free((void *)call.pak);
    return 0;
}
