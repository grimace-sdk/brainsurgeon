#include <windows.h>
#include "display.h"

#define LOG_MARKER L"\\Storage Card\\pwsh2-boot\\log"
#define LOG_PATH L"\\Storage Card\\pwsh2-boot\\autorotate.log"
#define READY_EVENT L"pwsh2-boot/DisplayReady"
#define STYLE_CHANGE_MESSAGE L"DicStyleChange"
#define STYLE_NORMAL 0
#define STYLE_TABLET 1
#define LOG_LINE_LENGTH 96
#define REFLOW_TIMER 1
#define REFLOW_DELAY_MILLISECONDS 250
#define REFLOW_PASSES 4

static UINT style_change_message;
static UINT apply_message;
static WPARAM current_style = STYLE_NORMAL;
static DWORD normal_rotation;
static HWND autorotate_window;
static struct display_layout layout_before_rotation;
static unsigned int reflow_passes_left;

static void append_text(const char *text)
{
    DWORD written;
    HANDLE file;

    if (GetFileAttributesW(LOG_MARKER) == 0xffffffff)
        return;
    file = CreateFileW(LOG_PATH, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    SetFilePointer(file, 0, NULL, FILE_END);
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
    WriteFile(file, "\r\n", 2, &written, NULL);
    CloseHandle(file);
}

static void append_log(const char *event, WPARAM style, DWORD rotation, DWORD milliseconds)
{
    char line[LOG_LINE_LENGTH];
    char number[12];
    SYSTEMTIME time;

    GetLocalTime(&time);
    _itoa(time.wHour * 10000 + time.wMinute * 100 + time.wSecond, number, 10);
    strcpy(line, number);
    strcat(line, " ");
    strcat(line, event);
    strcat(line, " style=");
    _itoa((int)style, number, 10);
    strcat(line, number);
    strcat(line, " rotation=");
    _itoa((int)rotation, number, 10);
    strcat(line, number);
    strcat(line, " took=");
    _itoa((int)milliseconds, number, 10);
    strcat(line, number);
    strcat(line, "ms");
    append_text(line);
}

static void apply_rotation(const char *event)
{
    struct display_settings settings;
    DWORD rotation, started;

    display_settings_read(&settings);
    rotation = settings.auto_rotate && current_style == STYLE_TABLET ? settings.tablet_rotation : normal_rotation;
    started = GetTickCount();
    display_layout_capture(&layout_before_rotation);
    if (display_rotate(rotation) && autorotate_window) {
        reflow_passes_left = REFLOW_PASSES;
        SetTimer(autorotate_window, REFLOW_TIMER, REFLOW_DELAY_MILLISECONDS, NULL);
    }
    append_log(event, current_style, rotation, GetTickCount() - started);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_TIMER && wparam == REFLOW_TIMER) {
        display_reflow(&layout_before_rotation);
        if (--reflow_passes_left == 0)
            KillTimer(window, REFLOW_TIMER);
        return 0;
    }
    if (message == style_change_message && style_change_message) {
        current_style = wparam == STYLE_TABLET ? STYLE_TABLET : STYLE_NORMAL;
        apply_rotation("fold");
        return 0;
    }
    if (message == apply_message && apply_message) {
        normal_rotation = (DWORD)wparam;
        apply_rotation("settings");
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void signal_ready(void)
{
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, READY_EVENT);

    if (!ready)
        return;
    SetEvent(ready);
    CloseHandle(ready);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    WNDCLASSW window_class;
    struct display_settings settings;
    MSG message;

    HWND existing = FindWindowW(AUTOROTATE_WINDOW_CLASS, NULL);

    if (existing && wcscmp(command_line, L"replace") == 0) {
        SendMessageW(existing, WM_CLOSE, 0, 0);
        existing = NULL;
    }
    if (existing) {
        signal_ready();
        return 0;
    }
    style_change_message = RegisterWindowMessageW(STYLE_CHANGE_MESSAGE);
    apply_message = RegisterWindowMessageW(AUTOROTATE_APPLY_MESSAGE);
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = AUTOROTATE_WINDOW_CLASS;
    RegisterClassW(&window_class);
    display_set_log(append_text);
    autorotate_window = CreateWindowExW(0, AUTOROTATE_WINDOW_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    display_settings_read(&settings);
    normal_rotation = settings.startup_rotation;
    display_set_brightness(settings.brightness);
    apply_rotation("start");
    signal_ready();
    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
