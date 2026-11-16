#include "internal.h"

struct profile_plan {
    struct buffer replacements;
    struct buffer registry_entries;
    int registry;
    struct buffer word_patches;
    struct pak_blob a7300_display;
};

static const char *hex_digest(const uint8_t digest[32])
{
    struct buffer text = { 0 };
    buffer_hex(&text, digest, 32);
    buffer_append(&text, "", 1);
    return (const char *)text.data;
}

static struct dll_bytes derive_from_rom(struct rom_image *image, const struct pak *pak, uint32_t blob)
{
    struct rom_spec spec;
    pak_rom_spec(pak, blob, &spec);
    struct dll_bytes dll = extract_rom_dll(image, spec.module, spec.spare_sections);
    for (uint32_t index = 0; index < spec.redirect_count; index++) dll = redirect_imports(dll, &spec.redirects[index]);
    patch_dll_bytes(dll, &spec);
    return dll;
}

static int find_match(const struct pak *pak, const uint8_t *image, uint32_t image_length, int use_profile, uint8_t digest[32], struct pak_image *match)
{
    int length_known = 0;
    for (uint32_t index = 0; index < pak->image_count; index++) {
        if (pak_image(pak, index).length == image_length) length_known = 1;
    }
    if (!length_known) return 0;
    sha256(image, image_length, digest, "Identifying image");
    if (!use_profile) return 1;
    for (uint32_t index = 0; index < pak->image_count; index++) {
        struct pak_image candidate = pak_image(pak, index);
        if (bytes_equal(candidate.sha256, digest, 32)) {
            *match = candidate;
            return 2;
        }
    }
    return 1;
}

static void plan_profile(const struct pak *pak, const struct pak_image *match, struct rom_image *image, struct profile_plan *plan)
{
    struct pak_profile profile = pak_profile(pak, match->profile);
    uint32_t rom_modules = 0;
    for (uint32_t index = 0; index < profile.operation_count; index++) {
        if (pak_operation(pak, profile.first_operation + index).kind == PAK_MODULE_ROM) rom_modules++;
    }
    uint32_t rom_done = 0;
    if (rom_modules) step_begin("rom modules");
    for (uint32_t index = 0; index < profile.operation_count; index++) {
        struct pak_operation operation = pak_operation(pak, profile.first_operation + index);
        struct pak_blob blob = pak_blob(pak, operation.blob);
        if (operation.kind == PAK_MODULE_FILE) {
            struct replacement *replacement = buffer_push(&plan->replacements, sizeof(struct replacement));
            replacement->name = operation.name;
            replacement->contents = blob.data;
            replacement->length = blob.length;
        } else if (operation.kind == PAK_MODULE_ROM) {
            report_progress("Extracting modules", rom_done++, rom_modules);
            struct dll_bytes dll = derive_from_rom(image, pak, operation.blob);
            struct replacement *replacement = buffer_push(&plan->replacements, sizeof(struct replacement));
            replacement->name = operation.name;
            replacement->contents = dll.data;
            replacement->length = dll.length;
            log_line("%s: %u bytes from the ROM", operation.name, dll.length);
        } else if (operation.kind == PAK_REGISTRY) {
            pak_registry(pak, operation.blob, match, &plan->registry_entries);
        } else if (operation.kind == PAK_WORD_PATCH) {
            struct word_patch *patch = buffer_push(&plan->word_patches, sizeof(struct word_patch));
            pak_word_patch(pak, operation.blob, patch);
        } else if (operation.kind == PAK_A7300_DISPLAY) {
            plan->a7300_display = blob;
        }
    }
    if (rom_modules) {
        report_progress("Extracting modules", rom_modules, rom_modules);
        step_end("%u modules", rom_modules);
    }
    plan->registry = 1;
}

static int valid_card_path(const char *path)
{
    static const char prefix[] = "pwsh2-boot/";
    uint32_t length = text_length(path);
    if (length <= sizeof(prefix) - 1 || !bytes_equal(path, prefix, sizeof(prefix) - 1)) return 0;
    for (uint32_t index = 0; index + 1 < length; index++) {
        if (path[index] == '.' && path[index + 1] == '.') return 0;
    }
    return 1;
}

static void add_output(struct buffer *outputs, const char *path, const uint8_t *data, uint32_t length)
{
    struct nkgen_output *output = buffer_push(outputs, sizeof(struct nkgen_output));
    output->path = path;
    output->data = data;
    output->length = length;
    output->crc32 = crc32(data, length);
}

static void add_card_files(struct buffer *outputs, const struct pak *pak, struct rom_image *image, uint32_t first, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        struct pak_operation operation = pak_operation(pak, first + index);
        if (operation.kind != PAK_CARD_FILE && operation.kind != PAK_CARD_FILE_ROM) continue;
        if (!valid_card_path(operation.name)) fail(NKGEN_BAD_PAK, "Pak card path %s is not under pwsh2-boot/", operation.name);
        if (operation.kind == PAK_CARD_FILE) {
            struct pak_blob blob = pak_blob(pak, operation.blob);
            add_output(outputs, operation.name, blob.data, blob.length);
            continue;
        }
        step_begin(operation.name);
        struct dll_bytes dll = derive_from_rom(image, pak, operation.blob);
        add_output(outputs, operation.name, dll.data, dll.length);
        step_end("%u bytes from the ROM", dll.length);
    }
}

static void generate(struct nkgen_call *call)
{
    struct pak pak;
    struct rom_image image;
    report_progress("Identifying image", 0, 1);
    pak_open(&pak, call->pak, call->pak_length);
    log_line("pak %s, crc32 %08x", pak.release, pak.crc32);

    step_begin("identify");
    check(is_rom_image(call->image, call->image_length), "Not a Windows CE image (nk-slot-*.bin)");
    call->identity_crc32 = image_identity(call->image, call->image_length);
    const char *folder = text_format("pwsh2-boot/nk-%08x", call->identity_crc32);
    struct pak_image match = { 0 };
    int use_profile = !(call->flags & NKGEN_FLAG_NO_PROFILE);
    int matched = find_match(&pak, call->image, call->image_length, use_profile, call->image_sha256, &match);
    const uint8_t *digest = matched ? call->image_sha256 : 0;
    call->known = matched == 2;
    int skipping = (call->flags & NKGEN_FLAG_SKIP) != 0;
    int applied = call->known && skipping;
    call->profile = applied ? pak_profile(&pak, match.profile).id : "";
    log_line("image %u bytes, sha256 %s, identity %08x", call->image_length, digest ? hex_digest(digest) : "not computed", call->identity_crc32);
    if (call->known) {
        log_line("%s %s, profile %s", match.device, match.description, call->profile);
        step_end("%s %s, profile %s", match.device, match.description, call->profile);
    } else {
        step_end("unknown image");
    }
    if (call->known && !applied) log_line("note: %s drivers need skipping, so only bstart was added", match.device);

    open_image(&image, call->image, call->image_length);
    struct profile_plan plan = { 0 };
    if (applied) plan_profile(&pak, &match, &image, &plan);

    struct skip_list skip_list;
    pak_skip_list(&pak, &skip_list);
    struct pak_blob bstart = pak_blob(&pak, pak.bstart_blob);
    struct bundle_request request = { 0 };
    request.image = &image;
    request.image_sha256 = digest;
    request.skipping = skipping;
    request.skip_list = &skip_list;
    request.bstart = bstart.data;
    request.bstart_length = bstart.length;
    request.a7300_display = plan.a7300_display.data;
    request.a7300_display_length = plan.a7300_display.length;
    request.replacement_count = plan.replacements.length / sizeof(struct replacement);
    request.replacements = buffer_items(&plan.replacements);
    request.registry = plan.registry;
    request.registry_entry_count = plan.registry_entries.length / sizeof(struct registry_entry);
    request.registry_entries = buffer_items(&plan.registry_entries);
    request.word_patch_count = plan.word_patches.length / sizeof(struct word_patch);
    request.word_patches = buffer_items(&plan.word_patches);
    struct bundle_result bundle;
    make_bundle(&request, &bundle);
    log_line("%u skip spans, %llu bytes", bundle.span_count, (unsigned long long)bundle.skipped_bytes);

    struct buffer outputs = { 0 };
    add_output(&outputs, text_format("%s/boot.nkp", folder), bundle.text.data, bundle.text.length);
    for (uint32_t index = 0; index < bundle.replaced_count; index++) {
        struct replaced_module *module = &bundle.replaced[index];
        add_output(&outputs, text_format("%s/%s", folder, module->card_path), module->contents, module->length);
    }
    if (applied) {
        struct pak_profile profile = pak_profile(&pak, match.profile);
        add_card_files(&outputs, &pak, &image, profile.first_operation, profile.operation_count);
        add_card_files(&outputs, &pak, &image, pak.shared_first_operation, pak.shared_operation_count);
    }
    call->outputs = buffer_items(&outputs);
    call->output_count = outputs.length / sizeof(struct nkgen_output);
}

int32_t nkgen_generate(struct nkgen_call *call)
{
    if (!call) return NKGEN_BAD_CALL;
    if (call->magic != NKGEN_CALL_MAGIC || call->abi_version != NKGEN_ABI_VERSION || call->size < sizeof(*call)
        || !call->image || !call->pak || !call->arena) {
        call->result = NKGEN_BAD_CALL;
        return NKGEN_BAD_CALL;
    }
    call->result = NKGEN_OK;
    call->output_count = 0;
    call->outputs = 0;
    call->step_count = 0;
    call->steps = 0;
    call->known = 0;
    call->profile = "";
    call->identity_crc32 = 0;
    fill_bytes(call->image_sha256, 0, sizeof(call->image_sha256));
    call->message[0] = 0;

    struct generator generator;
    generator_start(&generator, call);
    if (save_failure_point(generator.failure)) {
        struct generator *failed = active_generator();
        step_fail_open(failed->call->result, failed->call->message);
        failed->call->output_count = 0;
        failed->call->outputs = 0;
        failed->call->steps = failed->steps.steps;
        failed->call->step_count = failed->steps.count;
        if (failed->call->log) failed->call->log(failed->call->context, failed->call->message);
        generator_stop();
        return failed->call->result;
    }
    generate(call);
    call->steps = generator.steps.steps;
    call->step_count = generator.steps.count;
    log_line("arena peak %u bytes", generator.arena_peak);
    generator_stop();
    return NKGEN_OK;
}
