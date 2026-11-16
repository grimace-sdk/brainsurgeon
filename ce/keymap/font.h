#ifndef SUBLCD_FONT_H
#define SUBLCD_FONT_H

#include <stdint.h>

#define SUBLCD_FONT_FIRST ' '
#define SUBLCD_FONT_GLYPHS 95
#define SUBLCD_FONT_WIDTH 5
#define SUBLCD_FONT_HEIGHT 7
#define SUBLCD_FONT_ADVANCE (SUBLCD_FONT_WIDTH + 1)

extern const uint8_t sublcd_font[SUBLCD_FONT_GLYPHS][SUBLCD_FONT_WIDTH];

#endif
