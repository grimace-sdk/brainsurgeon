#include <windows.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "board_ioctl.h"

#define ABOUT_LENGTH 2048
#define BOARD_DEVICE L"BRD1:"
#define MODULE_NAME_LENGTH 64
#define WIDE_TEXT(text) L##text
#define WIDE(text) WIDE_TEXT(text)

#ifndef BRAINSURGEON_VERSION
#define BRAINSURGEON_VERSION "unknown"
#endif

static void append(WCHAR *about, const WCHAR *label, const WCHAR *value)
{
    if (wcslen(about) + wcslen(label) + wcslen(value) + 5 >= ABOUT_LENGTH)
        return;
    wcscat(about, label);
    wcscat(about, L": ");
    wcscat(about, value[0] ? value : L"unknown");
    wcscat(about, L"\r\n");
}

static void append_registry(WCHAR *about, const WCHAR *label, const WCHAR *name)
{
    WCHAR value[TEXT_LENGTH];

    registry_read_string(HKEY_LOCAL_MACHINE, BRAIN_KEY, name, value, TEXT_LENGTH);
    append(about, label, value);
}

static const struct {
    const WCHAR *module;
    const WCHAR *device;
} drivers[] = {
    { L"board.dll", BOARD_DEVICE },
    { L"touchpad.dll", L"PAD1:" },
    { L"touchraw.dll", L"TRD1:" },
};

static DWORD board_api(void)
{
    HANDLE board = device_open(BOARD_DEVICE);
    BRD_INFO information = { 0, 0 };
    DWORD returned = 0;

    if (board == INVALID_HANDLE_VALUE)
        return 0;
    if (!DeviceIoControl(board, BRD_IOCTL_GET_INFO, NULL, 0, &information, sizeof(information), &returned, NULL)
        || returned < sizeof(information) || information.Magic != BRD_INFO_MAGIC)
        information.Version = 0;
    CloseHandle(board);
    return information.Version;
}

static void append_module(WCHAR *about, const WCHAR *name, const WCHAR *release)
{
    WCHAR value[TEXT_LENGTH];
    DWORD api;

    wcsncpy(value, release, TEXT_LENGTH - 16);
    value[TEXT_LENGTH - 16] = 0;
    if (!_wcsicmp(name, L"board.dll") && (api = board_api()) != 0)
        wsprintfW(value + wcslen(value), L", API %u", api);
    append(about, name, value);
}

static void local_build(const WCHAR *name, WCHAR *value)
{
    WIN32_FIND_DATAW found;
    WCHAR path[MAX_PATH];
    SYSTEMTIME time;
    HANDLE search;

    wcscpy(value, L"local build");
    wcscpy(path, L"\\Windows\\");
    wcscat(path, name);
    search = FindFirstFileW(path, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    FindClose(search);
    wsprintfW(value + wcslen(value), L", %u bytes", found.nFileSizeLow);
    if (FileTimeToSystemTime(&found.ftLastWriteTime, &time) && time.wYear >= 2000)
        wsprintfW(value + wcslen(value), L", %04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
}

static void append_modules(WCHAR *about)
{
    WCHAR name[MODULE_NAME_LENGTH], value[TEXT_LENGTH];
    DWORD name_length, value_size, type, index;
    HKEY key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BRAIN_MODULES_KEY, 0, 0, &key) == ERROR_SUCCESS) {
        for (index = 0;; index++) {
            name_length = MODULE_NAME_LENGTH;
            value_size = sizeof(value) - sizeof(WCHAR);
            if (RegEnumValueW(key, index, name, &name_length, NULL, &type, (BYTE *)value, &value_size) != ERROR_SUCCESS)
                break;
            if (type != REG_SZ)
                continue;
            value[value_size / sizeof(WCHAR)] = 0;
            append_module(about, name, value);
        }
        RegCloseKey(key);
    }
    for (index = 0; index < sizeof(drivers) / sizeof(drivers[0]); index++) {
        if (registry_read_string(HKEY_LOCAL_MACHINE, BRAIN_MODULES_KEY, drivers[index].module, value, TEXT_LENGTH)
            || !device_present(drivers[index].device))
            continue;
        local_build(drivers[index].module, value);
        append_module(about, drivers[index].module, value);
    }
}

static void fill(HWND page)
{
    static WCHAR about[ABOUT_LENGTH];
    OSVERSIONINFOW version;
    WCHAR value[TEXT_LENGTH];

    about[0] = 0;
    append(about, L"brainsurgeon", WIDE(BRAINSURGEON_VERSION));
    append_registry(about, L"Model", L"Model");
    append_registry(about, L"Profile", L"Profile");
    append_registry(about, L"CE image", L"Image");
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    value[0] = 0;
    if (GetVersionExW(&version))
        wsprintfW(value, L"%u.%02u build %u", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    append(about, L"Windows CE", value);
    value[0] = 0;
    SystemParametersInfoW(SPI_GETOEMINFO, sizeof(value), value, 0);
    value[TEXT_LENGTH - 1] = 0;
    append(about, L"OEM", value);
    wsprintfW(value, L"%u x %u", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    append(about, L"Screen", value);
    append_modules(about);
    SetDlgItemTextW(page, IDC_ABOUT_TEXT, about);
}

BOOL CALLBACK about_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        fill(page);
        return TRUE;
    case WM_SIZE:
        MoveWindow(GetDlgItem(page, IDC_ABOUT_TEXT), 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return TRUE;
    }
    return FALSE;
}
