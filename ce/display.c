#include "display.h"
#include "config.h"
#include <commctrl.h>

#define CE_DEVMODE_SIZE 0xc0
#define CE_DEVMODE_SIZE_OFFSET 0x44
#define CE_DEVMODE_FIELDS_OFFSET 0x48
#define CE_DEVMODE_ORIENTATION_OFFSET 0xbc
#define CE_DM_DISPLAYORIENTATION 0x00800000
#define CE_CDS_TEST 0x00000002
#define CE_CDS_RESET 0x40000000
#define DEFAULT_TABLET_ROTATION 90
#define PANEL_DEVICE L"VMC1:"
#define IOCTL_PANEL_RELEASE 0x8000223b
#define BACKLIGHT_DEVICE L"BKL1:"
#define IOCTL_BACKLIGHT_LEVEL 0x00222ee4
#define BACKLIGHT_QUERY 0xffffffff
#define RESPONSE_TIMEOUT_MILLISECONDS 200

LONG WINAPI ce_change_display_settings(LPCWSTR device, void *mode, HWND window, DWORD flags, void *parameter) __asm__("ChangeDisplaySettingsEx");
LRESULT WINAPI ce_send_message_timeout(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT flags, UINT timeout, DWORD *result) __asm__("SendMessageTimeout");

static const DWORD orientations[][2] = {
    { 0, 0 },
    { 90, 1 },
    { 180, 2 },
    { 270, 4 },
};

const DWORD display_brightness_levels[DISPLAY_BRIGHTNESS_LEVELS] = { 1, 25, 50, 75, 100 };

static DWORD brightness_from_text(const WCHAR *text)
{
    DWORD value = (DWORD)_wtoi(text);

    return value >= DISPLAY_BRIGHTNESS_MINIMUM && value <= DISPLAY_BRIGHTNESS_MAXIMUM ? value : 0;
}

static DWORD degrees_from_text(const WCHAR *text, DWORD fallback)
{
    unsigned int index;
    DWORD value = 0;

    if (!*text)
        return fallback;
    while (*text >= L'0' && *text <= L'9')
        value = value * 10 + (DWORD)(*text++ - L'0');
    for (index = 0; index < sizeof(orientations) / sizeof(orientations[0]); index++)
        if (orientations[index][0] == value)
            return value;
    return fallback;
}

static void degrees_to_text(DWORD degrees, WCHAR *text)
{
    _itow((int)degrees, text, 10);
}

void display_settings_read(struct display_settings *settings)
{
    WCHAR startup_rotation[CONFIG_VALUE_LENGTH], auto_rotate[CONFIG_VALUE_LENGTH], tablet_rotation[CONFIG_VALUE_LENGTH];
    WCHAR brightness[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = {
        { L"startup-rotation", startup_rotation },
        { L"auto-rotate", auto_rotate },
        { L"tablet-rotation", tablet_rotation },
        { L"brightness", brightness },
    };

    config_read_settings(DISPLAY_SETTINGS_FILE, entries, sizeof(entries) / sizeof(entries[0]));
    settings->startup_rotation = degrees_from_text(startup_rotation, 0);
    settings->auto_rotate = !wcscmp(auto_rotate, L"yes");
    settings->tablet_rotation = degrees_from_text(tablet_rotation, DEFAULT_TABLET_ROTATION);
    settings->brightness = brightness_from_text(brightness);
}

BOOL display_settings_write(const struct display_settings *settings)
{
    WCHAR startup_rotation[CONFIG_VALUE_LENGTH], auto_rotate[CONFIG_VALUE_LENGTH], tablet_rotation[CONFIG_VALUE_LENGTH];
    WCHAR brightness[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = {
        { L"startup-rotation", startup_rotation },
        { L"auto-rotate", auto_rotate },
        { L"tablet-rotation", tablet_rotation },
        { L"brightness", brightness },
    };

    degrees_to_text(settings->startup_rotation, startup_rotation);
    wcscpy(auto_rotate, settings->auto_rotate ? L"yes" : L"no");
    degrees_to_text(settings->tablet_rotation, tablet_rotation);
    brightness[0] = 0;
    if (settings->brightness)
        _itow((int)settings->brightness, brightness, 10);
    return config_write_settings(DISPLAY_SETTINGS_FILE, entries, sizeof(entries) / sizeof(entries[0]));
}

void display_layout_capture(struct display_layout *layout)
{
    layout->screen.left = 0;
    layout->screen.top = 0;
    layout->screen.right = GetSystemMetrics(SM_CXSCREEN);
    layout->screen.bottom = GetSystemMetrics(SM_CYSCREEN);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &layout->work_area, 0);
}

static BOOL same_rectangle(const RECT *first, const RECT *second)
{
    return first->left == second->left && first->top == second->top && first->right == second->right && first->bottom == second->bottom;
}

static void move_to(HWND window, const RECT *area)
{
    MoveWindow(window, area->left, area->top, area->right - area->left, area->bottom - area->top, TRUE);
}

#define LOG_LINE_LENGTH 160

static display_log_function log_line;

void display_set_log(display_log_function log)
{
    log_line = log;
}

static void append_number(char *line, int value, const char *separator)
{
    char number[12];

    _itoa(value, number, 10);
    strcat(line, number);
    strcat(line, separator);
}

static void log_window(HWND window, const RECT *rectangle, const char *action)
{
    WCHAR class_name[64];
    char line[LOG_LINE_LENGTH];
    int length;

    if (!log_line)
        return;
    if (!GetClassNameW(window, class_name, sizeof(class_name) / sizeof(class_name[0])))
        class_name[0] = 0;
    strcpy(line, "  window ");
    length = WideCharToMultiByte(CP_UTF8, 0, class_name, -1, line + strlen(line), 64, NULL, NULL);
    if (length <= 0)
        strcat(line, "?");
    strcat(line, " ");
    append_number(line, rectangle->left, ",");
    append_number(line, rectangle->top, " ");
    append_number(line, rectangle->right, ",");
    append_number(line, rectangle->bottom, " ");
    strcat(line, action);
    log_line(line);
}

#define ADORNMENT_SPACER_COMMAND 0xccdeadcc
#define ADORNMENTS_SCANNED 4

static const int adornment_commands[] = { 1, 16, 83 };

static BOOL is_command_bar(const WCHAR *class_name)
{
    return !wcsncmp(class_name, L"Toolbar", 7) || !wcscmp(class_name, L"CommandBar");
}

static void log_child(HWND child, const WCHAR *class_name, const char *action)
{
    char line[LOG_LINE_LENGTH];
    RECT rectangle;

    if (!log_line)
        return;
    strcpy(line, "    child ");
    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, line + strlen(line), 64, NULL, NULL);
    if (GetWindowRect(child, &rectangle)) {
        strcat(line, " ");
        append_number(line, rectangle.left, ",");
        append_number(line, rectangle.top, " ");
        append_number(line, rectangle.right, ",");
        append_number(line, rectangle.bottom, "");
    }
    strcat(line, action);
    log_line(line);
}

struct remote_buffer {
    HANDLE process;
    void *address;
};

static BOOL remote_get_button(HWND bar, struct remote_buffer *buffer, int index, TBBUTTON *button)
{
    return SendMessageW(bar, TB_GETBUTTON, index, (LPARAM)buffer->address)
        && ReadProcessMemory(buffer->process, buffer->address, button, sizeof(*button), NULL);
}

static BOOL remote_get_item_rectangle(HWND bar, struct remote_buffer *buffer, int index, RECT *rectangle)
{
    return SendMessageW(bar, TB_GETITEMRECT, index, (LPARAM)buffer->address)
        && ReadProcessMemory(buffer->process, buffer->address, rectangle, sizeof(*rectangle), NULL);
}

static BOOL is_adornment(int command)
{
    unsigned int index;

    for (index = 0; index < sizeof(adornment_commands) / sizeof(adornment_commands[0]); index++)
        if (adornment_commands[index] == command)
            return TRUE;
    return FALSE;
}

static BOOL align_adornments(HWND bar, struct remote_buffer *buffer)
{
    int count = (int)SendMessageW(bar, TB_BUTTONCOUNT, 0, 0);
    int spacer = -1;
    int index, width;
    TBBUTTON button;
    TBBUTTONINFOW information;
    RECT rectangle;

    for (index = count - 1; index >= 0 && index > count - 1 - ADORNMENTS_SCANNED; index--)
        if (remote_get_button(bar, buffer, index, &button) && is_adornment(button.idCommand))
            spacer = index - 1;
    if (spacer < 0 || !GetClientRect(bar, &rectangle))
        return FALSE;
    width = rectangle.right - rectangle.left;
    for (index = count - 1; index > spacer; index--)
        if (remote_get_item_rectangle(bar, buffer, index, &rectangle))
            width -= rectangle.right - rectangle.left;
    if (!remote_get_button(bar, buffer, spacer, &button))
        return FALSE;
    if (!(button.fsStyle & TBSTYLE_SEP) || (DWORD)button.idCommand != ADORNMENT_SPACER_COMMAND)
        return FALSE;
    if (!remote_get_item_rectangle(bar, buffer, spacer, &rectangle) || rectangle.left >= width)
        return FALSE;
    memset(&information, 0, sizeof(information));
    information.cbSize = sizeof(information);
    information.dwMask = TBIF_SIZE;
    information.cx = (WORD)(width - rectangle.left);
    if (!WriteProcessMemory(buffer->process, buffer->address, &information, sizeof(information), NULL))
        return FALSE;
    return (BOOL)SendMessageW(bar, TB_SETBUTTONINFO, ADORNMENT_SPACER_COMMAND, (LPARAM)buffer->address);
}

static void fit_command_bars(HWND window)
{
    struct remote_buffer buffer;
    WCHAR class_name[64];
    DWORD process_id = 0;
    HWND child;

    GetWindowThreadProcessId(window, &process_id);
    buffer.process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);
    if (!buffer.process)
        return;
    buffer.address = VirtualAllocEx(buffer.process, NULL, sizeof(TBBUTTONINFOW), MEM_COMMIT, PAGE_READWRITE);
    for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!GetClassNameW(child, class_name, sizeof(class_name) / sizeof(class_name[0])))
            continue;
        log_child(child, class_name, "");
        if (!is_command_bar(class_name))
            continue;
        SendMessageW(child, TB_AUTOSIZE, 0, 0);
        if (buffer.address)
            log_child(child, class_name, align_adornments(child, &buffer) ? " adornments aligned" : " adornments not aligned");
    }
    if (buffer.address)
        VirtualFreeEx(buffer.process, buffer.address, 0, MEM_RELEASE);
    CloseHandle(buffer.process);
}

static void invalidate_tree(HWND window)
{
    HWND child;

    InvalidateRect(window, NULL, TRUE);
    for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        invalidate_tree(child);
}

struct reflow {
    const struct display_layout *before;
    struct display_layout after;
};

static BOOL CALLBACK reflow_window(HWND window, LPARAM parameter)
{
    struct reflow *reflow = (struct reflow *)parameter;
    RECT rectangle;

    DWORD result;

    if (!IsWindowVisible(window) || !GetWindowRect(window, &rectangle))
        return TRUE;
    if (!ce_send_message_timeout(window, WM_NULL, 0, 0, SMTO_NORMAL, RESPONSE_TIMEOUT_MILLISECONDS, &result)) {
        log_window(window, &rectangle, "skipped, not responding");
        return TRUE;
    }
    if (same_rectangle(&rectangle, &reflow->before->screen)) {
        log_window(window, &rectangle, "moved to screen");
        move_to(window, &reflow->after.screen);
        fit_command_bars(window);
    } else if (same_rectangle(&rectangle, &reflow->before->work_area)) {
        log_window(window, &rectangle, "moved to work area");
        move_to(window, &reflow->after.work_area);
        fit_command_bars(window);
    } else {
        log_window(window, &rectangle, "kept");
    }
    invalidate_tree(window);
    UpdateWindow(window);
    return TRUE;
}

static void release_panel(void)
{
    HANDLE panel = CreateFileW(PANEL_DEVICE, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    BOOL released = FALSE;

    if (panel != INVALID_HANDLE_VALUE) {
        released = DeviceIoControl(panel, IOCTL_PANEL_RELEASE, NULL, 0, NULL, 0, NULL, NULL);
        CloseHandle(panel);
    }
    if (log_line)
        log_line(released ? " panel released" : " panel release failed");
}

void display_reflow(const struct display_layout *before)
{
    struct reflow reflow;

    reflow.before = before;
    display_layout_capture(&reflow.after);
    if (log_line) {
        char line[LOG_LINE_LENGTH];

        strcpy(line, " reflow before work ");
        append_number(line, before->work_area.left, ",");
        append_number(line, before->work_area.top, " ");
        append_number(line, before->work_area.right, ",");
        append_number(line, before->work_area.bottom, " after work ");
        append_number(line, reflow.after.work_area.left, ",");
        append_number(line, reflow.after.work_area.top, " ");
        append_number(line, reflow.after.work_area.right, ",");
        append_number(line, reflow.after.work_area.bottom, "");
        log_line(line);
    }
    release_panel();
    EnumWindows(reflow_window, (LPARAM)&reflow);
    InvalidateRect(NULL, NULL, TRUE);
}

static void prepare_mode(BYTE *mode)
{
    memset(mode, 0, CE_DEVMODE_SIZE);
    *(WORD *)(mode + CE_DEVMODE_SIZE_OFFSET) = CE_DEVMODE_SIZE;
    *(DWORD *)(mode + CE_DEVMODE_FIELDS_OFFSET) = CE_DM_DISPLAYORIENTATION;
}

BOOL display_rotate(DWORD degrees)
{
    DWORD mode_words[CE_DEVMODE_SIZE / sizeof(DWORD)];
    BYTE *mode = (BYTE *)mode_words;
    struct display_layout before;
    unsigned int index;

    for (index = 0; index < sizeof(orientations) / sizeof(orientations[0]); index++) {
        if (orientations[index][0] != degrees)
            continue;
        if (display_current_rotation() == degrees)
            return TRUE;
        display_layout_capture(&before);
        prepare_mode(mode);
        *(DWORD *)(mode + CE_DEVMODE_ORIENTATION_OFFSET) = orientations[index][1];
        if (ce_change_display_settings(NULL, mode, NULL, CE_CDS_RESET, NULL) != DISP_CHANGE_SUCCESSFUL)
            return FALSE;
        display_reflow(&before);
        return TRUE;
    }
    return FALSE;
}

DWORD display_current_rotation(void)
{
    DWORD mode_words[CE_DEVMODE_SIZE / sizeof(DWORD)];
    BYTE *mode = (BYTE *)mode_words;
    DWORD orientation;
    unsigned int index;

    prepare_mode(mode);
    if (ce_change_display_settings(NULL, mode, NULL, CE_CDS_TEST, NULL) != DISP_CHANGE_SUCCESSFUL)
        return 0;
    orientation = *(DWORD *)(mode + CE_DEVMODE_ORIENTATION_OFFSET);
    for (index = 0; index < sizeof(orientations) / sizeof(orientations[0]); index++)
        if (orientations[index][1] == orientation)
            return orientations[index][0];
    return 0;
}

static DWORD backlight_level(DWORD level)
{
    HANDLE backlight = CreateFileW(BACKLIGHT_DEVICE, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD previous = 0, returned = 0;

    if (backlight == INVALID_HANDLE_VALUE)
        return 0;
    if (!DeviceIoControl(backlight, IOCTL_BACKLIGHT_LEVEL, &level, sizeof(level), &previous, sizeof(previous), &returned, NULL))
        previous = 0;
    CloseHandle(backlight);
    return previous;
}

BOOL display_set_brightness(DWORD level)
{
    return level && backlight_level(level) != 0;
}

DWORD display_current_brightness(void)
{
    return backlight_level(BACKLIGHT_QUERY);
}
