#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include "keymap.h"
#include "../config.h"

#define WINDOW_CLASS L"BrainKeymap"
#define WH_KEYBOARD_LL_CE 20
#define VOLUME_KEY_PATH L"ControlPanel\\Volume"
#define VOLUME_VALUE L"Volume"
#define VOLUME_MAXIMUM 0xffff
#define LOG_PATH L"\\Storage Card\\keymap.log"
#define TRAY_ID 1
#define TRAY_ICON_SIZE 16
#define TRAY_ICON_LIMIT 8
#define TRAY_RETRY_MS 2000
#define TRAY_TIMER 99

typedef struct {
    DWORD vkCode;
    DWORD scanCode;
    DWORD flags;
    DWORD time;
    DWORD dwExtraInfo;
} ce_keyboard_hook;

struct ce_tray_icon_data {
    DWORD size;
    HWND window;
    UINT id;
    UINT flags;
    UINT callback_message;
    HICON icon;
    WCHAR tip[64];
};

struct tray_icon {
    const WCHAR *label;
    BOOL active;
    HICON icon;
};

HWND keymap_window;

static const struct keymap_model *model;
static HHOOK hook;
static int injected_pending[256];
static HANDLE log_file = INVALID_HANDLE_VALUE;
static struct tray_icon tray_icons[TRAY_ICON_LIMIT];
static int tray_icon_count;
static HICON tray_shown_icon;
static BOOL tray_added;

static const WORD volume_levels[] = {
    0x0000, 0x0a31, 0x0cd8, 0x102a, 0x1462, 0x199a, 0x203a, 0x2896, 0x332e, 0x40c4, 0x5182, 0x6666, 0x8098, 0xa1e8, 0xcb59, 0xffff,
};

void keymap_log(const char *what, UINT vkey, UINT value)
{
    static const char digits[] = "0123456789abcdef";
    char line[64];
    DWORD written, tick = GetTickCount();
    int length = 0, shift_bits;

    if (log_file == INVALID_HANDLE_VALUE)
        return;
    for (shift_bits = 28; shift_bits >= 0; shift_bits -= 4)
        line[length++] = digits[(tick >> shift_bits) & 15];
    line[length++] = ' ';
    while (*what && length < 40)
        line[length++] = *what++;
    line[length++] = ' ';
    line[length++] = digits[(vkey >> 4) & 15];
    line[length++] = digits[vkey & 15];
    line[length++] = ' ';
    for (shift_bits = 12; shift_bits >= 0; shift_bits -= 4)
        line[length++] = digits[(value >> shift_bits) & 15];
    line[length++] = '\r';
    line[length++] = '\n';
    WriteFile(log_file, line, length, &written, NULL);
}

void keymap_inject(UINT vkey, BOOL up)
{
    injected_pending[vkey & 0xff]++;
    keymap_log(up ? "inject-up" : "inject-down", vkey, 0);
    keybd_event((BYTE)vkey, 0, up ? KEYEVENTF_KEYUP : 0, 0);
}

void keymap_post_character(UINT character)
{
    UINT state = KEYMAP_KEY_STATE_DOWN;

    keymap_log("post-char", 0, character);
    PostKeybdMessage((HWND)-1, 0, KEYMAP_KEY_STATE_DOWN, 1, &state, &character);
    PostKeybdMessage((HWND)-1, 0, KEYMAP_KEY_STATE_PREVIOUS_DOWN, 0, &state, &character);
}

void keymap_notify(void)
{
    PostMessageW(keymap_window, KEYMAP_STATE_CHANGED, 0, 0);
}

void keymap_set_legend(struct help_legends *legends, enum help_ink ink, const WCHAR *text)
{
    lstrcpynW(legends->text[ink], text, KEYMAP_LEGEND_LENGTH);
}

void keymap_set_character(struct help_legends *legends, enum help_ink ink, UINT character)
{
    legends->text[ink][0] = (WCHAR)character;
    legends->text[ink][1] = 0;
}

int keymap_volume_level(int *top_level)
{
    int count = sizeof(volume_levels) / sizeof(volume_levels[0]), index, level = 0;
    DWORD volume = 0;

    *top_level = count - 1;
    waveOutGetVolume(NULL, &volume);
    for (index = 1; index < count; index++)
        if (volume_levels[index] <= (volume & VOLUME_MAXIMUM))
            level = index;
    return level;
}

static void save_volume(DWORD volume)
{
    HKEY key;
    DWORD disposition;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, VOLUME_KEY_PATH, 0, NULL, 0, 0, NULL, &key, &disposition) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, VOLUME_VALUE, 0, REG_DWORD, (const BYTE *)&volume, sizeof(volume));
    RegCloseKey(key);
}

int keymap_step_volume(BOOL louder, int *top_level)
{
    int level = keymap_volume_level(top_level) + (louder ? 1 : -1);
    DWORD volume;

    if (level < 0)
        level = 0;
    if (level > *top_level)
        level = *top_level;
    volume = (DWORD)volume_levels[level] | ((DWORD)volume_levels[level] << 16);
    waveOutSetVolume(NULL, volume);
    save_volume(volume);
    keymap_log("volume", 0, level);
    MessageBeep(MB_OK);
    return level;
}

void keymap_toggle_help(void)
{
    PostMessageW(keymap_window, KEYMAP_TOGGLE_HELP, 0, 0);
}

static LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam)
{
    const ce_keyboard_hook *key = (const ce_keyboard_hook *)lparam;

    if (code == HC_ACTION) {
        BOOL up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
        UINT vkey = key->vkCode & 0xff;

        keymap_log(up ? "hook-up" : "hook-down", vkey, injected_pending[vkey]);
        if (injected_pending[vkey] > 0) {
            injected_pending[vkey]--;
            return CallNextHookEx(hook, code, wparam, lparam);
        }
        if (model->handle_key && model->handle_key(vkey, up))
            return 1;
        if (remap_handle(vkey, up, model->remapped))
            return 1;
    }
    return CallNextHookEx(hook, code, wparam, lparam);
}

static HICON create_text_icon(const WCHAR *text, BOOL active)
{
    HDC screen = GetDC(NULL);
    HDC context = CreateCompatibleDC(screen);
    HBITMAP colour = CreateCompatibleBitmap(screen, TRAY_ICON_SIZE, TRAY_ICON_SIZE);
    HBITMAP mask = CreateBitmap(TRAY_ICON_SIZE, TRAY_ICON_SIZE, 1, 1, NULL);
    HGDIOBJ previous = SelectObject(context, mask);
    RECT area = { 0, 0, TRAY_ICON_SIZE, TRAY_ICON_SIZE };
    ICONINFO info = { 0 };
    HICON icon;

    PatBlt(context, 0, 0, TRAY_ICON_SIZE, TRAY_ICON_SIZE, BLACKNESS);
    SelectObject(context, colour);
    FillRect(context, &area, GetSysColorBrush(active ? COLOR_HIGHLIGHT : COLOR_BTNFACE));
    DrawEdge(context, &area, BDR_SUNKENOUTER, BF_RECT);
    SetBkMode(context, TRANSPARENT);
    SetTextColor(context, GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT));
    DrawTextW(context, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, previous);
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = colour;
    icon = CreateIconIndirect(&info);
    DeleteObject(colour);
    DeleteObject(mask);
    DeleteDC(context);
    ReleaseDC(NULL, screen);
    return icon;
}

static HICON tray_icon_for(const WCHAR *label, BOOL active)
{
    int index;

    for (index = 0; index < tray_icon_count; index++)
        if (tray_icons[index].label == label && tray_icons[index].active == active)
            return tray_icons[index].icon;
    if (tray_icon_count == TRAY_ICON_LIMIT)
        return tray_icons[0].icon;
    tray_icons[tray_icon_count].label = label;
    tray_icons[tray_icon_count].active = active;
    tray_icons[tray_icon_count].icon = create_text_icon(label, active);
    return tray_icons[tray_icon_count++].icon;
}

static void update_tray(BOOL force)
{
    struct ce_tray_icon_data data = { 0 };
    BOOL active = FALSE;
    const WCHAR *label = model->layer_label ? model->layer_label(&active) : L"ab";
    HICON icon = tray_icon_for(label, active);

    if (!force && tray_added && icon == tray_shown_icon)
        return;
    data.size = sizeof(data);
    data.window = keymap_window;
    data.id = TRAY_ID;
    data.flags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.callback_message = KEYMAP_TRAY;
    data.icon = icon;
    lstrcpyW(data.tip, L"Keyboard: tap for keyboard help");
    if (tray_added)
        tray_added = Shell_NotifyIcon(NIM_MODIFY, (PNOTIFYICONDATAW)&data);
    if (!tray_added)
        tray_added = Shell_NotifyIcon(NIM_ADD, (PNOTIFYICONDATAW)&data);
    tray_shown_icon = tray_added ? icon : NULL;
    if (!tray_added)
        SetTimer(keymap_window, TRAY_TIMER, TRAY_RETRY_MS, NULL);
}

static void remove_tray(void)
{
    struct ce_tray_icon_data data = { 0 };

    if (!tray_added)
        return;
    data.size = sizeof(data);
    data.window = keymap_window;
    data.id = TRAY_ID;
    Shell_NotifyIcon(NIM_DELETE, (PNOTIFYICONDATAW)&data);
    tray_added = FALSE;
}

static LRESULT CALLBACK window_procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case KEYMAP_STATE_CHANGED:
        if (model->state_changed)
            model->state_changed();
        update_tray(FALSE);
        return 0;
    case KEYMAP_TOGGLE_HELP:
        help_toggle();
        return 0;
    case KEYMAP_TRAY:
        if (lparam == WM_LBUTTONUP)
            help_toggle();
        return 0;
    case KEYMAP_LAUNCH:
        remap_launch((int)wparam);
        return 0;
    case WM_TIMER:
        if (wparam == TRAY_TIMER) {
            KillTimer(handle, TRAY_TIMER);
            update_tray(TRUE);
        } else if (model->message) {
            model->message(message, wparam);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(handle);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (message >= KEYMAP_MODEL_MESSAGE && message < KEYMAP_MODEL_MESSAGE + KEYMAP_MODEL_MESSAGE_COUNT) {
        if (model->message)
            model->message(message, wparam);
        return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}

static const struct keymap_model *select_model(void)
{
    WCHAR profile[CONFIG_PROFILE_LENGTH];

    if (!config_profile(profile))
        return NULL;
    if (!wcscmp(profile, L"pwa7300") || !wcscmp(profile, L"pwg5300"))
        return &keymap_a7300;
    if (!wcscmp(profile, L"pwsh2") || !wcscmp(profile, L"pwsb1"))
        return &keymap_sh2;
    if (!wcscmp(profile, L"pwsh4"))
        return &keymap_sh4;
    return NULL;
}

static BOOL has_argument(const WCHAR *command_line, const WCHAR *argument)
{
    return command_line && wcsstr(command_line, argument) != NULL;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    HWND running = FindWindowW(WINDOW_CLASS, NULL);
    BOOL help = has_argument(command_line, L"help");
    WNDCLASSW window_class = { 0 };
    MSG message;

    if (running) {
        PostMessageW(running, help ? KEYMAP_TOGGLE_HELP : WM_CLOSE, 0, 0);
        return 0;
    }
    model = select_model();
    if (!model) {
        if (help)
            MessageBoxW(NULL, L"Keymap has no keyboard layout for this Brain.", L"Keyboard Help", MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    if (help) {
        remap_load(model->default_remaps);
        help_create(instance, model, TRUE);
        return help_run();
    }
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassW(&window_class))
        return 1;
    keymap_window = CreateWindowExW(0, WINDOW_CLASS, L"Keymap", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!keymap_window)
        return 1;
    if (has_argument(command_line, L"log"))
        log_file = CreateFileW(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    remap_load(model->default_remaps);
    help_create(instance, model, FALSE);
    hook = SetWindowsHookExW(WH_KEYBOARD_LL_CE, (HOOKPROC)keyboard_hook, instance, 0);
    if (!hook)
        return 1;
    if (model->start)
        model->start();
    update_tray(TRUE);
    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnhookWindowsHookEx(hook);
    remove_tray();
    if (model->stop)
        model->stop();
    return 0;
}
