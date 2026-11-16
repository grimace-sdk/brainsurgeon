#include "draw.h"
#include <string.h>

void sublcd_clear(void)
{
    memset(sublcd_buffer(), 0, SUBLCD_BUFFER_SIZE);
}

void sublcd_pixel(int x, int y, int colour)
{
    uint8_t *byte;
    int shift;

    if (x < 0 || x >= SUBLCD_WIDTH || y < 0 || y >= SUBLCD_HEIGHT)
        return;
    byte = &sublcd_buffer()[y * SUBLCD_STRIDE + x / 4];
    shift = 6 - 2 * (x % 4);
    *byte = (uint8_t)((*byte & ~(3 << shift)) | ((colour & 3) << shift));
}

void sublcd_rect(int x, int y, int width, int height, int colour)
{
    sublcd_fill_rect(x, y, width, 1, colour);
    sublcd_fill_rect(x, y + height - 1, width, 1, colour);
    sublcd_fill_rect(x, y, 1, height, colour);
    sublcd_fill_rect(x + width - 1, y, 1, height, colour);
}

void sublcd_fill_rect(int x, int y, int width, int height, int colour)
{
    int row, column;

    for (row = y; row < y + height; row++)
        for (column = x; column < x + width; column++)
            sublcd_pixel(column, row, colour);
}

static void draw_glyph(int x, int y, char character, int scale, int colour)
{
    int glyph = character - SUBLCD_FONT_FIRST, column, row;

    if (glyph < 0 || glyph >= SUBLCD_FONT_GLYPHS)
        glyph = '?' - SUBLCD_FONT_FIRST;
    for (column = 0; column < SUBLCD_FONT_WIDTH; column++)
        for (row = 0; row < SUBLCD_FONT_HEIGHT; row++)
            if (sublcd_font[glyph][column] & (1 << row))
                sublcd_fill_rect(x + column * scale, y + row * scale, scale, scale, colour);
}

int sublcd_text(int x, int y, const char *text, int scale, int colour)
{
    for (; *text; text++) {
        draw_glyph(x, y, *text, scale, colour);
        x += SUBLCD_FONT_ADVANCE * scale;
    }
    return x;
}

int sublcd_text_width(const char *text, int scale)
{
    int length = (int)strlen(text);

    return length ? (length * SUBLCD_FONT_ADVANCE - 1) * scale : 0;
}
