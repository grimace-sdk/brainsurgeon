#include <windows.h>
#include <mmsystem.h>
#include <commctrl.h>
#include "brain-control.h"
#include "brain-control-resource.h"

#define VOLUME_KEY L"ControlPanel\\Volume"
#define VOLUME_VALUE L"Volume"
#define KEY_CLICK_VALUE L"Key"
#define SCREEN_TAP_VALUE L"Screen"
#define CLICK_LEVEL_MASK 0x0000ffff
#define CLICK_SOFT 0x00000001
#define CLICK_DEFAULT 0x00010000
#define CHANNEL_MAXIMUM 0xffff
#define VOLUME_STEPS 100
#define VOLUME_PAGE 10
#define VOLUME_SAMPLE L"\\Windows\\default.wav"

static DWORD original_volume;
static BOOL volume_available;
static BOOL volume_changed;
static BOOL clicks_changed;

void WINAPI AudioUpdateFromRegistry(void);

static DWORD volume_from_position(LRESULT position)
{
    DWORD channel = (DWORD)position * CHANNEL_MAXIMUM / VOLUME_STEPS;

    return channel << 16 | channel;
}

static int position_from_volume(DWORD volume)
{
    DWORD left = volume & CHANNEL_MAXIMUM, right = volume >> 16;

    return (int)((max(left, right) * VOLUME_STEPS + CHANNEL_MAXIMUM / 2) / CHANNEL_MAXIMUM);
}

static BOOL click_on(const WCHAR *name)
{
    return (registry_read_dword(HKEY_CURRENT_USER, VOLUME_KEY, name, 0) & CLICK_LEVEL_MASK) != 0;
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

static void show_settings(HWND page)
{
    volume_changed = FALSE;
    clicks_changed = FALSE;
    volume_available = waveOutGetVolume(0, &original_volume) == MMSYSERR_NOERROR;
    SendDlgItemMessageW(page, IDC_VOLUME, TBM_SETRANGE, TRUE, MAKELONG(0, VOLUME_STEPS));
    SendDlgItemMessageW(page, IDC_VOLUME, TBM_SETPAGESIZE, 0, VOLUME_PAGE);
    SendDlgItemMessageW(page, IDC_VOLUME, TBM_SETPOS, TRUE, volume_available ? position_from_volume(original_volume) : 0);
    EnableWindow(GetDlgItem(page, IDC_VOLUME), volume_available);
    CheckDlgButton(page, IDC_KEY_CLICKS, click_on(KEY_CLICK_VALUE) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_SCREEN_TAPS, click_on(SCREEN_TAP_VALUE) ? BST_CHECKED : BST_UNCHECKED);
}

BOOL CALLBACK sound_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(page, IDC_VOLUME) && volume_available) {
            volume_changed = TRUE;
            waveOutSetVolume(0, volume_from_position(SendDlgItemMessageW(page, IDC_VOLUME, TBM_GETPOS, 0, 0)));
            if (LOWORD(wparam) == TB_ENDTRACK)
                PlaySoundW(VOLUME_SAMPLE, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED && (LOWORD(wparam) == IDC_KEY_CLICKS || LOWORD(wparam) == IDC_SCREEN_TAPS)) {
            clicks_changed = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void sound_page_save(HWND page)
{
    DWORD disposition, volume;
    HKEY key;

    if (!volume_changed && !clicks_changed)
        return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, VOLUME_KEY, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return;
    if (volume_changed) {
        volume = volume_from_position(SendDlgItemMessageW(page, IDC_VOLUME, TBM_GETPOS, 0, 0));
        RegSetValueExW(key, VOLUME_VALUE, 0, REG_DWORD, (const BYTE *)&volume, sizeof(volume));
    }
    if (clicks_changed) {
        write_click_value(key, KEY_CLICK_VALUE, IsDlgButtonChecked(page, IDC_KEY_CLICKS) == BST_CHECKED);
        write_click_value(key, SCREEN_TAP_VALUE, IsDlgButtonChecked(page, IDC_SCREEN_TAPS) == BST_CHECKED);
    }
    RegCloseKey(key);
    AudioUpdateFromRegistry();
}

void sound_page_revert(HWND page)
{
    if (volume_changed)
        waveOutSetVolume(0, original_volume);
}
