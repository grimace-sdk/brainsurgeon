#ifndef SUBLCD_BACKLIGHT_H
#define SUBLCD_BACKLIGHT_H

#include <windows.h>

BOOL sublcd_set_backlight(BOOL on);
int sublcd_backlight_level(void);
BOOL sublcd_set_backlight_level(int percent);

#endif
