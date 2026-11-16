#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "boot-status.h"

#define BOOT_STATUS_PATH L"\\Storage Card\\pwsh2-boot\\boot-status.txt"
#define BOOT_KEY L"Software\\Brain\\Boot"
#define STEPS_KEY L"Software\\Brain\\Boot\\Steps"
#define SECTIONS_KEY L"Software\\Brain\\Boot\\Sections"
#define STATUS_LIMIT 8192
#define TEXT_LENGTH 256

struct status_value {
    const char *key;
    const WCHAR *name;
    BOOL number;
};

struct status_keys {
    HKEY boot;
    HKEY steps;
    HKEY sections;
    DWORD applied;
    DWORD skipped;
};

static const struct status_value status_values[] = {
    { "image", L"Image", FALSE },
    { "profile", L"Profile", FALSE },
    { "device", L"Device", FALSE },
    { "image-crc", L"ImageCrc", FALSE },
    { "image-length", L"ImageLength", TRUE },
    { "image-sha256", L"ImageSha256", FALSE },
    { "bundle", L"Bundle", FALSE },
    { "folder", L"Folder", FALSE },
    { "generator", L"Generator", FALSE },
    { "release", L"Release", FALSE },
    { "uboot", L"UBoot", FALSE },
    { "error", L"Error", FALSE },
};

static HKEY create_key(const WCHAR *name)
{
    DWORD disposition;
    HKEY key;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, name, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return NULL;
    return key;
}

static void set_string(HKEY key, const WCHAR *name, const WCHAR *value)
{
    if (key)
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
}

static void set_dword(HKEY key, const WCHAR *name, DWORD value)
{
    if (key)
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void widen(WCHAR *wide, const char *text, size_t length)
{
    size_t index;

    if (length > TEXT_LENGTH - 1)
        length = TEXT_LENGTH - 1;
    for (index = 0; index < length && text[index] != '\r'; index++)
        wide[index] = (unsigned char)text[index];
    wide[index] = 0;
}

static DWORD read_status(char *text, DWORD capacity)
{
    HANDLE file = CreateFileW(BOOT_STATUS_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD read = 0;

    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(file, text, capacity - 1, &read, NULL))
        read = 0;
    CloseHandle(file);
    text[read] = 0;
    return read;
}

static void record_value(struct status_keys *keys, const char *key, size_t key_length, const char *value, size_t value_length)
{
    WCHAR text[TEXT_LENGTH];
    char number[16];
    unsigned int index;

    for (index = 0; index < sizeof(status_values) / sizeof(status_values[0]); index++) {
        if (strlen(status_values[index].key) != key_length || strncmp(status_values[index].key, key, key_length))
            continue;
        if (status_values[index].number) {
            memcpy(number, value, value_length < sizeof(number) - 1 ? value_length : sizeof(number) - 1);
            number[value_length < sizeof(number) - 1 ? value_length : sizeof(number) - 1] = 0;
            set_dword(keys->boot, status_values[index].name, strtoul(number, NULL, 16));
        } else {
            widen(text, value, value_length);
            set_string(keys->boot, status_values[index].name, text);
        }
        return;
    }
}

static void record_line(struct status_keys *keys, const char *line, size_t length)
{
    const char *equals = memchr(line, '=', length);
    const char *value;
    WCHAR name[TEXT_LENGTH], text[TEXT_LENGTH];
    size_t value_length;

    if (!equals)
        return;
    value = equals + 1;
    value_length = length - (size_t)(value - line);
    widen(text, value, value_length);
    if (!strncmp(line, "step ", 5)) {
        widen(name, line + 5, (size_t)(equals - line) - 5);
        set_string(keys->steps, name, text);
    } else if (!strncmp(line, "section ", 8)) {
        widen(name, line + 8, (size_t)(equals - line) - 8);
        set_string(keys->sections, name, text);
        if (value_length >= 7 && !strncmp(value, "applied", 7))
            keys->applied++;
        else
            keys->skipped++;
    } else {
        record_value(keys, line, (size_t)(equals - line), value, value_length);
    }
}

void boot_status_record(void)
{
    static char text[STATUS_LIMIT];
    struct status_keys keys = { NULL, NULL, NULL, 0, 0 };
    const char *line, *end;
    DWORD length;

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, BOOT_KEY);
    keys.boot = create_key(BOOT_KEY);
    if (!keys.boot)
        return;
    length = read_status(text, STATUS_LIMIT);
    if (!length) {
        set_string(keys.boot, L"Bundle", L"unknown");
        RegCloseKey(keys.boot);
        return;
    }
    keys.steps = create_key(STEPS_KEY);
    keys.sections = create_key(SECTIONS_KEY);
    for (line = text; line < text + length; line = end + 1) {
        end = memchr(line, '\n', (size_t)(text + length - line));
        if (!end)
            end = text + length;
        record_line(&keys, line, (size_t)(end - line));
    }
    set_dword(keys.boot, L"SectionsApplied", keys.applied);
    set_dword(keys.boot, L"SectionsSkipped", keys.skipped);
    if (keys.steps)
        RegCloseKey(keys.steps);
    if (keys.sections)
        RegCloseKey(keys.sections);
    RegCloseKey(keys.boot);
}
