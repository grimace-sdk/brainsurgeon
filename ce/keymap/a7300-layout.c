#include "a7300-layout.h"
#include "draw.h"

#define KEY_WIDTH 24
#define KEY_HEIGHT 35
#define TOP 12
#define LEGEND_SCALE 2

static const struct layout_key keys[] = {
    { 'Q', 0, 0, "q", '1', '!' },
    { 'W', 0, 24, "w", '2', '"' },
    { 'E', 0, 48, "e", '3', 0xa3 },
    { 'R', 0, 72, "r", '4', '$' },
    { 'T', 0, 96, "t", '5', '%' },
    { 'Y', 0, 120, "y", '6', '^' },
    { 'U', 0, 144, "u", '7', '&' },
    { 'I', 0, 168, "i", '8', '*' },
    { 'O', 0, 192, "o", '9', '(' },
    { 'P', 0, 216, "p", '0', ')' },
    { 'A', 1, 12, "a", '(', '{' },
    { 'S', 1, 36, "s", ')', '}' },
    { 'D', 1, 60, "d", '[', '<' },
    { 'F', 1, 84, "f", ']', '>' },
    { 'G', 1, 108, "g", '+', '=' },
    { 'H', 1, 132, "h", '-', '_' },
    { 'J', 1, 156, "j", '*', '@' },
    { 'K', 1, 180, "k", '/', '\\' },
    { 'L', 1, 204, "l", '%', '#' },
    { VK_SHIFT_KEY, 2, 0, "", 0, 0, 0, 0, "Shft", "Shft" },
    { 'Z', 2, 24, "z", '~', '|' },
    { 'X', 2, 48, "x", '?', '`' },
    { 'C', 2, 72, "c", ',', 0x20ac },
    { 'V', 2, 96, "v", '.', 0xac },
    { 'B', 2, 120, "b", ';', ':' },
    { 'N', 2, 144, "n", '&', '!' },
    { 'M', 2, 168, "m", '\'', '@' },
    { VK_MINUS_KEY, 2, 192, "spc", '-', '_', VK_SPACE, 0, NULL, "spc" },
    { VK_BACK, 2, 216, "bk", 0, 0, 0, VK_DELETE, "Del", "bksp" },
    { VK_PRIOR, -1, 0, NULL, 0, 0, VK_CONTROL, VK_PRIOR },
    { VK_NEXT, -1, 0, NULL, 0, 0, VK_MENU, VK_NEXT },
    { VK_SLASH_KEY, -1, 0, NULL, 0, 0, VK_TAB, VK_TAB },
    { VK_RETURN, -1, 0, NULL, '=', '=' },
};

static const struct {
    UINT character;
    unsigned char columns[5];
} extra_glyphs[] = {
    { 0xa3, { 0x48, 0x7e, 0x49, 0x41, 0x42 } },
    { 0x20ac, { 0x14, 0x3e, 0x55, 0x55, 0x41 } },
    { 0xac, { 0x04, 0x04, 0x04, 0x04, 0x1c } },
};

const struct layout_key *layout_find(UINT vkey)
{
    size_t index;

    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
        if (keys[index].vkey == vkey)
            return &keys[index];
    return NULL;
}

static void draw_extra_glyph(int x, int y, UINT character, int scale, int colour)
{
    size_t index;
    int column, row;

    for (index = 0; index < sizeof(extra_glyphs) / sizeof(extra_glyphs[0]); index++) {
        if (extra_glyphs[index].character != character)
            continue;
        for (column = 0; column < 5; column++)
            for (row = 0; row < 7; row++)
                if (extra_glyphs[index].columns[column] & (1 << row))
                    sublcd_fill_rect(x + column * scale, y + row * scale, scale, scale, colour);
    }
}

static void draw_legend(int x, int y, UINT character, const char *text, int colour)
{
    char single[2] = { (char)character, 0 };
    int width;

    if (text) {
        width = sublcd_text_width(text, 1);
        sublcd_text(x + (KEY_WIDTH - width) / 2, y + 17, text, 1, colour);
        return;
    }
    if (!character)
        return;
    width = 5 * LEGEND_SCALE;
    if (character >= 0x80)
        draw_extra_glyph(x + (KEY_WIDTH - width) / 2, y + 12, character, LEGEND_SCALE, colour);
    else
        sublcd_text(x + (KEY_WIDTH - width) / 2, y + 12, single, LEGEND_SCALE, colour);
}

void layout_draw(int layer, BOOL active)
{
    int legend_colour = active ? SUBLCD_BLACK : SUBLCD_DARK;
    size_t index;

    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++) {
        const struct layout_key *key = &keys[index];
        int y = TOP + key->row * KEY_HEIGHT;

        if (key->row < 0)
            continue;
        sublcd_rect(key->x, y, KEY_WIDTH - 1, KEY_HEIGHT - 2, SUBLCD_LIGHT);
        if (layer == LAYER_BASE || layer == LAYER_SHIFT) {
            UINT letter = key->vkey >= 'A' && key->vkey <= 'Z' ? key->vkey + (layer == LAYER_SHIFT ? 0 : 'a' - 'A') : 0;

            draw_legend(key->x, y, letter, letter ? NULL : key->base_label, legend_colour);
            continue;
        }
        sublcd_text(key->x + 2, y + 2, key->label, 1, SUBLCD_DARK);
        draw_legend(key->x, y, layer == LAYER_SHIFT_FN && key->shift_fn ? key->shift_fn : key->fn, key->fn_label, legend_colour);
    }
}
