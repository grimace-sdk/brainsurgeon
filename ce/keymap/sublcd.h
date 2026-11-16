#ifndef SUBLCD_H
#define SUBLCD_H

#include <windows.h>
#include <stdint.h>

#define SUBLCD_WIDTH 240
#define SUBLCD_HEIGHT 120
#define SUBLCD_STRIDE (SUBLCD_WIDTH / 4)
#define SUBLCD_BUFFER_SIZE (SUBLCD_STRIDE * SUBLCD_HEIGHT)

BOOL sublcd_open(void);
void sublcd_close(void);
BOOL sublcd_update(void);
uint8_t *sublcd_buffer(void);
BOOL sublcd_has_overlay(void);
BOOL sublcd_show_overlay(void);
BOOL sublcd_hide_overlay(void);

#endif
