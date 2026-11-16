#ifndef SUBLCD_DRAW_H
#define SUBLCD_DRAW_H

#include "sublcd.h"
#include "font.h"

#define SUBLCD_WHITE 0
#define SUBLCD_LIGHT 1
#define SUBLCD_DARK 2
#define SUBLCD_BLACK 3

void sublcd_clear(void);
void sublcd_pixel(int x, int y, int colour);
void sublcd_rect(int x, int y, int width, int height, int colour);
void sublcd_fill_rect(int x, int y, int width, int height, int colour);
int sublcd_text(int x, int y, const char *text, int scale, int colour);
int sublcd_text_width(const char *text, int scale);

#endif
