#include <windows.h>
#include <commctrl.h>
#include "brain-control.h"
#include "brain-control-resource.h"

#define SETTINGS_KEY L"Software\\Brain\\NightLight"
#define CUSTOMESCAPECODEBASE 100100
#define DISPLAY_NIGHT_LIGHT_PREVIEW (CUSTOMESCAPECODEBASE + 55)
#define MINUTES_PER_DAY (24 * 60)
#define DEFAULT_START (21 * 60)
#define DEFAULT_END (7 * 60)
#define DEFAULT_TRANSITION 60
#define DEFAULT_STRENGTH 100
#define STRENGTH_MINIMUM 10
#define STRENGTH_MAXIMUM 100
#define STRENGTH_STEP 10
#define PREVIEW_TIMER 1
#define PREVIEW_MILLISECONDS 2000

struct night_light_settings {
    DWORD enabled;
    DWORD start;
    DWORD end;
    DWORD transition;
    DWORD strength;
};

static const DWORD transition_minutes[] = { 0, 15, 30, 60, 120 };
static const WCHAR *const transition_names[] = { L"Instantly", L"15 minutes", L"30 minutes", L"1 hour", L"2 hours" };

#define TRANSITION_COUNT (sizeof(transition_minutes) / sizeof(transition_minutes[0]))

static BOOL changed;
static BOOL previewing;

static void read_settings(struct night_light_settings *settings)
{
    settings->enabled = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Enabled", 0);
    settings->start = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Start", DEFAULT_START) % MINUTES_PER_DAY;
    settings->end = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"End", DEFAULT_END) % MINUTES_PER_DAY;
    settings->transition = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Transition", DEFAULT_TRANSITION);
    settings->strength = registry_read_dword(HKEY_LOCAL_MACHINE, SETTINGS_KEY, L"Strength", DEFAULT_STRENGTH);
    if (settings->strength < STRENGTH_MINIMUM || settings->strength > STRENGTH_MAXIMUM)
        settings->strength = DEFAULT_STRENGTH;
}

static BOOL write_settings(const struct night_light_settings *settings)
{
    static const WCHAR *const names[] = { L"Enabled", L"Start", L"End", L"Transition", L"Strength" };
    const DWORD values[] = { settings->enabled, settings->start, settings->end, settings->transition, settings->strength };

    return registry_write_dwords(HKEY_LOCAL_MACHINE, SETTINGS_KEY, names, values, sizeof(names) / sizeof(names[0]));
}

BOOL night_light_page_available(void)
{
    int escape = DISPLAY_NIGHT_LIGHT_PREVIEW;
    HDC display;
    int supported;

    if (model_current() == MODEL_PWA7300)
        return TRUE;
    display = GetDC(NULL);
    supported = ExtEscape(display, QUERYESCSUPPORT, sizeof(escape), (LPCSTR)&escape, 0, NULL);
    ReleaseDC(NULL, display);
    return supported > 0;
}

static void send_preview(BOOL active, DWORD strength)
{
    DWORD request[2] = { active, strength };
    HDC display = GetDC(NULL);

    ExtEscape(display, DISPLAY_NIGHT_LIGHT_PREVIEW, sizeof(request), (LPCSTR)request, 0, NULL);
    ReleaseDC(NULL, display);
}

static void start_preview(HWND page, DWORD strength)
{
    send_preview(TRUE, strength);
    previewing = TRUE;
    ShowWindow(GetDlgItem(page, IDC_NIGHT_PREVIEWING), SW_SHOW);
    SetTimer(page, PREVIEW_TIMER, PREVIEW_MILLISECONDS, NULL);
}

static void end_preview(HWND page)
{
    KillTimer(page, PREVIEW_TIMER);
    ShowWindow(GetDlgItem(page, IDC_NIGHT_PREVIEWING), SW_HIDE);
    if (!previewing)
        return;
    send_preview(FALSE, 0);
    previewing = FALSE;
}

static void set_time(HWND page, int control, DWORD minutes)
{
    SYSTEMTIME time;

    GetLocalTime(&time);
    time.wHour = (WORD)(minutes / 60);
    time.wMinute = (WORD)(minutes % 60);
    time.wSecond = 0;
    time.wMilliseconds = 0;
    SendDlgItemMessageW(page, control, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&time);
}

static DWORD get_time(HWND page, int control, DWORD fallback)
{
    SYSTEMTIME time;

    if (SendDlgItemMessageW(page, control, DTM_GETSYSTEMTIME, 0, (LPARAM)&time) != GDT_VALID)
        return fallback;
    return time.wHour * 60 + time.wMinute;
}

static DWORD chosen_strength(HWND page)
{
    return (DWORD)SendDlgItemMessageW(page, IDC_NIGHT_STRENGTH, TBM_GETPOS, 0, 0) * STRENGTH_STEP;
}

static void show_settings(HWND page)
{
    struct night_light_settings settings;
    unsigned int index, selected = 0;

    changed = FALSE;
    previewing = FALSE;
    ShowWindow(GetDlgItem(page, IDC_NIGHT_PREVIEWING), SW_HIDE);
    read_settings(&settings);
    CheckDlgButton(page, IDC_NIGHT_ENABLED, settings.enabled ? BST_CHECKED : BST_UNCHECKED);
    set_time(page, IDC_NIGHT_START, settings.start);
    set_time(page, IDC_NIGHT_END, settings.end);
    for (index = 0; index < TRANSITION_COUNT; index++) {
        SendDlgItemMessageW(page, IDC_NIGHT_TRANSITION, CB_ADDSTRING, 0, (LPARAM)transition_names[index]);
        if (transition_minutes[index] <= settings.transition)
            selected = index;
    }
    SendDlgItemMessageW(page, IDC_NIGHT_TRANSITION, CB_SETCURSEL, selected, 0);
    SendDlgItemMessageW(page, IDC_NIGHT_STRENGTH, TBM_SETRANGE, TRUE, MAKELONG(STRENGTH_MINIMUM / STRENGTH_STEP, STRENGTH_MAXIMUM / STRENGTH_STEP));
    SendDlgItemMessageW(page, IDC_NIGHT_STRENGTH, TBM_SETPOS, TRUE, settings.strength / STRENGTH_STEP);
}

BOOL CALLBACK night_light_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(page, IDC_NIGHT_STRENGTH)) {
            changed = TRUE;
            start_preview(page, chosen_strength(page));
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (wparam == PREVIEW_TIMER) {
            end_preview(page);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        end_preview(page);
        break;
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->code == DTN_DATETIMECHANGE)
            changed = TRUE;
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_NIGHT_ENABLED && HIWORD(wparam) == BN_CLICKED) {
            changed = TRUE;
            if (IsDlgButtonChecked(page, IDC_NIGHT_ENABLED) == BST_CHECKED)
                start_preview(page, chosen_strength(page));
            else
                end_preview(page);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_NIGHT_TRANSITION && HIWORD(wparam) == CBN_SELCHANGE) {
            changed = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void night_light_page_save(HWND page)
{
    struct night_light_settings settings;
    LRESULT transition = SendDlgItemMessageW(page, IDC_NIGHT_TRANSITION, CB_GETCURSEL, 0, 0);

    if (changed) {
        read_settings(&settings);
        settings.enabled = IsDlgButtonChecked(page, IDC_NIGHT_ENABLED) == BST_CHECKED;
        settings.start = get_time(page, IDC_NIGHT_START, settings.start);
        settings.end = get_time(page, IDC_NIGHT_END, settings.end);
        if (transition >= 0 && transition < (LRESULT)TRANSITION_COUNT)
            settings.transition = transition_minutes[transition];
        settings.strength = chosen_strength(page);
        if (!write_settings(&settings))
            MessageBoxW(page, L"Could not save the night light settings.", L"Night Light", MB_OK | MB_ICONEXCLAMATION);
    }
    end_preview(page);
}

void night_light_page_revert(HWND page)
{
    end_preview(page);
}

void night_light_page_shown(HWND page, BOOL visible)
{
    if (!visible)
        end_preview(page);
}
