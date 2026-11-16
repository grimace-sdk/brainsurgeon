#include <windows.h>
#include <commctrl.h>
#include "brain-control.h"
#include "brain-control-resource.h"

#define SETTINGS_KEY L"Software\\Brain\\Touchpad"
#define TOUCHPAD_DEVICE L"PAD1:"
#define PAD_IOCTL_RELOAD 0x103
#define SPEED_MINIMUM 1
#define SPEED_MAXIMUM 5
#define DEFAULT_SPEED 3

struct touchpad_settings {
    DWORD enabled;
    DWORD speed;
    DWORD acceleration;
    DWORD tap_to_click;
};

static struct touchpad_settings original_settings;
static BOOL changed;

BOOL touchpad_page_available(void)
{
    return device_present(TOUCHPAD_DEVICE);
}

static void read_settings(struct touchpad_settings *settings)
{
    settings->enabled = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Enabled", 1);
    settings->speed = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Speed", DEFAULT_SPEED);
    settings->acceleration = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Acceleration", 1);
    settings->tap_to_click = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"TapToClick", 1);
    if (settings->speed < SPEED_MINIMUM || settings->speed > SPEED_MAXIMUM)
        settings->speed = DEFAULT_SPEED;
}

static BOOL write_settings(const struct touchpad_settings *settings)
{
    static const WCHAR *const names[] = { L"Enabled", L"Speed", L"Acceleration", L"TapToClick" };
    const DWORD values[] = { settings->enabled, settings->speed, settings->acceleration, settings->tap_to_click };
    BOOL written = registry_write_dwords(HKEY_LOCAL_MACHINE, SETTINGS_KEY, names, values, sizeof(names) / sizeof(names[0]));
    HANDLE device = device_open(TOUCHPAD_DEVICE);

    if (device != INVALID_HANDLE_VALUE) {
        DeviceIoControl(device, PAD_IOCTL_RELOAD, NULL, 0, NULL, 0, NULL, NULL);
        CloseHandle(device);
    }
    return written;
}

static void chosen_settings(HWND page, struct touchpad_settings *settings)
{
    settings->enabled = IsDlgButtonChecked(page, IDC_PAD_ENABLED) == BST_CHECKED;
    settings->speed = (DWORD)SendDlgItemMessageW(page, IDC_PAD_SPEED, TBM_GETPOS, 0, 0);
    settings->acceleration = IsDlgButtonChecked(page, IDC_PAD_ACCELERATION) == BST_CHECKED;
    settings->tap_to_click = IsDlgButtonChecked(page, IDC_PAD_TAP_TO_CLICK) == BST_CHECKED;
}

static void apply_chosen(HWND page)
{
    struct touchpad_settings settings;

    changed = TRUE;
    chosen_settings(page, &settings);
    write_settings(&settings);
}

static void show_settings(HWND page)
{
    changed = FALSE;
    read_settings(&original_settings);
    CheckDlgButton(page, IDC_PAD_ENABLED, original_settings.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_PAD_ACCELERATION, original_settings.acceleration ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_PAD_TAP_TO_CLICK, original_settings.tap_to_click ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(page, IDC_PAD_SPEED, TBM_SETRANGE, TRUE, MAKELONG(SPEED_MINIMUM, SPEED_MAXIMUM));
    SendDlgItemMessageW(page, IDC_PAD_SPEED, TBM_SETPOS, TRUE, original_settings.speed);
}

BOOL CALLBACK touchpad_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(page, IDC_PAD_SPEED)) {
            apply_chosen(page);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED
            && (LOWORD(wparam) == IDC_PAD_ENABLED || LOWORD(wparam) == IDC_PAD_ACCELERATION || LOWORD(wparam) == IDC_PAD_TAP_TO_CLICK)) {
            apply_chosen(page);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void touchpad_page_save(HWND page)
{
    struct touchpad_settings settings;

    if (!changed)
        return;
    chosen_settings(page, &settings);
    if (!write_settings(&settings))
        MessageBoxW(page, L"Could not save the touchpad settings.", L"Touchpad", MB_OK | MB_ICONEXCLAMATION);
}

void touchpad_page_revert(HWND page)
{
    if (changed)
        write_settings(&original_settings);
}
