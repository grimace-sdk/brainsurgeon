#include "power-settings.h"
#include "config.h"

#define TIMEOUTS_KEY L"System\\CurrentControlSet\\Control\\Power\\Timeouts"
#define RELOAD_TIMEOUTS_EVENT L"PowerManager/ReloadActivityTimeouts"
#define NEVER_TEXT L"never"
#define NUMBER_LENGTH 16

static const WCHAR *const setting_keys[POWER_SOURCE_COUNT][POWER_STEP_COUNT] = {
    { L"battery-dim", L"battery-off", L"battery-sleep" },
    { L"ac-dim", L"ac-off", L"ac-sleep" },
};

static const WCHAR *const timeout_values[POWER_SOURCE_COUNT][POWER_STEP_COUNT] = {
    { L"BattUserIdle", L"BattSystemIdle", L"BattSuspend" },
    { L"ACUserIdle", L"ACSystemIdle", L"ACSuspend" },
};

static const DWORD default_seconds[POWER_SOURCE_COUNT][POWER_STEP_COUNT] = {
    { 30, 60, 180 },
    { 60, 300, POWER_NEVER },
};

static BOOL parse_seconds(const WCHAR *text, DWORD *seconds)
{
    const WCHAR *digit;

    if (!wcscmp(text, NEVER_TEXT)) {
        *seconds = POWER_NEVER;
        return TRUE;
    }
    if (!text[0])
        return FALSE;
    for (digit = text; *digit; digit++)
        if (*digit < L'0' || *digit > L'9')
            return FALSE;
    *seconds = (DWORD)_wtol(text);
    return TRUE;
}

void power_timeouts_read(struct power_timeouts *timeouts)
{
    WCHAR values[POWER_SOURCE_COUNT][POWER_STEP_COUNT][CONFIG_VALUE_LENGTH];
    struct config_entry entries[POWER_SOURCE_COUNT * POWER_STEP_COUNT];
    unsigned int source, step, count = 0;

    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++) {
            entries[count].key = setting_keys[source][step];
            entries[count].value = values[source][step];
            count++;
        }
    config_read_settings(POWER_SETTINGS_FILE, entries, count);
    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++)
            if (!parse_seconds(values[source][step], &timeouts->seconds[source][step]))
                timeouts->seconds[source][step] = default_seconds[source][step];
}

static DWORD next_set(const DWORD *seconds, unsigned int step)
{
    for (; step < POWER_STEP_COUNT; step++)
        if (seconds[step] != POWER_NEVER)
            return seconds[step];
    return POWER_NEVER;
}

static void scheme_steps(const DWORD *seconds, DWORD *steps)
{
    DWORD reached = 0, at;
    unsigned int step;

    for (step = 0; step < POWER_STEP_COUNT; step++) {
        at = next_set(seconds, step);
        if (at == POWER_NEVER) {
            steps[step] = POWER_NEVER;
            continue;
        }
        steps[step] = at > reached ? at - reached : 1;
        reached += steps[step];
    }
}

BOOL power_timeouts_apply(const struct power_timeouts *timeouts)
{
    DWORD steps[POWER_STEP_COUNT], disposition;
    BOOL written = TRUE;
    unsigned int source, step;
    HANDLE reload;
    HKEY key;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, TIMEOUTS_KEY, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return FALSE;
    for (source = 0; source < POWER_SOURCE_COUNT; source++) {
        scheme_steps(timeouts->seconds[source], steps);
        for (step = 0; step < POWER_STEP_COUNT; step++)
            written &= RegSetValueExW(key, timeout_values[source][step], 0, REG_DWORD, (const BYTE *)&steps[step],
                sizeof(DWORD)) == ERROR_SUCCESS;
    }
    RegCloseKey(key);
    reload = OpenEventW(EVENT_ALL_ACCESS, FALSE, RELOAD_TIMEOUTS_EVENT);
    if (reload) {
        SetEvent(reload);
        CloseHandle(reload);
    }
    return written;
}

BOOL power_settings_write(const WCHAR *battery, const struct power_timeouts *timeouts)
{
    WCHAR numbers[POWER_SOURCE_COUNT][POWER_STEP_COUNT][NUMBER_LENGTH];
    WCHAR battery_value[CONFIG_VALUE_LENGTH];
    struct config_entry entries[1 + POWER_SOURCE_COUNT * POWER_STEP_COUNT];
    unsigned int source, step, count = 0;

    if (battery[0]) {
        wcscpy(battery_value, battery);
        entries[count].key = L"battery";
        entries[count].value = battery_value;
        count++;
    }
    for (source = 0; source < POWER_SOURCE_COUNT; source++)
        for (step = 0; step < POWER_STEP_COUNT; step++) {
            if (timeouts->seconds[source][step] == POWER_NEVER)
                wcscpy(numbers[source][step], NEVER_TEXT);
            else
                wsprintfW(numbers[source][step], L"%u", timeouts->seconds[source][step]);
            entries[count].key = setting_keys[source][step];
            entries[count].value = numbers[source][step];
            count++;
        }
    return config_write_settings(POWER_SETTINGS_FILE, entries, count);
}
