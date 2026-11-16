#ifndef KEYMAP_LAYOUT_H
#define KEYMAP_LAYOUT_H

#include <windows.h>

#define VK_SHIFT_KEY 0xa0
#define VK_MINUS_KEY 0xbd
#define VK_SLASH_KEY 0xbf
#define VK_FN_KEY 0xdc

enum layer {
    LAYER_BASE,
    LAYER_SHIFT,
    LAYER_FN,
    LAYER_SHIFT_FN,
};

struct layout_key {
    UINT vkey;
    int row;
    int x;
    const char *label;
    UINT fn;
    UINT shift_fn;
    UINT base_vkey;
    UINT fn_vkey;
    const char *fn_label;
    const char *base_label;
};

const struct layout_key *layout_find(UINT vkey);

void layout_draw(int layer, BOOL active);

#endif
