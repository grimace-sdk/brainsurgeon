#include <stddef.h>
#include <stdint.h>

#include "../nkgen.h"

#define GENERATOR_ADDRESS 0x42000000u
#define PAK_ADDRESS 0x42100000u
#define ARENA_ADDRESS 0x42800000u
#define ARENA_LENGTH 0x01800000u
#define PARAMETERS_ADDRESS 0x47fffff0u
#define IMAGE_ADDRESS 0x48000000u
#define SYS_OPEN 0x01
#define SYS_CLOSE 0x02
#define SYS_WRITE0 0x04
#define SYS_WRITE 0x05
#define OPEN_WRITE_BINARY 5

int semihosting_call(int operation, void *parameter);
void semihosting_exit(int status) __attribute__((noreturn));
int harness_main(void);
void harness_fault(void);

struct parameters {
    uint32_t image_length;
    uint32_t pak_length;
    uint32_t flags;
    uint32_t reserved;
};

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    return length;
}

static void print(const char *text)
{
    semihosting_call(SYS_WRITE0, (void *)text);
}

static void print_line(const char *text)
{
    print(text);
    print("\n");
}

static void on_log(void *context, const char *line)
{
    (void)context;
    print_line(line);
}

static void write_file(const char *name, const void *data, uint32_t length)
{
    uint32_t open_block[3] = { (uint32_t)name, OPEN_WRITE_BINARY, text_length(name) };
    int handle = semihosting_call(SYS_OPEN, open_block);
    if (handle < 0) {
        print("cannot open ");
        print_line(name);
        semihosting_exit(1);
    }
    uint32_t write_block[3] = { (uint32_t)handle, (uint32_t)data, length };
    semihosting_call(SYS_WRITE, write_block);
    uint32_t close_block[1] = { (uint32_t)handle };
    semihosting_call(SYS_CLOSE, close_block);
}

void harness_fault(void)
{
    print_line("fault: an exception was taken (unaligned access or bad address)");
}

int harness_main(void)
{
    const struct nkgen_header *header = (const struct nkgen_header *)GENERATOR_ADDRESS;
    const struct parameters *parameters = (const struct parameters *)PARAMETERS_ADDRESS;
    uint8_t *bss = (uint8_t *)(GENERATOR_ADDRESS + header->file_length);
    for (uint32_t index = 0; index < header->bss_length; index++) bss[index] = 0;
    int32_t (*generate)(struct nkgen_call *) = (int32_t (*)(struct nkgen_call *))(GENERATOR_ADDRESS + header->entry_offset);

    static struct nkgen_call call;
    call.magic = NKGEN_CALL_MAGIC;
    call.abi_version = NKGEN_ABI_VERSION;
    call.size = sizeof(call);
    call.image = (const uint8_t *)IMAGE_ADDRESS;
    call.image_length = parameters->image_length;
    call.pak = (const uint8_t *)PAK_ADDRESS;
    call.pak_length = parameters->pak_length;
    call.arena = (uint8_t *)ARENA_ADDRESS;
    call.arena_length = ARENA_LENGTH;
    call.flags = parameters->flags;
    call.log = on_log;
    int32_t result = generate(&call);
    if (result != NKGEN_OK) {
        print("Error: ");
        print_line(call.message);
        return 1;
    }
    static char manifest[16384];
    uint32_t manifest_length = 0;
    for (uint32_t index = 0; index < call.output_count; index++) {
        static char name[32];
        uint32_t number = index;
        name[0] = 'o';
        name[1] = 'u';
        name[2] = 't';
        name[3] = (char)('0' + number / 10);
        name[4] = (char)('0' + number % 10);
        name[5] = 0;
        write_file(name, call.outputs[index].data, call.outputs[index].length);
        const char *path = call.outputs[index].path;
        for (uint32_t character = 0; character < 6 && manifest_length < sizeof(manifest) - 1; character++) {
            manifest[manifest_length++] = character < 5 ? name[character] : ' ';
        }
        for (uint32_t character = 0; path[character] && manifest_length < sizeof(manifest) - 2; character++) manifest[manifest_length++] = path[character];
        manifest[manifest_length++] = '\n';
    }
    write_file("manifest.txt", manifest, manifest_length);
    return 0;
}
