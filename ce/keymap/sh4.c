#include <windows.h>
#include "keymap.h"

#define VK_MINUS_KEY 0xbd

static const char default_remaps[] =
    "F10=key Windows\n"
    "F1=key Escape\n"
    "Escape=key Tab\n"
    "PageUp=key Ctrl\n"
    "PageDown=key Alt\n"
    "F8=brain-control\n"
    "F9=help\n";

static const struct help_key help_keys[] = {
    { L"Power", 0, 15, 265, 255, 415, STYLE_TOP },
    { L"Search", VK_F1, 268, 265, 430, 415, STYLE_TOP },
    { L"English", VK_F2, 440, 265, 600, 415, STYLE_TOP },
    { L"Kokugo", VK_F3, 612, 265, 775, 415, STYLE_TOP },
    { L"Eiwa", VK_F4, 785, 265, 948, 415, STYLE_TOP },
    { L"My Dict", VK_F7, 958, 265, 1122, 415, STYLE_TOP },
    { L"History", VK_F8, 1133, 265, 1300, 415, STYLE_TOP },
    { L"Tool", VK_F9, 1310, 265, 1480, 415, STYLE_TOP },
    { L"HOME", VK_F10, 1492, 265, 1755, 415, STYLE_TOP },
    { L"Q", 'Q', 18, 440, 180, 590 }, { L"W", 'W', 188, 440, 350, 590 }, { L"E", 'E', 360, 440, 520, 590 },
    { L"R", 'R', 530, 440, 692, 590 }, { L"T", 'T', 700, 440, 862, 590 }, { L"Y", 'Y', 872, 440, 1035, 590 },
    { L"U", 'U', 1045, 440, 1210, 590 }, { L"I", 'I', 1222, 440, 1388, 590 }, { L"O", 'O', 1398, 440, 1572, 590 },
    { L"P", 'P', 1582, 440, 1755, 590 },
    { L"A", 'A', 103, 600, 265, 755 }, { L"S", 'S', 275, 600, 437, 755 }, { L"D", 'D', 445, 600, 607, 755 },
    { L"F", 'F', 617, 600, 780, 755 }, { L"G", 'G', 788, 600, 950, 755 }, { L"H", 'H', 960, 600, 1122, 755 },
    { L"J", 'J', 1133, 600, 1298, 755 }, { L"K", 'K', 1310, 600, 1478, 755 }, { L"L", 'L', 1488, 600, 1660, 755 },
    { L"Shift", VK_CAPITAL, 20, 765, 183, 918 }, { L"Z", 'Z', 190, 765, 352, 918 }, { L"X", 'X', 360, 765, 522, 918 },
    { L"C", 'C', 532, 765, 695, 918 }, { L"V", 'V', 703, 765, 865, 918 }, { L"B", 'B', 875, 765, 1038, 918 },
    { L"N", 'N', 1047, 765, 1212, 918 }, { L"M", 'M', 1222, 765, 1388, 918 }, { L"-", VK_MINUS_KEY, 1400, 765, 1572, 918 },
    { L"Del", VK_BACK, 1582, 765, 1755, 918 },
    { L"Sound", VK_F11, 20, 928, 355, 1015 },
    { L"PgUp", VK_PRIOR, 362, 928, 610, 1050 }, { L"PgDn", VK_NEXT, 362, 1060, 610, 1185 },
    { L"Symbol", VK_F12, 618, 928, 782, 1050 }, { L"Space", VK_SPACE, 618, 1060, 782, 1185 },
    { L"Back", VK_ESCAPE, 790, 928, 952, 1185 },
    { L"<", VK_LEFT, 962, 928, 1125, 1185 }, { L"^", VK_UP, 1135, 928, 1303, 1052 },
    { L"v", VK_DOWN, 1135, 1062, 1303, 1185 }, { L">", VK_RIGHT, 1312, 928, 1482, 1190 },
    { L"Enter", VK_RETURN, 1492, 928, 1755, 1190 },
};

static const WCHAR *const notes[] = {
    L"Purple: hold Sound + key.   Blue: hold Symbol + key.   Red: Symbol + Shift + key.   Green: remapped.",
    L"Shift: tap for one key, hold for capitals.   PgUp/PgDn: Ctrl/Alt.   Symbols + Up/Down: volume.   Sound + arrows: PgUp, PgDn, Home, End.",
    L"Toggle this map with the Tool key, the tray icon or Start > Programs > Keyboard Help.   History opens Brain Control.",
};

const struct keymap_model keymap_sh4 = {
    L"PW-SH4",
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
