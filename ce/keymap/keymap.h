#ifndef KEYMAP_H
#define KEYMAP_H

#include <windows.h>

#define KEYMAP_STATE_CHANGED (WM_APP + 1)
#define KEYMAP_TOGGLE_HELP (WM_APP + 2)
#define KEYMAP_TRAY (WM_APP + 3)
#define KEYMAP_LAUNCH (WM_APP + 4)
#define KEYMAP_MODEL_MESSAGE (WM_APP + 16)
#define KEYMAP_MODEL_MESSAGE_COUNT 16

#define KEYMAP_KEY_STATE_DOWN 0x0080
#define KEYMAP_KEY_STATE_PREVIOUS_DOWN 0x0040
#define KEYMAP_LEGEND_LENGTH 16

enum help_ink { INK_REMAP, INK_LAYER, INK_SHIFTED, INK_NUMBER, INK_NAVIGATION, INK_COUNT };

enum help_style { STYLE_KEY, STYLE_TOP };

struct help_key {
    const WCHAR *label;
    UINT vkey;
    short left;
    short top;
    short right;
    short bottom;
    enum help_style style;
};

struct help_legends {
    WCHAR text[INK_COUNT][KEYMAP_LEGEND_LENGTH];
};

struct keymap_model {
    const WCHAR *name;
    const struct help_key *keys;
    int key_count;
    const WCHAR *const *notes;
    int note_count;
    const char *default_remaps;
    void (*start)(void);
    void (*stop)(void);
    BOOL (*handle_key)(UINT vkey, BOOL up);
    void (*remapped)(UINT target, BOOL up);
    void (*state_changed)(void);
    void (*describe)(const struct help_key *key, struct help_legends *legends);
    const WCHAR *(*layer_label)(BOOL *active);
    void (*message)(UINT message, WPARAM wparam);
};

extern const struct keymap_model keymap_a7300;
extern const struct keymap_model keymap_sh2;
extern const struct keymap_model keymap_sh4;

extern HWND keymap_window;

BOOL sh2_handle_key(UINT vkey, BOOL up);
void sh2_remapped(UINT target, BOOL up);
void sh2_describe(const struct help_key *key, struct help_legends *legends);
const WCHAR *sh2_layer_label(BOOL *active);

void keymap_log(const char *what, UINT vkey, UINT value);
void keymap_inject(UINT vkey, BOOL up);
void keymap_post_character(UINT character);
void keymap_notify(void);
void keymap_toggle_help(void);
int keymap_volume_level(int *top_level);
int keymap_step_volume(BOOL louder, int *top_level);
void keymap_set_legend(struct help_legends *legends, enum help_ink ink, const WCHAR *text);
void keymap_set_character(struct help_legends *legends, enum help_ink ink, UINT character);

void remap_load(const char *defaults);
BOOL remap_handle(UINT vkey, BOOL up, void (*remapped)(UINT target, BOOL up));
BOOL remap_describe(UINT vkey, WCHAR *text, int length);
const WCHAR *remap_key_name(UINT vkey);
void remap_launch(int index);

void help_create(HINSTANCE instance, const struct keymap_model *model, BOOL standalone);
void help_toggle(void);
int help_run(void);

#endif
