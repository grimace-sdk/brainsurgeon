#include "config.h"

#define CONFIG_BYTES 1024
#define BRAIN_KEY L"Software\\Brain"

static void copy_value(WCHAR *destination, const WCHAR *start, const WCHAR *end)
{
    int length = (int)(end - start);

    while (length > 0 && (*start == L' ' || *start == L'\t')) {
        start++;
        length--;
    }
    while (length > 0 && (start[length - 1] == L' ' || start[length - 1] == L'\t' || start[length - 1] == L'\r'))
        length--;
    if (length >= CONFIG_VALUE_LENGTH)
        length = CONFIG_VALUE_LENGTH - 1;
    memcpy(destination, start, length * sizeof(WCHAR));
    destination[length] = 0;
}

static BOOL key_matches(const WCHAR *line, const WCHAR *equals, const WCHAR *key)
{
    int length = (int)(equals - line);

    return length == (int)wcslen(key) && !wcsncmp(line, key, length);
}

static void parse(const WCHAR *text, const struct config_entry *entries, unsigned int count)
{
    const WCHAR *line = text;
    unsigned int index;

    while (*line) {
        const WCHAR *end = wcschr(line, L'\n');
        const WCHAR *equals;

        if (!end)
            end = line + wcslen(line);
        equals = wcschr(line, L'=');
        if (equals && equals < end)
            for (index = 0; index < count; index++)
                if (key_matches(line, equals, entries[index].key))
                    copy_value(entries[index].value, equals + 1, end);
        line = *end ? end + 1 : end;
    }
}

BOOL config_read(const WCHAR *path, const struct config_entry *entries, unsigned int count)
{
    char bytes[CONFIG_BYTES];
    WCHAR text[CONFIG_BYTES + 1];
    const char *start = bytes;
    DWORD length = 0;
    int characters;
    unsigned int index;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    for (index = 0; index < count; index++)
        entries[index].value[0] = 0;
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    ReadFile(file, bytes, sizeof(bytes), &length, NULL);
    CloseHandle(file);
    if (length >= 3 && (unsigned char)bytes[0] == 0xef && (unsigned char)bytes[1] == 0xbb && (unsigned char)bytes[2] == 0xbf) {
        start += 3;
        length -= 3;
    }
    characters = MultiByteToWideChar(CP_UTF8, 0, start, (int)length, text, CONFIG_BYTES);
    text[characters > 0 ? characters : 0] = 0;
    parse(text, entries, count);
    return TRUE;
}

BOOL config_write(const WCHAR *path, const struct config_entry *entries, unsigned int count)
{
    WCHAR text[CONFIG_BYTES];
    char bytes[CONFIG_BYTES * 3];
    DWORD written;
    int length;
    unsigned int index;
    HANDLE file;

    text[0] = 0;
    for (index = 0; index < count; index++) {
        if (wcslen(text) + wcslen(entries[index].key) + wcslen(entries[index].value) + 4 >= CONFIG_BYTES)
            return FALSE;
        wcscat(text, entries[index].key);
        wcscat(text, L"=");
        wcscat(text, entries[index].value);
        wcscat(text, L"\r\n");
    }
    length = WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes, sizeof(bytes), NULL, NULL);
    if (length <= 0)
        return FALSE;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(file, bytes, (DWORD)(length - 1), &written, NULL);
    CloseHandle(file);
    return written == (DWORD)(length - 1);
}

static BOOL read_brain_value(const WCHAR *name, WCHAR *text, DWORD characters)
{
    DWORD size = characters * sizeof(WCHAR), type;
    BOOL found;
    HKEY key;

    text[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BRAIN_KEY, 0, 0, &key) != ERROR_SUCCESS)
        return FALSE;
    found = RegQueryValueExW(key, name, NULL, &type, (BYTE *)text, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    text[characters - 1] = 0;
    if (!found)
        text[0] = 0;
    return text[0] != 0;
}

static BOOL profile_from_text(WCHAR *profile, const WCHAR *text)
{
    unsigned int length = 0;

    for (; *text && length < CONFIG_PROFILE_LENGTH - 1; text++) {
        if (*text >= L'A' && *text <= L'Z')
            profile[length++] = *text - L'A' + L'a';
        else if ((*text >= L'a' && *text <= L'z') || (*text >= L'0' && *text <= L'9'))
            profile[length++] = *text;
    }
    profile[length] = 0;
    return length > 0;
}

BOOL config_profile(WCHAR *profile)
{
    WCHAR text[CONFIG_PROFILE_LENGTH];

    if (read_brain_value(L"Profile", text, CONFIG_PROFILE_LENGTH) && profile_from_text(profile, text))
        return TRUE;
    if (read_brain_value(L"Model", text, CONFIG_PROFILE_LENGTH) && profile_from_text(profile, text))
        return TRUE;
    profile[0] = 0;
    return FALSE;
}

BOOL config_profile_path(WCHAR *path, const WCHAR *name)
{
    WCHAR profile[CONFIG_PROFILE_LENGTH];

    if (!config_profile(profile) || wcslen(CONFIG_FOLDER) + wcslen(profile) + wcslen(name) + 3 > MAX_PATH)
        return FALSE;
    wcscpy(path, CONFIG_FOLDER L"\\");
    wcscat(path, profile);
    if (name[0]) {
        wcscat(path, L"\\");
        wcscat(path, name);
    }
    return TRUE;
}

void config_settings_path(WCHAR *path, const WCHAR *name)
{
    if (config_profile_path(path, name) && GetFileAttributesW(path) != 0xffffffff)
        return;
    wcscpy(path, CONFIG_FOLDER L"\\");
    wcscat(path, name);
}

BOOL config_read_settings(const WCHAR *name, const struct config_entry *entries, unsigned int count)
{
    WCHAR path[MAX_PATH];

    config_settings_path(path, name);
    return config_read(path, entries, count);
}

BOOL config_write_settings(const WCHAR *name, const struct config_entry *entries, unsigned int count)
{
    WCHAR folder[MAX_PATH], path[MAX_PATH];

    if (config_profile_path(folder, L"") && config_profile_path(path, name)) {
        CreateDirectoryW(folder, NULL);
        return config_write(path, entries, count);
    }
    wcscpy(path, CONFIG_FOLDER L"\\");
    wcscat(path, name);
    return config_write(path, entries, count);
}
