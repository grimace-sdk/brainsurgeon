#include <windows.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "../display.h"

static const struct {
    DWORD degrees;
    int button;
    const WCHAR *name;
} orientations[] = {
    { 0, IDC_ROTATION_0, L"Landscape (0)" },
    { 90, IDC_ROTATION_90, L"Portrait (90)" },
    { 180, IDC_ROTATION_180, L"Landscape (180)" },
    { 270, IDC_ROTATION_270, L"Portrait (270)" },
};

#define ORIENTATION_COUNT (sizeof(orientations) / sizeof(orientations[0]))

static BOOL changed;

static unsigned int orientation_index(DWORD degrees)
{
    unsigned int index;

    for (index = 0; index < ORIENTATION_COUNT; index++)
        if (orientations[index].degrees == degrees)
            return index;
    return 0;
}

static BOOL has_tablet_mode(void)
{
    DWORD model = model_current();

    return model == MODEL_PWSH2 || model == MODEL_PWSH4 || model == MODEL_UNKNOWN;
}

static void update_tablet_controls(HWND page)
{
    BOOL enabled = IsDlgButtonChecked(page, IDC_AUTO_ROTATE) == BST_CHECKED;

    EnableWindow(GetDlgItem(page, IDC_TABLET_ROTATION), enabled);
    EnableWindow(GetDlgItem(page, IDC_TABLET_LABEL), enabled);
}

static void show_settings(HWND page)
{
    struct display_settings settings;
    int shown = has_tablet_mode() ? SW_SHOW : SW_HIDE;
    unsigned int index;

    changed = FALSE;
    display_settings_read(&settings);
    CheckRadioButton(page, IDC_ROTATION_0, IDC_ROTATION_270, orientations[orientation_index(display_current_rotation())].button);
    CheckDlgButton(page, IDC_STARTUP, settings.startup_rotation ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_AUTO_ROTATE, settings.auto_rotate ? BST_CHECKED : BST_UNCHECKED);
    for (index = 0; index < ORIENTATION_COUNT; index++)
        SendDlgItemMessageW(page, IDC_TABLET_ROTATION, CB_ADDSTRING, 0, (LPARAM)orientations[index].name);
    SendDlgItemMessageW(page, IDC_TABLET_ROTATION, CB_SETCURSEL, orientation_index(settings.tablet_rotation), 0);
    ShowWindow(GetDlgItem(page, IDC_AUTO_ROTATE), shown);
    ShowWindow(GetDlgItem(page, IDC_TABLET_LABEL), shown);
    ShowWindow(GetDlgItem(page, IDC_TABLET_ROTATION), shown);
    update_tablet_controls(page);
}

BOOL CALLBACK rotation_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == CBN_SELCHANGE)
            changed = TRUE;
        if (LOWORD(wparam) == IDC_AUTO_ROTATE) {
            update_tablet_controls(page);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void rotation_page_save(HWND page)
{
    struct display_settings settings;
    UINT apply_message;
    HWND autorotate;
    LRESULT tablet = SendDlgItemMessageW(page, IDC_TABLET_ROTATION, CB_GETCURSEL, 0, 0);
    DWORD rotation = 0;
    unsigned int index;

    if (!changed)
        return;
    for (index = 0; index < ORIENTATION_COUNT; index++)
        if (IsDlgButtonChecked(page, orientations[index].button) == BST_CHECKED)
            rotation = orientations[index].degrees;
    display_settings_read(&settings);
    settings.startup_rotation = IsDlgButtonChecked(page, IDC_STARTUP) == BST_CHECKED ? rotation : 0;
    settings.auto_rotate = IsDlgButtonChecked(page, IDC_AUTO_ROTATE) == BST_CHECKED;
    if (tablet >= 0 && tablet < (LRESULT)ORIENTATION_COUNT)
        settings.tablet_rotation = orientations[tablet].degrees;
    if (!display_settings_write(&settings))
        MessageBoxW(page, L"Could not save display.txt on the card.", L"Rotation", MB_OK | MB_ICONEXCLAMATION);
    apply_message = RegisterWindowMessageW(AUTOROTATE_APPLY_MESSAGE);
    autorotate = FindWindowW(AUTOROTATE_WINDOW_CLASS, NULL);
    if (autorotate && apply_message)
        PostMessageW(autorotate, apply_message, rotation, 0);
    else
        display_rotate(rotation);
}
