#include <windows.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "../config.h"
#include "../power-settings.h"

#define BATTERY_DEVICE L"BAT1:"
#define IOCTL_BATTERY_SET_TYPE 0x00290418
#define BATTERY_TYPE_ALKALINE 0
#define BATTERY_TYPE_NIMH 1
#define BATTERY_CHOICE_UNSET 0
#define BATTERY_CHOICE_ALKALINE 1
#define BATTERY_CHOICE_NIMH 2
#define STATUS_TIMER 1
#define STATUS_INTERVAL_MILLISECONDS 5000
#define LEVEL_UNKNOWN 255
#define SECONDS_PER_MINUTE 60

static const WCHAR *const battery_names[] = { L"Not set", L"Alkaline", L"Rechargeable NiMH" };
static const WCHAR *const battery_values[] = { L"", L"alkaline", L"nimh" };

#define BATTERY_CHOICE_COUNT (sizeof(battery_names) / sizeof(battery_names[0]))

static const int timeout_controls[POWER_SOURCE_COUNT][POWER_STEP_COUNT] = {
    { IDC_BATTERY_DIM, IDC_BATTERY_OFF, IDC_BATTERY_SLEEP },
    { IDC_AC_DIM, IDC_AC_OFF, IDC_AC_SLEEP },
};

static const DWORD timeout_choices[] = { 15, 30, 60, 120, 180, 300, 600, 900, 1800, POWER_NEVER };

#define TIMEOUT_CHOICE_COUNT (sizeof(timeout_choices) / sizeof(timeout_choices[0]))

static const int battery_controls[] = { IDC_BATTERY_LABEL, IDC_BATTERY_TYPE, IDC_BATTERY_NOTE };

#define BATTERY_CONTROL_COUNT (sizeof(battery_controls) / sizeof(battery_controls[0]))

static int original_choice;
static WCHAR saved_battery[CONFIG_VALUE_LENGTH];
static struct power_timeouts original_timeouts;
static BOOL has_battery_type;
static BOOL changed;

static int saved_choice(void)
{
    struct config_entry entries[] = { { L"battery", saved_battery } };
    unsigned int index;

    config_read_settings(POWER_SETTINGS_FILE, entries, 1);
    for (index = BATTERY_CHOICE_ALKALINE; index < BATTERY_CHOICE_COUNT; index++)
        if (!wcscmp(saved_battery, battery_values[index]))
            return (int)index;
    return BATTERY_CHOICE_UNSET;
}

static int chosen(HWND page)
{
    LRESULT choice = SendDlgItemMessageW(page, IDC_BATTERY_TYPE, CB_GETCURSEL, 0, 0);

    if (choice < 0 || choice >= (LRESULT)BATTERY_CHOICE_COUNT)
        return BATTERY_CHOICE_UNSET;
    return (int)choice;
}

static void send_battery_type(int choice)
{
    DWORD type = choice == BATTERY_CHOICE_NIMH ? BATTERY_TYPE_NIMH : BATTERY_TYPE_ALKALINE, returned;
    HANDLE device = CreateFileW(BATTERY_DEVICE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE)
        return;
    DeviceIoControl(device, IOCTL_BATTERY_SET_TYPE, &type, sizeof(type), NULL, 0, &returned, NULL);
    CloseHandle(device);
}

static void show_status(HWND page)
{
    SYSTEM_POWER_STATUS_EX2 status;
    WCHAR text[TEXT_LENGTH], level[32];

    memset(&status, 0, sizeof(status));
    if (!GetSystemPowerStatusEx2(&status, sizeof(status), TRUE)) {
        SetDlgItemTextW(page, IDC_BATTERY_STATUS, L"Battery status unavailable");
        return;
    }
    if (status.BatteryLifePercent == LEVEL_UNKNOWN)
        wcscpy(level, L"unknown");
    else
        wsprintfW(level, L"%u%%", status.BatteryLifePercent);
    wsprintfW(text, L"Battery: %s, %u.%02u V\r\n%s", level, status.BatteryVoltage / 1000, status.BatteryVoltage % 1000 / 10,
        status.ACLineStatus == AC_LINE_ONLINE ? L"On external power" : L"On battery");
    SetDlgItemTextW(page, IDC_BATTERY_STATUS, text);
}

static void timeout_text(DWORD seconds, WCHAR *text)
{
    if (seconds == POWER_NEVER)
        wcscpy(text, L"Never");
    else if (seconds % SECONDS_PER_MINUTE)
        wsprintfW(text, L"%u sec", seconds);
    else
        wsprintfW(text, L"%u min", seconds / SECONDS_PER_MINUTE);
}

static void add_timeout_choice(HWND combo, DWORD seconds)
{
    WCHAR text[TEXT_LENGTH];
    LRESULT item;

    timeout_text(seconds, text);
    item = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
    if (item >= 0)
        SendMessageW(combo, CB_SETITEMDATA, item, seconds);
}

static void show_timeout(HWND page, int control, DWORD seconds)
{
    HWND combo = GetDlgItem(page, control);
    unsigned int index;
    LRESULT count;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (index = 0; index < TIMEOUT_CHOICE_COUNT; index++)
        add_timeout_choice(combo, timeout_choices[index]);
    for (index = 0; index < TIMEOUT_CHOICE_COUNT && timeout_choices[index] != seconds; index++)
        ;
    if (index == TIMEOUT_CHOICE_COUNT)
        add_timeout_choice(combo, seconds);
    count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (index = 0; index < (unsigned int)count; index++)
        if ((DWORD)SendMessageW(combo, CB_GETITEMDATA, index, 0) == seconds)
            SendMessageW(combo, CB_SETCURSEL, index, 0);
}

static DWORD chosen_timeout(HWND page, int control)
{
    LRESULT item = SendDlgItemMessageW(page, control, CB_GETCURSEL, 0, 0);

    if (item < 0)
        return POWER_NEVER;
    return (DWORD)SendDlgItemMessageW(page, control, CB_GETITEMDATA, item, 0);
}

static void chosen_timeouts(HWND page, struct power_timeouts *timeouts)
{
    unsigned int source, step;

    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++)
            timeouts->seconds[source][step] = chosen_timeout(page, timeout_controls[source][step]);
}

static BOOL timeouts_differ(const struct power_timeouts *first, const struct power_timeouts *second)
{
    unsigned int source, step;

    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++)
            if (first->seconds[source][step] != second->seconds[source][step])
                return TRUE;
    return FALSE;
}

static void show_settings(HWND page)
{
    unsigned int index, source, step;

    changed = FALSE;
    has_battery_type = model_current() == MODEL_PWA7300;
    original_choice = saved_choice();
    for (index = 0; index < BATTERY_CHOICE_COUNT; index++)
        SendDlgItemMessageW(page, IDC_BATTERY_TYPE, CB_ADDSTRING, 0, (LPARAM)battery_names[index]);
    SendDlgItemMessageW(page, IDC_BATTERY_TYPE, CB_SETCURSEL, original_choice, 0);
    for (index = 0; index < BATTERY_CONTROL_COUNT; index++)
        ShowWindow(GetDlgItem(page, battery_controls[index]), has_battery_type ? SW_SHOW : SW_HIDE);
    power_timeouts_read(&original_timeouts);
    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++)
            show_timeout(page, timeout_controls[source][step], original_timeouts.seconds[source][step]);
    show_status(page);
}

BOOL CALLBACK power_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_TIMER:
        if (wparam == STATUS_TIMER) {
            show_status(page);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) == IDC_BATTERY_TYPE) {
            changed = TRUE;
            send_battery_type(chosen(page));
            show_status(page);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void power_page_shown(HWND page, BOOL visible)
{
    if (!visible) {
        KillTimer(page, STATUS_TIMER);
        return;
    }
    show_status(page);
    SetTimer(page, STATUS_TIMER, STATUS_INTERVAL_MILLISECONDS, NULL);
}

void power_page_save(HWND page)
{
    struct power_timeouts timeouts;
    int choice = chosen(page);
    BOOL battery_changed = has_battery_type && changed && choice != original_choice;
    BOOL timeouts_changed;

    KillTimer(page, STATUS_TIMER);
    chosen_timeouts(page, &timeouts);
    timeouts_changed = timeouts_differ(&timeouts, &original_timeouts);
    if (!battery_changed && !timeouts_changed)
        return;
    if (timeouts_changed)
        power_timeouts_apply(&timeouts);
    if (!power_settings_write(battery_changed ? battery_values[choice] : saved_battery, &timeouts))
        MessageBoxW(page, L"Could not save power.txt on the card.", L"Power", MB_OK | MB_ICONEXCLAMATION);
}

void power_page_revert(HWND page)
{
    KillTimer(page, STATUS_TIMER);
    if (has_battery_type && changed)
        send_battery_type(original_choice);
}
