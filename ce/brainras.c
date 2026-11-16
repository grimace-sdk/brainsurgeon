#include <windows.h>

#define ERROR_CANNOT_FIND_PHONEBOOK_ENTRY 623
#define LINEERR_OPERATIONUNAVAIL 0x80000048

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static DWORD fail(DWORD code)
{
    SetLastError(code);
    return code;
}

static DWORD report_none(LPDWORD size, LPDWORD count)
{
    if (count)
        *count = 0;
    if (size)
        *size = 0;
    return ERROR_SUCCESS;
}

DWORD WINAPI RasEnumEntries(LPCWSTR reserved, LPCWSTR phonebook, void *entries, LPDWORD size, LPDWORD count)
{
    return report_none(size, count);
}

DWORD WINAPI RasEnumConnections(void *connections, LPDWORD size, LPDWORD count)
{
    return report_none(size, count);
}

DWORD WINAPI RasEnumDevicesW(void *devices, LPDWORD size, LPDWORD count)
{
    return report_none(size, count);
}

DWORD WINAPI RasGetEntryProperties(LPCWSTR phonebook, LPCWSTR entry, void *properties, LPDWORD size, void *device, LPDWORD device_size)
{
    return fail(ERROR_CANNOT_FIND_PHONEBOOK_ENTRY);
}

DWORD WINAPI RasSetEntryProperties(LPCWSTR phonebook, LPCWSTR entry, void *properties, DWORD size, void *device, DWORD device_size)
{
    return fail(ERROR_NOT_SUPPORTED);
}

DWORD WINAPI RasValidateEntryName(LPCWSTR phonebook, LPCWSTR entry)
{
    return ERROR_SUCCESS;
}

DWORD WINAPI RasDeleteEntry(LPCWSTR phonebook, LPCWSTR entry)
{
    return fail(ERROR_CANNOT_FIND_PHONEBOOK_ENTRY);
}

DWORD WINAPI RasRenameEntry(LPCWSTR phonebook, LPCWSTR old_entry, LPCWSTR new_entry)
{
    return fail(ERROR_CANNOT_FIND_PHONEBOOK_ENTRY);
}

DWORD WINAPI RasHangUp(HANDLE connection)
{
    return ERROR_SUCCESS;
}

DWORD WINAPI RasGetConnectStatus(HANDLE connection, void *status)
{
    return fail(ERROR_INVALID_HANDLE);
}

DWORD WINAPI RasDevConfigDialogEditW(LPCWSTR device, LPCWSTR type, HWND owner, void *in, DWORD in_size, void *out)
{
    return fail(ERROR_NOT_SUPPORTED);
}

DWORD WINAPI RasGetEapConnectionData(LPCWSTR entry, BYTE *data, LPDWORD size)
{
    return fail(ERROR_NOT_SUPPORTED);
}

DWORD WINAPI RasSetEapConnectionData(LPCWSTR entry, BYTE *data, DWORD size)
{
    return fail(ERROR_NOT_SUPPORTED);
}

LONG WINAPI lineInitialize(void *line_app, HINSTANCE instance, void *callback, LPCWSTR name, LPDWORD devices)
{
    if (devices)
        *devices = 0;
    return (LONG)LINEERR_OPERATIONUNAVAIL;
}

LONG WINAPI lineShutdown(void *line_app)
{
    return 0;
}

LONG WINAPI lineGetTranslateCaps(void *line_app, DWORD api_version, void *caps)
{
    return (LONG)LINEERR_OPERATIONUNAVAIL;
}

LONG WINAPI lineTranslateDialog(void *line_app, DWORD device, DWORD version, HWND owner, LPCWSTR address)
{
    return (LONG)LINEERR_OPERATIONUNAVAIL;
}
