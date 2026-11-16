#include <windows.h>
#include "config.h"
#include "debug-agent.h"
#include "power-settings.h"
#include "sound.h"

#define BOOT_FOLDER_PATH L"\\Storage Card\\pwsh2-boot"
#define LAUNCH_FILE L"launch.txt"
#define CARD_PREFIX L"\\Storage Card\\"
#define BATTERY_TYPE_PROFILE L"pwa7300"
#define PROGRAMS_FOLDER L"\\Windows\\Programs\\"
#define CARD_PROGRAMS_FOLDER L"\\Storage Card\\pwsh2-boot\\Programs"
#define START_MENU_PROGRAMS L"\\Windows\\Programs"
#define CARD_WINDOWS_FOLDER L"\\Storage Card\\pwsh2-boot\\Windows"
#define WINDOWS_FOLDER L"\\Windows"
#define DISPLAY_SETTINGS_FILE L"display.txt"
#define BACKLIGHT_DEVICE L"BKL1:"
#define IOCTL_BACKLIGHT_LEVEL 0x00222ee4
#define BATTERY_DEVICE L"BAT1:"
#define IOCTL_BATTERY_SET_TYPE 0x00290418
#define BATTERY_TYPE_ALKALINE 0
#define BATTERY_TYPE_NIMH 1
#define RESTART_SHORTCUT L"Restart.lnk"
#define RESTART_SHORTCUT_TARGET "\"\\Windows\\bstart.exe\" restart"
#define IOCTL_HAL_REBOOT 0x0101003C
#define SAVE_REQUEST_EVENT L"pwsh2-boot/SaveRegistry"
#define SAVED_EVENT L"pwsh2-boot/RegistrySaved"
#define REGISTRY_SAVE_TIMEOUT_MS 10000
#define CE_POWER_STATE_RESET 0x00800000
#define CE_POWER_FORCE 0x00001000
#define CARD_WAIT_MILLISECONDS 15000
#define CARD_POLL_MILLISECONDS 100
#define POWER_MANAGER_DEVICE L"PWR1:"
#define IOCTL_POWER_SUSPEND_ENABLE 0x00220018
#define IOCTL_POWER_IDLE_ENABLE 0x00220034
#define HOLDING_CLASS L"BrainBootHolding"
#define HOLDING_COLOUR RGB(58, 110, 165)
#define MESSAGE_COLOUR RGB(255, 255, 255)
#define MESSAGE_FONT_HEIGHT 26
#define MESSAGE_MARGIN 40
#define MESSAGE_LENGTH 640
#define HANDOVER_WAIT_MILLISECONDS 20000
#define AUTOROTATE_PATH L"\\Storage Card\\pwsh2-boot\\autorotate.exe"
#define DISPLAY_READY_EVENT L"pwsh2-boot/DisplayReady"
#define DISPLAY_READY_WAIT_MILLISECONDS 3000
#define STARTUP_READY_EVENT L"pwsh2-boot/StartupReady"
#define STARTUP_READY_WAIT_MILLISECONDS 10000
#define INPUT_PANEL_DEVICE L"SIP0:"
#define DESKTOP_KEY L"ControlPanel\\Desktop"
#define WALLPAPER_VALUE L"wallpaper"
#define DEBUG_KEY L"Debug"
#define JIT_DEBUGGER_VALUE L"JITDebugger"

struct launch_config {
    WCHAR program[CONFIG_VALUE_LENGTH];
    WCHAR arguments[CONFIG_VALUE_LENGTH];
    WCHAR wait_for_window[CONFIG_VALUE_LENGTH];
    WCHAR restart_keyboard[CONFIG_VALUE_LENGTH];
    WCHAR restart_shortcut[CONFIG_VALUE_LENGTH];
    WCHAR wallpaper[CONFIG_VALUE_LENGTH];
    WCHAR crash_handler[CONFIG_VALUE_LENGTH];
    WCHAR startup[CONFIG_VALUE_LENGTH];
    WCHAR path[MAX_PATH];
};

DWORD WINAPI ce_set_system_power_state(LPCWSTR state, DWORD power_state, DWORD options) __asm__("SetSystemPowerState");
BOOL WINAPI KernelIoControl(DWORD code, LPVOID input, DWORD input_size, LPVOID output, DWORD output_size, LPDWORD returned);

BOOL WINAPI DeactivateDevice(HANDLE device);
HANDLE WINAPI ActivateDeviceEx(LPCWSTR key, LPCVOID registry_values, DWORD value_count, LPVOID context);

static BOOL launch(const WCHAR *path, WCHAR *arguments)
{
    PROCESS_INFORMATION process;

    if (!CreateProcessW(path, arguments, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process))
        return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

static WCHAR problem[MESSAGE_LENGTH];

static LRESULT CALLBACK holding_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    LOGFONTW font_description;
    HFONT font, previous_font;
    RECT area;
    HDC context;

    if (message != WM_PAINT || !problem[0])
        return DefWindowProcW(window, message, wparam, lparam);
    context = BeginPaint(window, &paint);
    memset(&font_description, 0, sizeof(font_description));
    font_description.lfHeight = MESSAGE_FONT_HEIGHT;
    font_description.lfWeight = FW_NORMAL;
    font = CreateFontIndirectW(&font_description);
    previous_font = SelectObject(context, font);
    SetBkMode(context, TRANSPARENT);
    SetTextColor(context, MESSAGE_COLOUR);
    GetClientRect(window, &area);
    area.left += MESSAGE_MARGIN;
    area.top += MESSAGE_MARGIN;
    area.right -= MESSAGE_MARGIN;
    area.bottom -= MESSAGE_MARGIN;
    DrawTextW(context, problem, -1, &area, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(context, previous_font);
    DeleteObject(font);
    EndPaint(window, &paint);
    return 0;
}

static void pump_messages(void)
{
    MSG message;

    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static HWND show_holding_window(HINSTANCE instance)
{
    WNDCLASSW window_class;
    HWND window;

    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = holding_window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = CreateSolidBrush(HOLDING_COLOUR);
    window_class.lpszClassName = HOLDING_CLASS;
    RegisterClassW(&window_class);
    window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, HOLDING_CLASS, L"", WS_POPUP | WS_VISIBLE, 0, 0,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, instance, NULL);
    if (window)
        UpdateWindow(window);
    pump_messages();
    return window;
}

static BOOL wait_for_file(const WCHAR *path)
{
    DWORD started = GetTickCount();

    while (GetFileAttributesW(path) == 0xffffffff) {
        if (GetTickCount() - started > CARD_WAIT_MILLISECONDS)
            return FALSE;
        pump_messages();
        Sleep(CARD_POLL_MILLISECONDS);
    }
    return TRUE;
}

static void set_problem(const WCHAR *text, const WCHAR *detail)
{
    wcsncpy(problem, text, MESSAGE_LENGTH - 1);
    if (detail)
        wcsncpy(problem + wcslen(problem), detail, MESSAGE_LENGTH - 1 - wcslen(problem));
}

static void show_problem(HWND holding_window)
{
    MSG message;

    if (!holding_window)
        return;
    InvalidateRect(holding_window, NULL, TRUE);
    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void restart_input_panel(void)
{
    DEVMGR_DEVICE_INFORMATION information;
    HANDLE search;

    memset(&information, 0, sizeof(information));
    information.dwSize = sizeof(information);
    search = FindFirstDevice(DeviceSearchByLegacyName, INPUT_PANEL_DEVICE, &information);
    if (search == INVALID_HANDLE_VALUE)
        return;
    FindClose(search);
    if (DeactivateDevice(information.hDevice))
        ActivateDeviceEx(information.szDeviceKey, NULL, 0, NULL);
}

static BOOL handover_complete(const struct launch_config *config, HWND holding_window)
{
    HWND foreground;

    if (config->wait_for_window[0])
        return FindWindowW(config->wait_for_window, NULL) != NULL;
    foreground = GetForegroundWindow();
    return foreground && foreground != holding_window;
}

static void wait_for_handover(const struct launch_config *config, HWND holding_window)
{
    DWORD started = GetTickCount();

    while (!handover_complete(config, holding_window) && GetTickCount() - started < HANDOVER_WAIT_MILLISECONDS) {
        pump_messages();
        Sleep(CARD_POLL_MILLISECONDS);
    }
}

static BOOL read_config(struct launch_config *config)
{
    struct config_entry entries[] = {
        { L"program", config->program },
        { L"arguments", config->arguments },
        { L"wait-for-window", config->wait_for_window },
        { L"restart-keyboard", config->restart_keyboard },
        { L"restart-shortcut", config->restart_shortcut },
        { L"wallpaper", config->wallpaper },
        { L"crash-handler", config->crash_handler },
        { L"startup", config->startup },
    };

    config_settings_path(config->path, LAUNCH_FILE);
    return config_read(config->path, entries, sizeof(entries) / sizeof(entries[0])) && config->program[0];
}

static void start_autorotate(void)
{
    HANDLE ready;

    if (GetFileAttributesW(AUTOROTATE_PATH) == 0xffffffff)
        return;
    ready = CreateEventW(NULL, TRUE, FALSE, DISPLAY_READY_EVENT);
    if (launch(AUTOROTATE_PATH, NULL) && ready)
        WaitForSingleObject(ready, DISPLAY_READY_WAIT_MILLISECONDS);
    if (ready)
        CloseHandle(ready);
}

static void run_startup_program(const WCHAR *path)
{
    HANDLE ready;

    if (!path[0] || GetFileAttributesW(path) == 0xffffffff)
        return;
    ready = CreateEventW(NULL, TRUE, FALSE, STARTUP_READY_EVENT);
    if (launch(path, NULL) && ready)
        WaitForSingleObject(ready, STARTUP_READY_WAIT_MILLISECONDS);
    if (ready)
        CloseHandle(ready);
}

static void signal_named_event(const WCHAR *name)
{
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, name);

    if (!event)
        return;
    SetEvent(event);
    CloseHandle(event);
}

static const WCHAR *const backlight_events[] = {
    L"Display_Event_Backlight_SubActive",
    L"Display_Event_Backlight_SubUpdate",
    L"Display_Event_Backlight_Update",
};

static void enable_power_management(void)
{
    HANDLE power_manager = CreateFileW(POWER_MANAGER_DEVICE, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD returned;

    if (power_manager == INVALID_HANDLE_VALUE)
        return;
    DeviceIoControl(power_manager, IOCTL_POWER_SUSPEND_ENABLE, NULL, 0, NULL, 0, &returned, NULL);
    DeviceIoControl(power_manager, IOCTL_POWER_IDLE_ENABLE, NULL, 0, NULL, 0, &returned, NULL);
    CloseHandle(power_manager);
}

static void signal_backlight_events(void)
{
    unsigned int index;

    for (index = 0; index < sizeof(backlight_events) / sizeof(backlight_events[0]); index++)
        signal_named_event(backlight_events[index]);
}

static void write_shortcut(const WCHAR *name, const char *target)
{
    WCHAR path[MAX_PATH];
    char contents[MAX_PATH];
    DWORD written;
    HANDLE file;
    int length = (int)strlen(target);

    wcscpy(path, PROGRAMS_FOLDER);
    wcscat(path, name);
    _itoa(length, contents, 10);
    strcat(contents, "#");
    strcat(contents, target);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, contents, (DWORD)strlen(contents), &written, NULL);
    CloseHandle(file);
}

static void create_restart_shortcut(void)
{
    write_shortcut(RESTART_SHORTCUT, RESTART_SHORTCUT_TARGET);
}

static BOOL join_path(WCHAR *path, const WCHAR *folder, const WCHAR *name)
{
    if (wcslen(folder) + wcslen(name) + 2 > MAX_PATH)
        return FALSE;
    wcscpy(path, folder);
    wcscat(path, L"\\");
    wcscat(path, name);
    return TRUE;
}

static void copy_tree(const WCHAR *source, const WCHAR *destination)
{
    WCHAR pattern[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;

    if (!join_path(pattern, source, L"*"))
        return;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!join_path(from, source, found.cFileName) || !join_path(to, destination, found.cFileName))
            continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(to, NULL);
            copy_tree(from, to);
        } else {
            CopyFileW(from, to, FALSE);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static const WCHAR *const retired_card_files[] = {
    L"\\Storage Card\\pwsh2-boot\\rotate.cpl",
    L"\\Storage Card\\pwsh2-boot\\Windows\\brightness.cpl",
    L"\\Storage Card\\pwsh2-boot\\Windows\\nightlight.cpl",
    L"\\Storage Card\\pwsh2-boot\\Windows\\touchpad.cpl",
};

static void remove_retired_card_files(void)
{
    unsigned int index;

    for (index = 0; index < sizeof(retired_card_files) / sizeof(retired_card_files[0]); index++)
        DeleteFileW(retired_card_files[index]);
}

static void remove_superseded_card_files(const WCHAR *profile_folder)
{
    WCHAR pattern[MAX_PATH], shared[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;

    if (!join_path(pattern, profile_folder, L"*"))
        return;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && join_path(shared, CARD_WINDOWS_FOLDER, found.cFileName))
            DeleteFileW(shared);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static void copy_windows_folders(void)
{
    WCHAR profile_folder[MAX_PATH];
    BOOL has_profile = config_profile_path(profile_folder, L"Windows");

    if (has_profile)
        remove_superseded_card_files(profile_folder);
    copy_tree(CARD_WINDOWS_FOLDER, WINDOWS_FOLDER);
    if (has_profile)
        copy_tree(profile_folder, WINDOWS_FOLDER);
}

static const WCHAR *card_relative(const WCHAR *path)
{
    return wcsncmp(path, CARD_PREFIX, wcslen(CARD_PREFIX)) ? path : path + wcslen(CARD_PREFIX);
}

static void set_missing_launch_problem(void)
{
    WCHAR profile_launch[MAX_PATH];

    if (config_profile_path(profile_launch, LAUNCH_FILE)) {
        set_problem(L"No program to start.\n\nAdd ", card_relative(profile_launch));
        wcsncat(problem, L" or pwsh2-boot\\launch.txt to the card, naming the program to start.\n\n"
            L"To use Sharp's dictionary instead, start the Brain without the card.", MESSAGE_LENGTH - 1 - wcslen(problem));
        return;
    }
    set_problem(L"No program to start.\n\nAdd pwsh2-boot\\launch.txt to the card, naming the program to start.\n\n"
        L"To use Sharp's dictionary instead, start the Brain without the card.", NULL);
}

static void set_launch_problem(const WCHAR *text, const struct launch_config *config)
{
    set_problem(text, card_relative(config->path));
    wcsncat(problem, L":\n\n", MESSAGE_LENGTH - 1 - wcslen(problem));
    wcsncat(problem, config->program, MESSAGE_LENGTH - 1 - wcslen(problem));
}

static void set_crash_handler(const WCHAR *path)
{
    HKEY key;
    DWORD disposition;

    if (!path[0] || GetFileAttributesW(path) == 0xffffffff)
        return;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, DEBUG_KEY, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, JIT_DEBUGGER_VALUE, 0, REG_SZ, (const BYTE *)path, (DWORD)((wcslen(path) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
}

static void restore_brightness(void)
{
    WCHAR brightness[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = { { L"brightness", brightness } };
    DWORD level, previous, returned;
    HANDLE backlight;

    if (!config_read_settings(DISPLAY_SETTINGS_FILE, entries, 1) || !brightness[0])
        return;
    level = (DWORD)_wtol(brightness);
    if (!level)
        return;
    backlight = CreateFileW(BACKLIGHT_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (backlight == INVALID_HANDLE_VALUE)
        return;
    DeviceIoControl(backlight, IOCTL_BACKLIGHT_LEVEL, &level, sizeof(level), &previous, sizeof(previous), &returned, NULL);
    CloseHandle(backlight);
}

static void restore_battery_type(void)
{
    WCHAR battery[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = { { L"battery", battery } };
    WCHAR profile[CONFIG_PROFILE_LENGTH];
    DWORD type, returned;
    HANDLE device;

    if (!config_profile(profile) || wcscmp(profile, BATTERY_TYPE_PROFILE) || !config_read_settings(POWER_SETTINGS_FILE, entries, 1))
        return;
    if (!wcscmp(battery, L"alkaline"))
        type = BATTERY_TYPE_ALKALINE;
    else if (!wcscmp(battery, L"nimh"))
        type = BATTERY_TYPE_NIMH;
    else
        return;
    device = CreateFileW(BATTERY_DEVICE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE)
        return;
    DeviceIoControl(device, IOCTL_BATTERY_SET_TYPE, &type, sizeof(type), NULL, 0, &returned, NULL);
    CloseHandle(device);
}

static void restore_power_timeouts(void)
{
    struct power_timeouts timeouts;

    power_timeouts_read(&timeouts);
    power_timeouts_apply(&timeouts);
}

static void set_desktop(const WCHAR *wallpaper)
{
    int element = COLOR_BACKGROUND;
    COLORREF colour = HOLDING_COLOUR;
    HKEY key;

    if (!wcscmp(wallpaper, L"stock"))
        return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, DESKTOP_KEY, 0, 0, &key) == ERROR_SUCCESS) {
        RegSetValueExW(key, WALLPAPER_VALUE, 0, REG_SZ, (const BYTE *)wallpaper, (DWORD)((wcslen(wallpaper) + 1) * sizeof(WCHAR)));
        RegCloseKey(key);
    }
    if (!wallpaper[0])
        SetSysColors(1, &element, &colour);
    PostMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETDESKWALLPAPER, 0);
}

static void save_registry(void)
{
    HANDLE save_request = OpenEventW(EVENT_ALL_ACCESS, FALSE, SAVE_REQUEST_EVENT);
    HANDLE saved = OpenEventW(EVENT_ALL_ACCESS, FALSE, SAVED_EVENT);

    if (save_request && saved) {
        ResetEvent(saved);
        SetEvent(save_request);
        WaitForSingleObject(saved, REGISTRY_SAVE_TIMEOUT_MS);
    }
    if (save_request)
        CloseHandle(save_request);
    if (saved)
        CloseHandle(saved);
}

static void restart(void)
{
    save_registry();
    if (ce_set_system_power_state(NULL, CE_POWER_STATE_RESET, CE_POWER_FORCE) != ERROR_SUCCESS)
        KernelIoControl(IOCTL_HAL_REBOOT, NULL, 0, NULL, 0, NULL);
}

static BOOL starts_with(const WCHAR *text, const WCHAR *prefix)
{
    return text && !wcsncmp(text, prefix, wcslen(prefix));
}

static DWORD launch_sequence(const WCHAR *command_line)
{
    DWORD sequence = 0;

    while (command_line && *command_line >= L'0' && *command_line <= L'9')
        sequence = sequence * 10 + (DWORD)(*command_line++ - L'0');
    return sequence;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    static struct launch_config config;
    DWORD sequence = launch_sequence(command_line);
    HWND holding_window;

    if (starts_with(command_line, L"restart")) {
        if (MessageBoxW(NULL, L"Restart the Brain?", L"Restart", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
            restart();
        return 0;
    }
    if (sequence)
        SignalStarted(sequence);
    enable_power_management();
    signal_backlight_events();
    holding_window = show_holding_window(instance);
    if (wait_for_file(BOOT_FOLDER_PATH)) {
        restore_brightness();
        restore_battery_type();
    }
    restore_power_timeouts();
    if (!wait_for_file(BOOT_FOLDER_PATH) || !read_config(&config)) {
        set_missing_launch_problem();
    } else if (!wait_for_file(config.program)) {
        set_launch_problem(L"Cannot find the program named in ", &config);
    } else {
        run_startup_program(config.startup);
        remove_retired_card_files();
        copy_windows_folders();
        sound_restore();
        set_crash_handler(config.crash_handler);
        set_desktop(config.wallpaper);
        start_autorotate();
        if (wcscmp(config.restart_shortcut, L"no"))
            create_restart_shortcut();
        copy_tree(CARD_PROGRAMS_FOLDER, START_MENU_PROGRAMS);
        if (wcscmp(config.restart_keyboard, L"no"))
            restart_input_panel();
        if (launch(config.program, config.arguments[0] ? config.arguments : NULL)) {
            wait_for_handover(&config, holding_window);
            if (holding_window)
                DestroyWindow(holding_window);
            if (debug_agent_at_boot())
                debug_agent_start();
            sound_watch();
            return 0;
        }
        set_launch_problem(L"Cannot start the program named in ", &config);
    }
    show_problem(holding_window);
    return 0;
}
