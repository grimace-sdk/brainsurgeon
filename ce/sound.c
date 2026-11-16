#include "sound.h"
#include "config.h"

#define SOUND_SETTINGS_FILE L"sound.txt"
#define VOLUME_KEY L"ControlPanel\\Volume"
#define KEY_CLICK_VALUE L"Key"
#define SCREEN_TAP_VALUE L"Screen"
#define CLICK_LEVEL_MASK 0x0000ffff
#define CLICK_SOFT 0x00000001
#define CLICK_DEFAULT 0x00010000
#define BUZZER_EVENTS_KEY L"Drivers\\BuiltIn\\Buzzer\\Event"
#define BUZZER_KEY_EVENT L"Click"
#define BUZZER_TAP_EVENT L"Touch"
#define BUZZER_RESET_EVENT L"Buzzer_Event_Reset"

HANDLE WINAPI CeFindFirstRegChange(HKEY key, BOOL watch_subtree, DWORD filter);
void WINAPI AudioUpdateFromRegistry(void);
BOOL WINAPI CeFindNextRegChange(HANDLE change);
BOOL WINAPI CeFindCloseRegChange(HANDLE change);

struct click_settings {
    BOOL keys;
    BOOL taps;
};

static struct click_settings applied;

static BOOL read_click_value(HKEY key, const WCHAR *name)
{
    DWORD value, size = sizeof(value), type;

    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return FALSE;
    return (value & CLICK_LEVEL_MASK) != 0;
}

static void read_volume_settings(struct click_settings *settings)
{
    HKEY key;

    settings->keys = FALSE;
    settings->taps = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, VOLUME_KEY, 0, 0, &key) != ERROR_SUCCESS)
        return;
    settings->keys = read_click_value(key, KEY_CLICK_VALUE);
    settings->taps = read_click_value(key, SCREEN_TAP_VALUE);
    RegCloseKey(key);
}

static void write_click_value(HKEY key, const WCHAR *name, BOOL on)
{
    DWORD value = CLICK_DEFAULT, size = sizeof(value), type;

    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        value = CLICK_DEFAULT;
    if (((value & CLICK_LEVEL_MASK) != 0) == on)
        return;
    value = (value & ~CLICK_LEVEL_MASK) | (on ? CLICK_SOFT : 0);
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void write_volume_settings(const struct click_settings *settings)
{
    HKEY key;
    DWORD disposition;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, VOLUME_KEY, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return;
    write_click_value(key, KEY_CLICK_VALUE, settings->keys);
    write_click_value(key, SCREEN_TAP_VALUE, settings->taps);
    RegCloseKey(key);
}

static BOOL read_saved_settings(struct click_settings *settings)
{
    WCHAR keys[CONFIG_VALUE_LENGTH], taps[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = { { L"key-clicks", keys }, { L"screen-taps", taps } };

    if (!config_read_settings(SOUND_SETTINGS_FILE, entries, 2))
        return FALSE;
    settings->keys = wcscmp(keys, L"no") != 0;
    settings->taps = wcscmp(taps, L"no") != 0;
    return TRUE;
}

static void save_settings(const struct click_settings *settings)
{
    WCHAR keys[CONFIG_VALUE_LENGTH], taps[CONFIG_VALUE_LENGTH];
    struct config_entry entries[] = { { L"key-clicks", keys }, { L"screen-taps", taps } };

    wcscpy(keys, settings->keys ? L"yes" : L"no");
    wcscpy(taps, settings->taps ? L"yes" : L"no");
    config_write_settings(SOUND_SETTINGS_FILE, entries, 2);
}

static void set_buzzer_event(HKEY events, const WCHAR *name, BOOL on)
{
    HKEY event;
    DWORD disposition;

    if (!on) {
        RegDeleteKeyW(events, name);
        return;
    }
    if (RegCreateKeyExW(events, name, 0, NULL, 0, 0, NULL, &event, &disposition) == ERROR_SUCCESS)
        RegCloseKey(event);
}

static void apply_to_buzzer(const struct click_settings *settings)
{
    HKEY events;
    HANDLE reset;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, BUZZER_EVENTS_KEY, 0, 0, &events) != ERROR_SUCCESS)
        return;
    set_buzzer_event(events, BUZZER_KEY_EVENT, settings->keys);
    set_buzzer_event(events, BUZZER_TAP_EVENT, settings->taps);
    RegCloseKey(events);
    reset = CreateEventW(NULL, FALSE, FALSE, BUZZER_RESET_EVENT);
    if (reset) {
        SetEvent(reset);
        CloseHandle(reset);
    }
    applied = *settings;
}

void sound_restore(void)
{
    struct click_settings settings;

    if (!read_saved_settings(&settings)) {
        settings.keys = FALSE;
        settings.taps = FALSE;
    }
    write_volume_settings(&settings);
    AudioUpdateFromRegistry();
    apply_to_buzzer(&settings);
}

void sound_watch(void)
{
    struct click_settings settings;
    HANDLE change;
    HKEY key;
    DWORD disposition;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, VOLUME_KEY, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return;
    change = CeFindFirstRegChange(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET);
    if (change == INVALID_HANDLE_VALUE || !change) {
        RegCloseKey(key);
        return;
    }
    for (;;) {
        DWORD result = MsgWaitForMultipleObjectsEx(1, &change, INFINITE, QS_ALLINPUT, 0);
        MSG message;

        if (result == WAIT_OBJECT_0 + 1) {
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        if (result != WAIT_OBJECT_0)
            break;
        read_volume_settings(&settings);
        if (settings.keys != applied.keys || settings.taps != applied.taps) {
            apply_to_buzzer(&settings);
            save_settings(&settings);
        }
        if (!CeFindNextRegChange(change))
            break;
    }
    CeFindCloseRegChange(change);
    RegCloseKey(key);
}
