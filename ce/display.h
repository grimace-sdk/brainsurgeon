#ifndef PWSH2_BOOT_DISPLAY_H
#define PWSH2_BOOT_DISPLAY_H

#include <windows.h>

#define DISPLAY_SETTINGS_FILE L"display.txt"
#define AUTOROTATE_WINDOW_CLASS L"pwsh2-boot autorotate"
#define AUTOROTATE_APPLY_MESSAGE L"pwsh2-boot/ApplyRotation"

#define DISPLAY_BRIGHTNESS_LEVELS 5
#define DISPLAY_BRIGHTNESS_MINIMUM 1
#define DISPLAY_BRIGHTNESS_MAXIMUM 100

struct display_settings {
    DWORD startup_rotation;
    BOOL auto_rotate;
    DWORD tablet_rotation;
    DWORD brightness;
};

extern const DWORD display_brightness_levels[DISPLAY_BRIGHTNESS_LEVELS];

struct display_layout {
    RECT screen;
    RECT work_area;
};

typedef void (*display_log_function)(const char *line);

void display_set_log(display_log_function log);
void display_settings_read(struct display_settings *settings);
void display_layout_capture(struct display_layout *layout);
void display_reflow(const struct display_layout *before);
BOOL display_settings_write(const struct display_settings *settings);
BOOL display_rotate(DWORD degrees);
DWORD display_current_rotation(void);
BOOL display_set_brightness(DWORD level);
DWORD display_current_brightness(void);

#endif
