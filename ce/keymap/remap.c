#include "keymap.h"
#include "../config.h"
#include <string.h>

#define REMAP_LIMIT 32
#define REMAP_FILE L"keys.txt"
#define REMAP_FILE_LIMIT 4096
#define CONTROL_PANEL L"\\Windows\\ctlpnl.exe"
#define BRAIN_CONTROL_ARGUMENTS L"\\Windows\\brain-control.cpl,0"
#define BRAIN_CONTROL_TITLE L"Brain Control"

enum remap_action { REMAP_KEY, REMAP_LAUNCH, REMAP_NONE, REMAP_HELP, REMAP_BRAIN_CONTROL, REMAP_VOLUME_UP, REMAP_VOLUME_DOWN };

struct remap {
    UINT vkey;
    enum remap_action action;
    UINT target;
    WCHAR path[MAX_PATH];
};

struct key_name {
    const WCHAR *name;
    UINT vkey;
};

static const struct key_name key_names[] = {
    { L"F1", VK_F1 }, { L"F2", VK_F2 }, { L"F3", VK_F3 }, { L"F4", VK_F4 }, { L"F5", VK_F5 }, { L"F6", VK_F6 },
    { L"F7", VK_F7 }, { L"F8", VK_F8 }, { L"F9", VK_F9 }, { L"F10", VK_F10 }, { L"F11", VK_F11 }, { L"F12", VK_F12 },
    { L"Back", VK_BACK }, { L"Return", VK_RETURN }, { L"Space", VK_SPACE }, { L"Tab", VK_TAB }, { L"Escape", VK_ESCAPE },
    { L"Left", VK_LEFT }, { L"Right", VK_RIGHT }, { L"Up", VK_UP }, { L"Down", VK_DOWN }, { L"Delete", VK_DELETE },
    { L"Home", VK_HOME }, { L"End", VK_END }, { L"PageUp", VK_PRIOR }, { L"PageDown", VK_NEXT }, { L"Windows", VK_LWIN },
    { L"LWin", VK_LWIN }, { L"Ctrl", VK_CONTROL }, { L"Control", VK_CONTROL }, { L"Alt", VK_MENU },
};

static struct remap remaps[REMAP_LIMIT];
static int remap_count;

static int find_remap(UINT vkey)
{
    int index;

    for (index = 0; index < remap_count; index++)
        if (remaps[index].vkey == vkey)
            return index;
    return -1;
}

static BOOL parse_key_name(const WCHAR *text, UINT *vkey)
{
    int index;

    if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        *vkey = (UINT)wcstoul(text + 2, NULL, 16);
        return *vkey != 0;
    }
    for (index = 0; index < (int)(sizeof(key_names) / sizeof(key_names[0])); index++) {
        if (!lstrcmpiW(text, key_names[index].name)) {
            *vkey = key_names[index].vkey;
            return TRUE;
        }
    }
    return FALSE;
}

static void parse_line(WCHAR *line)
{
    struct remap *remap = &remaps[remap_count];
    WCHAR *equals, *argument;
    int length;

    if (!line[0] || line[0] == L'#' || (equals = wcschr(line, L'=')) == NULL)
        return;
    *equals = 0;
    if (!parse_key_name(line, &remap->vkey)) {
        keymap_log("keys-unknown", 0, 0);
        return;
    }
    argument = wcschr(equals + 1, L' ');
    if (argument)
        *argument++ = 0;
    if (!lstrcmpiW(equals + 1, L"key") && argument && parse_key_name(argument, &remap->target)) {
        remap->action = REMAP_KEY;
    } else if (!lstrcmpiW(equals + 1, L"launch") && argument) {
        remap->action = REMAP_LAUNCH;
        for (length = 0; argument[length] && length < MAX_PATH - 1; length++)
            remap->path[length] = argument[length];
        remap->path[length] = 0;
    } else if (!lstrcmpiW(equals + 1, L"help")) {
        remap->action = REMAP_HELP;
    } else if (!lstrcmpiW(equals + 1, L"brain-control")) {
        remap->action = REMAP_BRAIN_CONTROL;
    } else if (!lstrcmpiW(equals + 1, L"volume-up")) {
        remap->action = REMAP_VOLUME_UP;
    } else if (!lstrcmpiW(equals + 1, L"volume-down")) {
        remap->action = REMAP_VOLUME_DOWN;
    } else if (!lstrcmpiW(equals + 1, L"none")) {
        remap->action = REMAP_NONE;
    } else {
        keymap_log("keys-bad-action", remap->vkey, 0);
        return;
    }
    keymap_log("keys-remap", remap->vkey, remap->target);
    remap_count++;
}

static void parse_text(const char *contents, DWORD length)
{
    DWORD start = 0, position;

    for (position = 0; position <= length && remap_count < REMAP_LIMIT; position++) {
        WCHAR line[2 * MAX_PATH];
        int line_length;

        if (position < length && contents[position] != '\n')
            continue;
        line_length = MultiByteToWideChar(CP_ACP, 0, contents + start, position - start, line, 2 * MAX_PATH - 1);
        start = position + 1;
        while (line_length > 0 && (line[line_length - 1] == L'\r' || line[line_length - 1] == L' '))
            line_length--;
        line[line_length < 0 ? 0 : line_length] = 0;
        parse_line(line);
    }
}

void remap_load(const char *defaults)
{
    WCHAR path[MAX_PATH];
    char contents[REMAP_FILE_LIMIT];
    DWORD length = 0;
    HANDLE file;

    remap_count = 0;
    config_settings_path(path, REMAP_FILE);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        ReadFile(file, contents, sizeof(contents), &length, NULL);
        CloseHandle(file);
        keymap_log("keys-file", 0, length);
        parse_text(contents, length);
    } else if (defaults) {
        parse_text(defaults, (DWORD)strlen(defaults));
    }
}

BOOL remap_handle(UINT vkey, BOOL up, void (*remapped)(UINT target, BOOL up))
{
    int index = find_remap(vkey);
    int top_level;

    if (index < 0)
        return FALSE;
    switch (remaps[index].action) {
    case REMAP_KEY:
        if (remapped)
            remapped(remaps[index].target, up);
        else
            keymap_inject(remaps[index].target, up);
        break;
    case REMAP_LAUNCH:
    case REMAP_BRAIN_CONTROL:
        if (!up)
            PostMessageW(keymap_window, KEYMAP_LAUNCH, (WPARAM)index, 0);
        break;
    case REMAP_HELP:
        if (!up)
            PostMessageW(keymap_window, KEYMAP_TOGGLE_HELP, 0, 0);
        break;
    case REMAP_VOLUME_UP:
    case REMAP_VOLUME_DOWN:
        if (!up)
            keymap_step_volume(remaps[index].action == REMAP_VOLUME_UP, &top_level);
        break;
    case REMAP_NONE:
        break;
    }
    return TRUE;
}

const WCHAR *remap_key_name(UINT vkey)
{
    int index;

    for (index = 0; index < (int)(sizeof(key_names) / sizeof(key_names[0])); index++)
        if (key_names[index].vkey == vkey)
            return key_names[index].name;
    return L"?";
}

BOOL remap_describe(UINT vkey, WCHAR *text, int length)
{
    int index = find_remap(vkey);
    const WCHAR *name;

    if (index < 0)
        return FALSE;
    switch (remaps[index].action) {
    case REMAP_KEY:
        name = remap_key_name(remaps[index].target);
        break;
    case REMAP_LAUNCH:
        name = remaps[index].path + lstrlenW(remaps[index].path);
        while (name > remaps[index].path && name[-1] != L'\\')
            name--;
        break;
    case REMAP_HELP:
        name = L"Key help";
        break;
    case REMAP_BRAIN_CONTROL:
        name = L"Settings";
        break;
    case REMAP_VOLUME_UP:
        name = L"Vol+";
        break;
    case REMAP_VOLUME_DOWN:
        name = L"Vol-";
        break;
    default:
        name = L"Off";
        break;
    }
    lstrcpynW(text, name, length);
    return TRUE;
}

static void open_brain_control(void)
{
    HWND window = FindWindowW(NULL, BRAIN_CONTROL_TITLE);
    WCHAR arguments[] = BRAIN_CONTROL_ARGUMENTS;
    PROCESS_INFORMATION process;

    if (window) {
        SetForegroundWindow(window);
        return;
    }
    if (CreateProcessW(CONTROL_PANEL, arguments, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        keymap_log("brain-control-failed", 0, GetLastError());
    }
}

void remap_launch(int index)
{
    PROCESS_INFORMATION process;

    if (index < 0 || index >= remap_count)
        return;
    if (remaps[index].action == REMAP_BRAIN_CONTROL) {
        open_brain_control();
        return;
    }
    if (remaps[index].action != REMAP_LAUNCH)
        return;
    if (CreateProcessW(remaps[index].path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        keymap_log("launch-failed", 0, GetLastError());
    }
}
