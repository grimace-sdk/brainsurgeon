#include <windows.h>
#include <string.h>
#include "keymap.h"
#include "sublcd.h"
#include "draw.h"
#include "backlight.h"
#include "a7300-layout.h"
#include "../display.h"

#define ADJUST_STEP KEYMAP_MODEL_MESSAGE
#define PAGE_STEP (KEYMAP_MODEL_MESSAGE + 1)
#define CLAIM_TIMER 1
#define ADJUST_TIMER 2
#define ADJUST_SHOW_MS 2500
#define ADJUST_MARGIN 12
#define ADJUST_BAR_TOP 50
#define ADJUST_BAR_HEIGHT 30
#define ADJUST_HINT_TOP 96
#define ADJUST_PAGE_GAP 8
#define SUB_BRIGHTNESS_STEP 10
#define SUB_BRIGHTNESS_MAXIMUM 100
#define CLAIM_POLL_MS 500
#define TOUCHPAD_DEVICE L"PAD1:"
#define PAD_IOCTL_GET_INFO 0x100

enum action_kind { ACTION_NONE, ACTION_PASS, ACTION_SHIFTED, ACTION_VKEY, ACTION_CHARACTER, ACTION_MODIFIER, ACTION_ADJUST, ACTION_PAGE };

enum adjust_page { PAGE_VOLUME, PAGE_BRIGHTNESS, PAGE_SUB_BRIGHTNESS, PAGE_COUNT };

struct action {
    enum action_kind kind;
    UINT value;
};

struct modifier {
    BOOL held;
    BOOL latched;
    BOOL used;
};

struct pad_info {
    DWORD magic;
    DWORD version;
    DWORD claims;
    DWORD active;
};

static struct modifier shift;
static struct modifier fn;
static struct action actions[256];
static BOOL control_held;
static BOOL alt_held;
static BOOL display_open;
static BOOL display_paused;
static BOOL overlay_showing;
static int drawn_state = -1;
static volatile BOOL adjust_showing;
static enum adjust_page adjust_page;
static BOOL brightness_changed;

static const char *const page_titles[PAGE_COUNT] = { "VOLUME", "LCD", "SUB LCD" };
static const char *const page_tabs[PAGE_COUNT] = { "VOL", "LCD", "SUB" };

static BOOL shift_active(void)
{
    return shift.held || shift.latched;
}

static BOOL fn_active(void)
{
    return fn.held || fn.latched;
}

static BOOL is_shift_key(UINT vkey)
{
    return vkey == VK_SHIFT_KEY || vkey == VK_SHIFT;
}

static void modifier_down(struct modifier *modifier)
{
    if (!modifier->held) {
        modifier->held = TRUE;
        modifier->used = FALSE;
    }
}

static void modifier_up(struct modifier *modifier)
{
    modifier->held = FALSE;
    if (!modifier->used)
        modifier->latched = !modifier->latched;
}

static void use_modifiers(void)
{
    if (shift.held)
        shift.used = TRUE;
    if (fn.held)
        fn.used = TRUE;
}

static void clear_latches(void)
{
    if (!shift.latched && !fn.latched)
        return;
    shift.latched = FALSE;
    fn.latched = FALSE;
    keymap_notify();
}

static struct action choose_action(UINT vkey)
{
    const struct layout_key *key = layout_find(vkey);
    struct action action = { ACTION_PASS, 0 };

    if (vkey == VK_FN_KEY || is_shift_key(vkey)) {
        action.kind = ACTION_MODIFIER;
        return action;
    }
    if (fn_active() && (vkey == VK_LEFT || vkey == VK_RIGHT)) {
        action.kind = ACTION_ADJUST;
        action.value = vkey == VK_RIGHT;
        return action;
    }
    if (fn_active() && (vkey == VK_UP || vkey == VK_DOWN)) {
        action.kind = ACTION_PAGE;
        action.value = vkey == VK_DOWN;
        return action;
    }
    if (fn_active() && key) {
        UINT character = shift_active() && key->shift_fn ? key->shift_fn : key->fn;

        if (key->fn_vkey) {
            action.kind = ACTION_VKEY;
            action.value = key->fn_vkey;
        } else if (character) {
            action.kind = ACTION_CHARACTER;
            action.value = character;
        }
        return action;
    }
    if (key && key->base_vkey) {
        action.kind = ACTION_VKEY;
        action.value = key->base_vkey;
        return action;
    }
    if (shift.latched && !shift.held) {
        action.kind = ACTION_SHIFTED;
        action.value = vkey;
    }
    return action;
}

static void track_control_and_alt(UINT vkey, BOOL up)
{
    BOOL *held;

    if (vkey != VK_CONTROL && vkey != VK_MENU)
        return;
    held = vkey == VK_CONTROL ? &control_held : &alt_held;
    if (*held == up) {
        *held = !up;
        keymap_notify();
    }
}

static BOOL handle_key(UINT vkey, BOOL up)
{
    struct action *action = &actions[vkey & 0xff];

    if (!up && action->kind == ACTION_NONE)
        *action = choose_action(vkey);

    switch (action->kind) {
    case ACTION_MODIFIER:
        if (up) {
            modifier_up(is_shift_key(vkey) ? &shift : &fn);
            action->kind = ACTION_NONE;
        } else {
            modifier_down(is_shift_key(vkey) ? &shift : &fn);
        }
        keymap_notify();
        return vkey == VK_FN_KEY;
    case ACTION_SHIFTED:
        if (!up) {
            keymap_inject(VK_SHIFT, FALSE);
            keymap_inject(action->value, FALSE);
        } else {
            keymap_inject(action->value, TRUE);
            keymap_inject(VK_SHIFT, TRUE);
            action->kind = ACTION_NONE;
            clear_latches();
        }
        return TRUE;
    case ACTION_VKEY:
        if (!up)
            use_modifiers();
        keymap_inject(action->value, up);
        track_control_and_alt(action->value, up);
        if (up) {
            action->kind = ACTION_NONE;
            if (action->value != VK_CONTROL && action->value != VK_MENU)
                clear_latches();
        }
        return TRUE;
    case ACTION_CHARACTER:
        if (!up) {
            use_modifiers();
            keymap_post_character(action->value);
        } else {
            action->kind = ACTION_NONE;
            clear_latches();
        }
        return TRUE;
    case ACTION_ADJUST:
    case ACTION_PAGE:
        if (!up) {
            use_modifiers();
            PostMessageW(keymap_window, action->kind == ACTION_ADJUST ? ADJUST_STEP : PAGE_STEP, action->value, 0);
        } else {
            action->kind = ACTION_NONE;
            clear_latches();
        }
        return TRUE;
    case ACTION_PASS:
        if (!up)
            use_modifiers();
        else {
            action->kind = ACTION_NONE;
            clear_latches();
        }
        return FALSE;
    case ACTION_NONE:
        return FALSE;
    }
    return FALSE;
}

static BOOL panel_claimed(void)
{
    HANDLE device = CreateFileW(TOUCHPAD_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    struct pad_info info = { 0 };
    DWORD returned = 0;

    if (device == INVALID_HANDLE_VALUE)
        return FALSE;
    DeviceIoControl(device, PAD_IOCTL_GET_INFO, NULL, 0, &info, sizeof(info), &returned, NULL);
    CloseHandle(device);
    return info.claims != 0;
}

static void draw_layer(int layer, BOOL active)
{
    int x = 2;

    sublcd_clear();
    layout_draw(layer, active);
    if (!active && !control_held && !alt_held)
        sublcd_text(2, 2, "Fn speaker  Shift func  Ctrl ^  Alt v", 1, SUBLCD_DARK);
    if (shift_active())
        x = sublcd_text(x, 2, "SHIFT ", 1, SUBLCD_BLACK);
    if (fn_active())
        x = sublcd_text(x, 2, "FN ", 1, SUBLCD_BLACK);
    if (fn_active() && !control_held && !alt_held)
        x = sublcd_text(x, 2, "<>^v adjust ", 1, SUBLCD_DARK);
    if (control_held)
        x = sublcd_text(x, 2, "CTRL ", 1, SUBLCD_BLACK);
    if (alt_held)
        sublcd_text(x, 2, "ALT", 1, SUBLCD_BLACK);
    sublcd_update();
}

static void refresh_display(BOOL force)
{
    int layer = (fn_active() ? LAYER_FN : LAYER_BASE) + (shift_active() ? 1 : 0);
    int state = layer * 4 + (control_held ? 2 : 0) + (alt_held ? 1 : 0);

    if (display_paused || !display_open || adjust_showing)
        return;
    if (!force && state == drawn_state)
        return;
    drawn_state = state;
    if (state != LAYER_BASE * 4)
        sublcd_set_backlight(TRUE);
    draw_layer(layer, layer != LAYER_BASE);
}

static int brightness_position(void)
{
    DWORD level = display_current_brightness();
    int index;

    for (index = DISPLAY_BRIGHTNESS_LEVELS - 1; index > 0; index--)
        if (level >= display_brightness_levels[index])
            break;
    return index;
}

static int step_brightness(BOOL brighter, int *top_level)
{
    int position = brightness_position() + (brighter ? 1 : -1);

    *top_level = DISPLAY_BRIGHTNESS_LEVELS - 1;
    if (position < 0)
        position = 0;
    if (position > *top_level)
        position = *top_level;
    display_set_brightness(display_brightness_levels[position]);
    brightness_changed = TRUE;
    keymap_log("brightness", 0, display_brightness_levels[position]);
    return position;
}

static int step_sub_brightness(BOOL brighter, int *top_level)
{
    int level = sublcd_backlight_level();

    *top_level = SUB_BRIGHTNESS_MAXIMUM;
    if (!level)
        return 0;
    if (brighter)
        level = (level / SUB_BRIGHTNESS_STEP + 1) * SUB_BRIGHTNESS_STEP;
    else
        level = (level - 1) / SUB_BRIGHTNESS_STEP * SUB_BRIGHTNESS_STEP;
    if (level < 1)
        level = 1;
    if (level > SUB_BRIGHTNESS_MAXIMUM)
        level = SUB_BRIGHTNESS_MAXIMUM;
    sublcd_set_backlight_level(level);
    keymap_log("sub-brightness", 0, level);
    return level;
}

static int page_level(enum adjust_page page, int *top_level)
{
    switch (page) {
    case PAGE_BRIGHTNESS:
        *top_level = DISPLAY_BRIGHTNESS_LEVELS - 1;
        return brightness_position();
    case PAGE_SUB_BRIGHTNESS:
        *top_level = SUB_BRIGHTNESS_MAXIMUM;
        return sublcd_backlight_level();
    default:
        return keymap_volume_level(top_level);
    }
}

static int number_text(int value, char *text)
{
    int length = 0;

    if (value >= 100)
        text[length++] = (char)('0' + value / 100);
    if (value >= 10)
        text[length++] = (char)('0' + value / 10 % 10);
    text[length++] = (char)('0' + value % 10);
    text[length] = 0;
    return length;
}

static void level_text(enum adjust_page page, int level, char *text)
{
    int value = page == PAGE_BRIGHTNESS ? (int)display_brightness_levels[level] : level;

    if (page == PAGE_VOLUME && !level) {
        memcpy(text, "MUTE", 5);
        return;
    }
    if (page == PAGE_SUB_BRIGHTNESS && !level) {
        memcpy(text, "--", 3);
        return;
    }
    number_text(value, text);
}

static void draw_adjust(int level, int top_level)
{
    int bar_width = SUBLCD_WIDTH - 2 * ADJUST_MARGIN;
    int filled = top_level ? (bar_width - 4) * level / top_level : 0;
    char text[5];
    int x, page;

    level_text(adjust_page, level, text);
    sublcd_clear();
    sublcd_text(ADJUST_MARGIN, 16, page_titles[adjust_page], 2, SUBLCD_BLACK);
    sublcd_text(SUBLCD_WIDTH - ADJUST_MARGIN - sublcd_text_width(text, 2), 16, text, 2, SUBLCD_BLACK);
    sublcd_rect(ADJUST_MARGIN, ADJUST_BAR_TOP, bar_width, ADJUST_BAR_HEIGHT, SUBLCD_BLACK);
    sublcd_fill_rect(ADJUST_MARGIN + 2, ADJUST_BAR_TOP + 2, filled, ADJUST_BAR_HEIGHT - 4, SUBLCD_BLACK);
    x = sublcd_text(ADJUST_MARGIN, ADJUST_HINT_TOP, "</> adjust  ^v ", 1, SUBLCD_DARK);
    for (page = 0; page < PAGE_COUNT; page++)
        x = sublcd_text(x, ADJUST_HINT_TOP, page_tabs[page], 1, page == adjust_page ? SUBLCD_BLACK : SUBLCD_DARK) + ADJUST_PAGE_GAP;
}

static void close_overlay(void)
{
    if (!display_paused || !display_open)
        return;
    if (overlay_showing)
        sublcd_hide_overlay();
    overlay_showing = FALSE;
    sublcd_close();
    display_open = FALSE;
}

static void show_adjust(int level, int top_level)
{
    adjust_showing = TRUE;
    SetTimer(keymap_window, ADJUST_TIMER, ADJUST_SHOW_MS, NULL);
    if (display_paused && !display_open)
        display_open = sublcd_open();
    if (!display_open)
        return;
    if (display_paused && !sublcd_has_overlay()) {
        close_overlay();
        return;
    }
    sublcd_set_backlight(TRUE);
    draw_adjust(level, top_level);
    if (display_paused)
        overlay_showing = sublcd_show_overlay();
    else
        sublcd_update();
}

static void step_adjust(BOOL up)
{
    int top_level, level;

    if (!adjust_showing)
        adjust_page = PAGE_VOLUME;
    if (adjust_page == PAGE_BRIGHTNESS)
        level = step_brightness(up, &top_level);
    else if (adjust_page == PAGE_SUB_BRIGHTNESS)
        level = step_sub_brightness(up, &top_level);
    else
        level = keymap_step_volume(up, &top_level);
    show_adjust(level, top_level);
}

static void step_page(BOOL next)
{
    int top_level, level;

    if (!adjust_showing)
        adjust_page = PAGE_VOLUME;
    else
        adjust_page = (enum adjust_page)((adjust_page + (next ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT);
    level = page_level(adjust_page, &top_level);
    show_adjust(level, top_level);
}

static void save_brightness(void)
{
    struct display_settings settings;

    brightness_changed = FALSE;
    display_settings_read(&settings);
    settings.brightness = display_brightness_levels[brightness_position()];
    display_settings_write(&settings);
}

static void end_adjust(void)
{
    KillTimer(keymap_window, ADJUST_TIMER);
    adjust_showing = FALSE;
    close_overlay();
    if (brightness_changed)
        save_brightness();
    refresh_display(TRUE);
}

static void check_claims(void)
{
    BOOL claimed = panel_claimed();

    if (claimed && !display_paused) {
        display_paused = TRUE;
        if (display_open)
            sublcd_close();
        display_open = FALSE;
    } else if (!claimed && display_paused) {
        if (adjust_showing)
            end_adjust();
        display_paused = FALSE;
        display_open = sublcd_open();
        refresh_display(TRUE);
    }
}

static void start(void)
{
    display_paused = panel_claimed();
    if (!display_paused)
        display_open = sublcd_open();
    refresh_display(TRUE);
    SetTimer(keymap_window, CLAIM_TIMER, CLAIM_POLL_MS, NULL);
}

static void stop(void)
{
    close_overlay();
    if (display_open) {
        sublcd_clear();
        sublcd_update();
        sublcd_close();
    }
}

static void state_changed(void)
{
    refresh_display(FALSE);
}

static void model_message(UINT message, WPARAM wparam)
{
    if (message == ADJUST_STEP)
        step_adjust(wparam != 0);
    else if (message == PAGE_STEP)
        step_page(wparam != 0);
    else if (message == WM_TIMER && wparam == ADJUST_TIMER)
        end_adjust();
    else if (message == WM_TIMER && wparam == CLAIM_TIMER)
        check_claims();
}

static const WCHAR *layer_label(BOOL *active)
{
    *active = fn_active() || shift_active();
    if (fn_active())
        return L"Fn";
    if (shift.held)
        return L"AB";
    if (shift.latched)
        return L"Ab";
    return L"ab";
}

static const WCHAR *short_key_name(UINT vkey)
{
    switch (vkey) {
    case VK_SPACE:
        return L"Space";
    case VK_CONTROL:
        return L"Ctrl";
    case VK_MENU:
        return L"Alt";
    case VK_TAB:
        return L"Tab";
    case VK_DELETE:
        return L"Del";
    case VK_PRIOR:
        return L"PgUp";
    case VK_NEXT:
        return L"PgDn";
    }
    return remap_key_name(vkey);
}

static void describe(const struct help_key *help, struct help_legends *legends)
{
    const struct layout_key *key = layout_find(help->vkey);

    switch (help->vkey) {
    case VK_SHIFT_KEY:
        keymap_set_legend(legends, INK_REMAP, L"Shift");
        return;
    case VK_FN_KEY:
        keymap_set_legend(legends, INK_REMAP, L"Fn");
        return;
    case VK_LEFT:
        keymap_set_legend(legends, INK_LAYER, L"Vol-");
        return;
    case VK_RIGHT:
        keymap_set_legend(legends, INK_LAYER, L"Vol+");
        return;
    case VK_UP:
    case VK_DOWN:
        keymap_set_legend(legends, INK_LAYER, L"Page");
        return;
    }
    if (!key)
        return;
    if (key->base_vkey)
        keymap_set_legend(legends, INK_REMAP, short_key_name(key->base_vkey));
    if (key->fn_vkey)
        keymap_set_legend(legends, INK_LAYER, short_key_name(key->fn_vkey));
    else if (key->fn)
        keymap_set_character(legends, INK_LAYER, key->fn);
    if (key->shift_fn && key->shift_fn != key->fn)
        keymap_set_character(legends, INK_SHIFTED, key->shift_fn);
}

static const struct help_key help_keys[] = {
    { L"F1", VK_F1, 0, -48, 26, -12, STYLE_TOP }, { L"F2", VK_F2, 27, -48, 53, -12, STYLE_TOP },
    { L"F3", VK_F3, 54, -48, 80, -12, STYLE_TOP }, { L"F4", VK_F4, 81, -48, 107, -12, STYLE_TOP },
    { L"F5", VK_F5, 108, -48, 134, -12, STYLE_TOP }, { L"F6", VK_F6, 135, -48, 161, -12, STYLE_TOP },
    { L"F7", VK_F7, 162, -48, 188, -12, STYLE_TOP }, { L"F10", VK_F10, 189, -48, 215, -12, STYLE_TOP },
    { L"Home", VK_HOME, 216, -48, 240, -12, STYLE_TOP },
    { L"Q", 'Q', 0, 0, 24, 35 }, { L"W", 'W', 24, 0, 48, 35 }, { L"E", 'E', 48, 0, 72, 35 }, { L"R", 'R', 72, 0, 96, 35 },
    { L"T", 'T', 96, 0, 120, 35 }, { L"Y", 'Y', 120, 0, 144, 35 }, { L"U", 'U', 144, 0, 168, 35 }, { L"I", 'I', 168, 0, 192, 35 },
    { L"O", 'O', 192, 0, 216, 35 }, { L"P", 'P', 216, 0, 240, 35 },
    { L"A", 'A', 12, 35, 36, 70 }, { L"S", 'S', 36, 35, 60, 70 }, { L"D", 'D', 60, 35, 84, 70 }, { L"F", 'F', 84, 35, 108, 70 },
    { L"G", 'G', 108, 35, 132, 70 }, { L"H", 'H', 132, 35, 156, 70 }, { L"J", 'J', 156, 35, 180, 70 }, { L"K", 'K', 180, 35, 204, 70 },
    { L"L", 'L', 204, 35, 228, 70 },
    { L"Func", VK_SHIFT_KEY, 0, 70, 24, 105 }, { L"Z", 'Z', 24, 70, 48, 105 }, { L"X", 'X', 48, 70, 72, 105 },
    { L"C", 'C', 72, 70, 96, 105 }, { L"V", 'V', 96, 70, 120, 105 }, { L"B", 'B', 120, 70, 144, 105 }, { L"N", 'N', 144, 70, 168, 105 },
    { L"M", 'M', 168, 70, 192, 105 }, { L"-", VK_MINUS_KEY, 192, 70, 216, 105 }, { L"Del", VK_BACK, 216, 70, 240, 105 },
    { L"Prev", VK_PRIOR, 0, 105, 24, 140 }, { L"Next", VK_NEXT, 24, 105, 48, 140 }, { L"Sound", VK_FN_KEY, 48, 105, 72, 140 },
    { L"S-Jump", VK_SLASH_KEY, 72, 105, 96, 140 }, { L"Back", VK_ESCAPE, 96, 105, 120, 140 }, { L"<", VK_LEFT, 120, 105, 144, 140 },
    { L"^", VK_UP, 144, 105, 168, 140 }, { L"v", VK_DOWN, 168, 105, 192, 140 }, { L">", VK_RIGHT, 192, 105, 216, 140 },
    { L"Enter", VK_RETURN, 216, 105, 240, 140 },
};

static const WCHAR *const notes[] = {
    L"Blue: Fn + key.   Red: Shift + Fn + key.   Green: the key on its own.",
    L"Func is Shift and Sound is Fn: tap for the next key, hold for more.   Fn + arrows: volume and brightness on the sub screen.",
    L"Toggle this map with the tray icon or Start > Programs > Keyboard Help.",
};

const struct keymap_model keymap_a7300 = {
    L"PW-A7300",
    help_keys,
    sizeof(help_keys) / sizeof(help_keys[0]),
    notes,
    sizeof(notes) / sizeof(notes[0]),
    NULL,
    start,
    stop,
    handle_key,
    NULL,
    state_changed,
    describe,
    layer_label,
    model_message,
};
