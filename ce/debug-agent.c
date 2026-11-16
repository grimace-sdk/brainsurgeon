#include "debug-agent.h"
#include "config.h"

#define DEBUG_AGENT_WINDOW_CLASS L"CeDebugManager"
#define DEBUG_AGENT_STOP_MESSAGE (WM_APP + 2)
#define NETWORK_DRIVER L"\\Windows\\rndisfn.dll"
#define SHARP_SERIAL_DRIVER L"\\Windows\\serialusbfn.dll"
#define TRANSPORT_NETWORK L"rndis"
#define TRANSPORT_SHARP_SERIAL L"usb"
#define TRANSPORT_CDC_SERIAL L"cdc"

struct debug_settings {
    WCHAR at_boot[CONFIG_VALUE_LENGTH];
    WCHAR transport[CONFIG_VALUE_LENGTH];
};

static void read_settings(struct debug_settings *settings)
{
    struct config_entry entries[] = {
        { L"start-at-boot", settings->at_boot },
        { L"transport", settings->transport },
    };

    config_read_settings(DEBUG_AGENT_SETTINGS_FILE, entries, sizeof(entries) / sizeof(entries[0]));
}

static BOOL file_exists(const WCHAR *path)
{
    return GetFileAttributesW(path) != 0xffffffff;
}

BOOL debug_agent_at_boot(void)
{
    struct debug_settings settings;

    read_settings(&settings);
    return !wcscmp(settings.at_boot, L"yes");
}

BOOL debug_agent_save_at_boot(BOOL enabled)
{
    struct debug_settings settings;
    struct config_entry entries[] = {
        { L"start-at-boot", settings.at_boot },
        { L"transport", settings.transport },
    };

    read_settings(&settings);
    wcscpy(settings.at_boot, enabled ? L"yes" : L"no");
    if (!settings.transport[0])
        wcscpy(settings.transport, L"auto");
    return config_write_settings(DEBUG_AGENT_SETTINGS_FILE, entries, sizeof(entries) / sizeof(entries[0]));
}

const WCHAR *debug_agent_transport(void)
{
    struct debug_settings settings;

    read_settings(&settings);
    if (!wcscmp(settings.transport, TRANSPORT_NETWORK))
        return TRANSPORT_NETWORK;
    if (!wcscmp(settings.transport, TRANSPORT_SHARP_SERIAL))
        return TRANSPORT_SHARP_SERIAL;
    if (!wcscmp(settings.transport, TRANSPORT_CDC_SERIAL))
        return TRANSPORT_CDC_SERIAL;
    if (file_exists(NETWORK_DRIVER))
        return TRANSPORT_NETWORK;
    if (file_exists(SHARP_SERIAL_DRIVER))
        return TRANSPORT_SHARP_SERIAL;
    return TRANSPORT_CDC_SERIAL;
}

const WCHAR *debug_agent_transport_description(const WCHAR *transport)
{
    if (!wcscmp(transport, TRANSPORT_NETWORK))
        return L"Network over USB (rndisfn.dll)";
    if (!wcscmp(transport, TRANSPORT_SHARP_SERIAL))
        return L"USB serial (serialusbfn.dll)";
    return L"USB serial (usbserial.dll)";
}

BOOL debug_agent_installed(void)
{
    return file_exists(DEBUG_AGENT_PATH);
}

BOOL debug_agent_running(void)
{
    return FindWindowW(DEBUG_AGENT_WINDOW_CLASS, NULL) != NULL;
}

BOOL debug_agent_start(void)
{
    WCHAR arguments[16];
    PROCESS_INFORMATION process;

    if (!debug_agent_installed())
        return FALSE;
    wcscpy(arguments, debug_agent_transport());
    if (!CreateProcessW(DEBUG_AGENT_PATH, arguments, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process))
        return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

BOOL debug_agent_stop(void)
{
    HWND window = FindWindowW(DEBUG_AGENT_WINDOW_CLASS, NULL);

    return window && PostMessageW(window, DEBUG_AGENT_STOP_MESSAGE, 0, 0);
}
