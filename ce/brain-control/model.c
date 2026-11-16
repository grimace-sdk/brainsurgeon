#include "brain-control.h"

static const struct {
    const WCHAR *name;
    DWORD model;
} models[] = {
    { L"PW-SH2", MODEL_PWSH2 },
    { L"PW-SB1", MODEL_PWSB1 },
    { L"PW-SH4", MODEL_PWSH4 },
    { L"PW-A7300", MODEL_PWA7300 },
    { L"PW-G5300", MODEL_PWA7300 },
};

static DWORD detected = MODEL_UNKNOWN;

DWORD model_detect(void)
{
    WCHAR name[TEXT_LENGTH];
    unsigned int index;

    detected = MODEL_UNKNOWN;
    if (!registry_read_string(HKEY_LOCAL_MACHINE, BRAIN_KEY, L"Model", name, TEXT_LENGTH))
        return detected;
    for (index = 0; index < sizeof(models) / sizeof(models[0]); index++)
        if (!wcscmp(name, models[index].name))
            detected = models[index].model;
    return detected;
}

DWORD model_current(void)
{
    return detected;
}

DWORD registry_read_dword(HKEY root, const WCHAR *key_name, const WCHAR *name, DWORD fallback)
{
    DWORD value, size = sizeof(value), type;
    HKEY key;

    if (RegOpenKeyExW(root, key_name, 0, 0, &key) != ERROR_SUCCESS)
        return fallback;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        value = fallback;
    RegCloseKey(key);
    return value;
}

BOOL registry_write_dwords(HKEY root, const WCHAR *key_name, const WCHAR *const *names, const DWORD *values, unsigned int count)
{
    DWORD disposition;
    BOOL written = TRUE;
    unsigned int index;
    HKEY key;

    if (RegCreateKeyExW(root, key_name, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return FALSE;
    for (index = 0; index < count; index++)
        written &= RegSetValueExW(key, names[index], 0, REG_DWORD, (const BYTE *)&values[index], sizeof(DWORD)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return written;
}

BOOL registry_read_string(HKEY root, const WCHAR *key_name, const WCHAR *name, WCHAR *text, DWORD characters)
{
    DWORD size = characters * sizeof(WCHAR), type;
    BOOL found;
    HKEY key;

    text[0] = 0;
    if (RegOpenKeyExW(root, key_name, 0, 0, &key) != ERROR_SUCCESS)
        return FALSE;
    found = RegQueryValueExW(key, name, NULL, &type, (BYTE *)text, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    text[characters - 1] = 0;
    if (!found)
        text[0] = 0;
    return found && text[0];
}

HANDLE device_open(const WCHAR *name)
{
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

BOOL device_present(const WCHAR *name)
{
    HANDLE device = device_open(name);

    if (device == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(device);
    return TRUE;
}
