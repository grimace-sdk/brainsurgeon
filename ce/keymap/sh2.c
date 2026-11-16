#include <windows.h>
#include "keymap.h"

#define VK_SYMBOL VK_F12
#define VK_NUMBERS VK_F11
#define VK_SHARP_SHIFT VK_CAPITAL
#define VK_MINUS_KEY 0xbd

struct symbol_key {
    UINT vkey;
    WCHAR plain;
    WCHAR shifted;
};

struct navigation_key {
    UINT vkey;
    UINT target;
    const WCHAR *label;
};

static const struct symbol_key symbol_keys[] = {
    { 'Q', L'!', L'1' }, { 'W', L'@', L'2' }, { 'E', L'#', L'3' }, { 'R', L'$', L'4' }, { 'T', L'%', L'5' },
    { 'Y', L'^', L'6' }, { 'U', L'&', L'7' }, { 'I', L'*', L'8' }, { 'O', L'(', L'9' }, { 'P', L')', L'0' },
    { 'A', L'~', L'~' }, { 'S', L'+', L'+' }, { 'D', L'`', L'~' }, { 'F', L'=', L'+' }, { 'G', L'\\', L'|' },
    { 'H', L';', L':' }, { 'J', L'\'', L'"' }, { 'K', L'[', L'{' }, { 'L', L']', L'}' },
    { 'Z', L'<', L'<' }, { 'X', L'>', L'>' }, { 'C', L':', L':' }, { 'V', L'"', L'"' }, { 'B', L'?', L'?' },
    { 'N', L',', L'<' }, { 'M', L'.', L'>' }, { VK_MINUS_KEY, L'/', L'?' },
};

static const struct symbol_key number_keys[] = {
    { 'Q', L'1', L'1' }, { 'W', L'2', L'2' }, { 'E', L'3', L'3' }, { 'R', L'4', L'4' }, { 'T', L'5', L'5' },
    { 'Y', L'6', L'6' }, { 'U', L'7', L'7' }, { 'I', L'8', L'8' }, { 'O', L'9', L'9' }, { 'P', L'0', L'0' },
    { 'N', L',', L',' }, { 'M', L'.', L'.' }, { VK_MINUS_KEY, L'-', L'-' },
};

static const struct navigation_key number_navigation[] = {
    { VK_UP, VK_PRIOR, L"PgUp" }, { VK_DOWN, VK_NEXT, L"PgDn" }, { VK_LEFT, VK_HOME, L"Home" }, { VK_RIGHT, VK_END, L"End" },
};

static const char default_remaps[] =
    "F10=key Back\n"
    "Back=key Return\n"
    "Return=key Space\n"
    "Space=key Windows\n"
    "PageUp=key Ctrl\n"
    "PageDown=key Alt\n"
    "F1=key Escape\n"
    "Escape=key Tab\n"
    "F8=brain-control\n"
    "F9=help\n";

static BOOL symbol_held;
static BOOL number_held;
static BOOL shift_held;
static BOOL shift_used;
static BOOL shift_latched;
static BOOL chord_modifier_held;
static BOOL tab_held;
static BOOL real_shift_injected;

static const struct symbol_key *find_layer_key(const struct symbol_key *table, int count, UINT vkey)
{
    int index;

    for (index = 0; index < count; index++)
        if (table[index].vkey == vkey)
            return &table[index];
    return NULL;
}

static const struct symbol_key *find_symbol_key(UINT vkey)
{
    return find_layer_key(symbol_keys, sizeof(symbol_keys) / sizeof(symbol_keys[0]), vkey);
}

static const struct symbol_key *find_number_key(UINT vkey)
{
    return find_layer_key(number_keys, sizeof(number_keys) / sizeof(number_keys[0]), vkey);
}

static const struct navigation_key *find_number_navigation(UINT vkey)
{
    int index;

    for (index = 0; index < (int)(sizeof(number_navigation) / sizeof(number_navigation[0])); index++)
        if (number_navigation[index].vkey == vkey)
            return &number_navigation[index];
    return NULL;
}

static void sync_real_shift(void)
{
    BOOL wanted = shift_held && (chord_modifier_held || tab_held);

    if (wanted != real_shift_injected) {
        keymap_inject(VK_SHIFT, !wanted);
        real_shift_injected = wanted;
    }
}

static void clear_latch(void)
{
    if (!shift_latched)
        return;
    shift_latched = FALSE;
    keymap_notify();
}

BOOL sh2_handle_key(UINT vkey, BOOL up)
{
    BOOL down = !up;
    const struct symbol_key *symbol;
    const struct navigation_key *navigation;
    int top_level;

    if (vkey == VK_SYMBOL || vkey == VK_NUMBERS) {
        if (vkey == VK_SYMBOL)
            symbol_held = down;
        else
            number_held = down;
        keymap_notify();
        return TRUE;
    }
    if (vkey == VK_SHARP_SHIFT) {
        if (down && !shift_held)
            shift_used = FALSE;
        if (!down && !shift_used)
            shift_latched = TRUE;
        shift_held = down;
        sync_real_shift();
        keymap_notify();
        return FALSE;
    }
    if (down)
        shift_used = TRUE;
    if (number_held && (symbol = find_number_key(vkey)) != NULL) {
        if (down)
            keymap_post_character(symbol->plain);
        return TRUE;
    }
    if (number_held && (symbol = find_symbol_key(vkey)) != NULL) {
        if (down)
            keymap_post_character(symbol->shifted);
        return TRUE;
    }
    if (symbol_held && !number_held && (vkey == VK_UP || vkey == VK_DOWN)) {
        if (down)
            keymap_step_volume(vkey == VK_UP, &top_level);
        return TRUE;
    }
    if ((number_held || symbol_held) && (navigation = find_number_navigation(vkey)) != NULL) {
        keymap_inject(navigation->target, up);
        return TRUE;
    }
    if (symbol_held && (symbol = find_symbol_key(vkey)) != NULL) {
        if (down) {
            keymap_post_character(shift_held || shift_latched ? symbol->shifted : symbol->plain);
            clear_latch();
        }
        return TRUE;
    }
    if (down)
        clear_latch();
    return FALSE;
}

void sh2_remapped(UINT target, BOOL up)
{
    if (target == VK_TAB && !up) {
        tab_held = TRUE;
        sync_real_shift();
    }
    keymap_inject(target, up);
    if (target == VK_TAB && up) {
        tab_held = FALSE;
        sync_real_shift();
    }
    if (target == VK_CONTROL || target == VK_MENU) {
        chord_modifier_held = !up;
        sync_real_shift();
    }
}

const WCHAR *sh2_layer_label(BOOL *active)
{
    *active = TRUE;
    if (number_held)
        return L"12";
    if (symbol_held)
        return L"#!";
    if (shift_held)
        return L"AB";
    if (shift_latched)
        return L"Ab";
    *active = FALSE;
    return L"ab";
}

void sh2_describe(const struct help_key *key, struct help_legends *legends)
{
    const struct symbol_key *symbol = find_symbol_key(key->vkey);
    const struct symbol_key *number = find_number_key(key->vkey);
    const struct navigation_key *navigation = find_number_navigation(key->vkey);

    if (navigation)
        keymap_set_legend(legends, INK_NAVIGATION, navigation->label);
    if (number && key->vkey != VK_MINUS_KEY && key->vkey != 'N' && key->vkey != 'M')
        keymap_set_character(legends, INK_NUMBER, number->plain);
    if (symbol) {
        keymap_set_character(legends, INK_LAYER, symbol->plain);
        keymap_set_character(legends, INK_SHIFTED, symbol->shifted);
    }
}

static const struct help_key help_keys[] = {
    { L"Power", 0, 42, 225, 208, 382, STYLE_TOP },
    { L"Search", VK_F1, 230, 225, 395, 382, STYLE_TOP },
    { L"Kokugo", VK_F2, 402, 225, 565, 382, STYLE_TOP },
    { L"Eiwa", VK_F3, 570, 225, 733, 382, STYLE_TOP },
    { L"My Dict", VK_F4, 738, 225, 900, 382, STYLE_TOP },
    { L"History", VK_F7, 905, 225, 1064, 382, STYLE_TOP },
    { L"Marker", VK_F8, 1068, 225, 1230, 382, STYLE_TOP },
    { L"Tool", VK_F9, 1236, 225, 1398, 382, STYLE_TOP },
    { L"HOME", VK_F10, 1402, 225, 1565, 382, STYLE_TOP },
    { L"Q", 'Q', 85, 440, 205, 530 }, { L"W", 'W', 233, 440, 353, 530 }, { L"E", 'E', 383, 440, 503, 530 },
    { L"R", 'R', 532, 440, 652, 530 }, { L"T", 'T', 680, 440, 800, 530 }, { L"Y", 'Y', 828, 440, 948, 530 },
    { L"U", 'U', 972, 440, 1092, 530 }, { L"I", 'I', 1117, 440, 1237, 530 }, { L"O", 'O', 1260, 440, 1380, 530 },
    { L"P", 'P', 1405, 440, 1522, 530 },
    { L"A", 'A', 160, 563, 278, 655 }, { L"S", 'S', 310, 563, 428, 655 }, { L"D", 'D', 458, 563, 576, 655 },
    { L"F", 'F', 606, 563, 724, 655 }, { L"G", 'G', 752, 563, 870, 655 }, { L"H", 'H', 898, 563, 1016, 655 },
    { L"J", 'J', 1043, 563, 1161, 655 }, { L"K", 'K', 1190, 563, 1308, 655 }, { L"L", 'L', 1335, 563, 1450, 655 },
    { L"Shift", VK_SHARP_SHIFT, 82, 687, 205, 780 },
    { L"Z", 'Z', 233, 687, 352, 780 }, { L"X", 'X', 383, 687, 502, 780 }, { L"C", 'C', 532, 687, 651, 780 },
    { L"V", 'V', 680, 687, 799, 780 }, { L"B", 'B', 828, 687, 947, 780 }, { L"N", 'N', 972, 687, 1091, 780 },
    { L"M", 'M', 1117, 687, 1236, 780 }, { L"-", VK_MINUS_KEY, 1262, 687, 1378, 780 },
    { L"Del", VK_BACK, 1438, 690, 1525, 785 },
    { L"PgUp", VK_PRIOR, 80, 838, 205, 915 }, { L"PgDn", VK_NEXT, 80, 945, 205, 1020 },
    { L"Numbers", VK_NUMBERS, 232, 838, 352, 915 }, { L"Symbol", VK_SYMBOL, 232, 942, 352, 1018 },
    { L"Space", VK_SPACE, 430, 835, 550, 955 },
    { L"Back", VK_ESCAPE, 578, 838, 733, 955 }, { L"Enter", VK_RETURN, 737, 838, 1040, 955 },
    { L"<", VK_LEFT, 1118, 838, 1200, 1018 }, { L"^", VK_UP, 1230, 838, 1410, 915 },
    { L"v", VK_DOWN, 1230, 942, 1410, 1018 }, { L">", VK_RIGHT, 1440, 838, 1525, 1018 },
};

static const WCHAR *const notes[] = {
    L"Purple: hold Numbers + key.   Blue: hold Symbol + key.   Red: Symbol + Shift + key.   Green: remapped.",
    L"Shift: tap for one key, hold for capitals.   Symbol + Up/Down: volume.",
    L"Toggle this map with the Tool key, the tray icon or Start > Programs > Keyboard Help.   Marker opens Brain Control.",
};

const struct keymap_model keymap_sh2 = {
    L"PW-SH2",
    help_keys,
    sizeof(help_keys) / sizeof(help_keys[0]),
    notes,
    sizeof(notes) / sizeof(notes[0]),
    default_remaps,
    NULL,
    NULL,
    sh2_handle_key,
    sh2_remapped,
    NULL,
    sh2_describe,
    sh2_layer_label,
    NULL,
};
