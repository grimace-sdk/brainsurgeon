#include <windows.h>
#include <commctrl.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "../display.h"

#define BACKLIGHT_DEVICE L"BKL1:"
#define SUB_DISPLAY_DEVICE L"SLD1:"
#define SLD_IOCTL_GET_INFO 0x100
#define SLD_IOCTL_SET_BACKLIGHT 0x102
#define SLD_IOCTL_GET_BACKLIGHT 0x103
#define SLD_INFO_MAGIC 0x32444C53
#define SLD_CAPS_BACKLIGHT_LEVEL 0x00000002
#define SUB_BRIGHTNESS_PAGE 10
#define BKL_IOCTL_GET_INFO 0x100
#define BKL_INFO_MAGIC 0x324C4B42
#define BKL_CAPS_CONTINUOUS_LEVEL 0x00000001
#define BACKLIGHT_KEY L"Software\\Brain\\Backlight"
#define DIM_LEVEL_VALUE L"DimLevel"
#define DEFAULT_DIM_LEVEL 20
#define LEVEL_PAGE 10

struct bkl_info {
    DWORD magic;
    DWORD version;
    DWORD capabilities;
    DWORD applied_level;
    DWORD active_counts;
    DWORD period_counts;
    DWORD low_current;
};

struct sld_info {
    DWORD magic;
    DWORD version;
    DWORD capabilities;
};

static DWORD original_brightness;
static DWORD original_sub_brightness;
static HANDLE sub_display = INVALID_HANDLE_VALUE;
static BOOL changed;
static BOOL continuous;
static BOOL dim_changed;

static BOOL continuous_backlight(void)
{
    struct bkl_info info;
    DWORD returned = 0;
    HANDLE backlight = device_open(BACKLIGHT_DEVICE);
    BOOL found;

    if (backlight == INVALID_HANDLE_VALUE)
        return FALSE;
    memset(&info, 0, sizeof(info));
    found = DeviceIoControl(backlight, BKL_IOCTL_GET_INFO, NULL, 0, &info, sizeof(info), &returned, NULL)
        && returned >= sizeof(info) && info.magic == BKL_INFO_MAGIC && (info.capabilities & BKL_CAPS_CONTINUOUS_LEVEL);
    CloseHandle(backlight);
    return found;
}

static void move_control(HWND page, int control, int place_of)
{
    RECT area;

    GetWindowRect(GetDlgItem(page, place_of), &area);
    MapWindowPoints(NULL, page, (POINT *)&area, 2);
    MoveWindow(GetDlgItem(page, control), area.left, area.top, area.right - area.left, area.bottom - area.top, TRUE);
}

BOOL display_page_available(void)
{
    return device_present(BACKLIGHT_DEVICE);
}

static BOOL open_sub_display(void)
{
    struct sld_info info = { 0, 0, 0 };
    DWORD returned = 0;

    sub_display = device_open(SUB_DISPLAY_DEVICE);
    if (sub_display == INVALID_HANDLE_VALUE)
        return FALSE;
    if (DeviceIoControl(sub_display, SLD_IOCTL_GET_INFO, NULL, 0, &info, sizeof(info), &returned, NULL)
        && returned >= sizeof(info) && info.magic == SLD_INFO_MAGIC && (info.capabilities & SLD_CAPS_BACKLIGHT_LEVEL))
        return TRUE;
    CloseHandle(sub_display);
    sub_display = INVALID_HANDLE_VALUE;
    return FALSE;
}

static void close_sub_display(void)
{
    if (sub_display != INVALID_HANDLE_VALUE)
        CloseHandle(sub_display);
    sub_display = INVALID_HANDLE_VALUE;
}

static DWORD sub_brightness(void)
{
    DWORD level = 0, returned = 0;

    if (!DeviceIoControl(sub_display, SLD_IOCTL_GET_BACKLIGHT, NULL, 0, &level, sizeof(level), &returned, NULL) || returned < sizeof(level))
        return 0;
    return level;
}

static void set_sub_brightness(DWORD level)
{
    DWORD returned;

    if (sub_display != INVALID_HANDLE_VALUE && level)
        DeviceIoControl(sub_display, SLD_IOCTL_SET_BACKLIGHT, &level, sizeof(level), NULL, 0, &returned, NULL);
}

static int brightness_position(DWORD level)
{
    int index;

    for (index = DISPLAY_BRIGHTNESS_LEVELS - 1; index > 0; index--)
        if (level >= display_brightness_levels[index])
            return index;
    return 0;
}

static DWORD chosen_brightness(HWND page)
{
    LRESULT position = SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_GETPOS, 0, 0);

    if (continuous)
        return position >= DISPLAY_BRIGHTNESS_MINIMUM && position <= DISPLAY_BRIGHTNESS_MAXIMUM ? (DWORD)position : 0;

    if (position < 0 || position >= DISPLAY_BRIGHTNESS_LEVELS)
        return 0;
    return display_brightness_levels[position];
}

static void show_settings(HWND page)
{
    BOOL has_sub_display;

    changed = FALSE;
    dim_changed = FALSE;
    continuous = continuous_backlight();
    original_brightness = display_current_brightness();
    if (continuous) {
        SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_SETRANGE, TRUE, MAKELONG(DISPLAY_BRIGHTNESS_MINIMUM, DISPLAY_BRIGHTNESS_MAXIMUM));
        SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_SETPAGESIZE, 0, LEVEL_PAGE);
        SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_SETPOS, TRUE, original_brightness);
    } else {
        SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_SETRANGE, TRUE, MAKELONG(0, DISPLAY_BRIGHTNESS_LEVELS - 1));
        SendDlgItemMessageW(page, IDC_MAIN_BRIGHTNESS, TBM_SETPOS, TRUE, brightness_position(original_brightness));
    }
    EnableWindow(GetDlgItem(page, IDC_MAIN_BRIGHTNESS), original_brightness != 0);

    has_sub_display = open_sub_display();
    original_sub_brightness = has_sub_display ? sub_brightness() : 0;
    SendDlgItemMessageW(page, IDC_SUB_BRIGHTNESS, TBM_SETRANGE, TRUE, MAKELONG(1, 100));
    SendDlgItemMessageW(page, IDC_SUB_BRIGHTNESS, TBM_SETPAGESIZE, 0, SUB_BRIGHTNESS_PAGE);
    SendDlgItemMessageW(page, IDC_SUB_BRIGHTNESS, TBM_SETPOS, TRUE, original_sub_brightness ? original_sub_brightness : 100);
    ShowWindow(GetDlgItem(page, IDC_SUB_LABEL), has_sub_display ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(page, IDC_SUB_BRIGHTNESS), has_sub_display ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(page, IDC_SUB_BRIGHTNESS), original_sub_brightness != 0);

    SendDlgItemMessageW(page, IDC_DIM_LEVEL, TBM_SETRANGE, TRUE, MAKELONG(DISPLAY_BRIGHTNESS_MINIMUM, DISPLAY_BRIGHTNESS_MAXIMUM));
    SendDlgItemMessageW(page, IDC_DIM_LEVEL, TBM_SETPAGESIZE, 0, LEVEL_PAGE);
    SendDlgItemMessageW(page, IDC_DIM_LEVEL, TBM_SETPOS, TRUE, registry_read_dword(HKEY_LOCAL_MACHINE, BACKLIGHT_KEY, DIM_LEVEL_VALUE, DEFAULT_DIM_LEVEL));
    ShowWindow(GetDlgItem(page, IDC_DIM_LABEL), continuous ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(page, IDC_DIM_LEVEL), continuous ? SW_SHOW : SW_HIDE);
    if (continuous && !has_sub_display) {
        move_control(page, IDC_DIM_LEVEL, IDC_SUB_BRIGHTNESS);
        move_control(page, IDC_DIM_LABEL, IDC_SUB_LABEL);
    }
}

BOOL CALLBACK display_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(page, IDC_MAIN_BRIGHTNESS)) {
            changed = TRUE;
            display_set_brightness(chosen_brightness(page));
            return TRUE;
        }
        if ((HWND)lparam == GetDlgItem(page, IDC_DIM_LEVEL)) {
            dim_changed = TRUE;
            return TRUE;
        }
        if ((HWND)lparam == GetDlgItem(page, IDC_SUB_BRIGHTNESS)) {
            set_sub_brightness((DWORD)SendDlgItemMessageW(page, IDC_SUB_BRIGHTNESS, TBM_GETPOS, 0, 0));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void display_page_save(HWND page)
{
    struct display_settings settings;

    close_sub_display();
    if (continuous && dim_changed) {
        static const WCHAR *const names[] = { DIM_LEVEL_VALUE };
        DWORD dim_level = (DWORD)SendDlgItemMessageW(page, IDC_DIM_LEVEL, TBM_GETPOS, 0, 0);

        registry_write_dwords(HKEY_LOCAL_MACHINE, BACKLIGHT_KEY, names, &dim_level, 1);
    }
    if (!changed || !original_brightness)
        return;
    display_settings_read(&settings);
    settings.brightness = chosen_brightness(page);
    if (!display_settings_write(&settings))
        MessageBoxW(page, L"Could not save display.txt on the card.", L"Display", MB_OK | MB_ICONEXCLAMATION);
}

void display_page_revert(HWND page)
{
    if (changed)
        display_set_brightness(original_brightness);
    set_sub_brightness(original_sub_brightness);
    close_sub_display();
}
