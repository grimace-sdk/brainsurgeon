#include <windows.h>
#include <cpl.h>
#include <commctrl.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "icons.h"
#include "../applet-icon.h"

#define APPLET_NAME L"Brain"
#define APPLET_INFO L"Display, sound, power, CPU and other Brain settings"
#define WINDOW_TITLE L"Brain Control"
#define NARROW_WIDTH 400
#define LARGE_ICON_SCREEN 480
#define MARGIN 4
#define ROW_PADDING 8
#define SYNC_SECTION_MESSAGE (WM_APP + 1)
#define BUTTON_WIDTH_UNITS 50
#define BUTTON_HEIGHT_UNITS 16
#define TITLE_SCALE_PERCENT 140
#define RULE_GAP 2
#define DESCRIPTION_LINES 2

typedef BOOL (WINAPI *init_common_controls_function)(const INITCOMMONCONTROLSEX *controls);

struct section {
    const WCHAR *title;
    const WCHAR *description;
    const struct brain_icon *small_icon;
    const struct brain_icon *large_icon;
    DWORD models;
    BOOL (*available)(void);
    int dialog;
    DLGPROC procedure;
    void (*save)(HWND page);
    void (*revert)(HWND page);
    void (*shown)(HWND page, BOOL visible);
    HWND page;
};

static struct section sections[] = {
    { L"Display", L"Screen brightness, and the sub screen where there is one", &icon_brightness_6_24, &icon_brightness_6_32, MODEL_ALL, display_page_available,
        IDD_PAGE_DISPLAY, display_page_procedure, display_page_save, display_page_revert, NULL, NULL },
    { L"Rotation", L"Which way up the screen is, now and at startup", &icon_screen_rotation_24, &icon_screen_rotation_32, MODEL_PWSH2 | MODEL_PWSB1 | MODEL_PWA7300, NULL,
        IDD_PAGE_ROTATION, rotation_page_procedure, rotation_page_save, NULL, NULL, NULL },
    { L"Night Light", L"Warmer screen colours in the evening, on a schedule", &icon_bedtime_24, &icon_bedtime_32, MODEL_PWA7300, night_light_page_available,
        IDD_PAGE_NIGHT_LIGHT, night_light_page_procedure, night_light_page_save, night_light_page_revert, night_light_page_shown, NULL },
    { L"Touchpad", L"The touch panel under the sub screen as a mouse", &icon_touchpad_mouse_24, &icon_touchpad_mouse_32, MODEL_PWA7300, touchpad_page_available,
        IDD_PAGE_TOUCHPAD, touchpad_page_procedure, touchpad_page_save, touchpad_page_revert, NULL, NULL },
    { L"Sound", L"Volume, and clicks for keys and screen taps", &icon_volume_up_24, &icon_volume_up_32, MODEL_ALL, NULL,
        IDD_PAGE_SOUND, sound_page_procedure, sound_page_save, sound_page_revert, NULL, NULL },
    { L"Power", L"Battery level, when to dim and sleep, and the battery type on AA models", &icon_battery_full_24, &icon_battery_full_32, MODEL_ALL, NULL,
        IDD_PAGE_POWER, power_page_procedure, power_page_save, power_page_revert, power_page_shown, NULL },
    { L"CPU", L"How fast the processor runs, and when it speeds up", &icon_speed_24, &icon_speed_32, MODEL_ALL, cpu_page_available,
        IDD_PAGE_CPU, cpu_page_procedure, cpu_page_save, cpu_page_revert, cpu_page_shown, NULL },
    { L"Debug", L"Debug Agent, for a computer to copy files and run programs over USB", &icon_bug_report_24, &icon_bug_report_32, MODEL_ALL, NULL,
        IDD_PAGE_DEBUG, debug_page_procedure, debug_page_save, debug_page_revert, debug_page_shown, NULL },
    { L"About", L"Versions of brainsurgeon, its drivers and this Brain", &icon_info_24, &icon_info_32, MODEL_ALL, NULL,
        IDD_PAGE_ABOUT, about_page_procedure, NULL, NULL, NULL, NULL },
};

#define SECTION_COUNT (sizeof(sections) / sizeof(sections[0]))

static HINSTANCE module_instance;
static HMODULE common_controls;
static int selected_section = -1;
static BOOL large_icons;
static int list_sections[SECTION_COUNT];
static int list_count;
static HICON window_large_icon;
static HFONT title_font;
static RECT heading_area;
static HICON window_small_icon;
static WNDPROC list_procedure_original;

static BOOL section_shown(const struct section *section, DWORD model)
{
    if (model == MODEL_UNKNOWN)
        return section->available ? section->available() : section->models == MODEL_ALL;
    return (section->models & model) && (!section->available || section->available());
}

static const struct brain_icon *section_icon(const struct section *section)
{
    return large_icons ? section->large_icon : section->small_icon;
}

static void draw_icon(HDC context, const struct brain_icon *icon, int left, int top, COLORREF background, COLORREF foreground)
{
    BITMAPINFO information;
    DWORD *pixels = NULL;
    HBITMAP bitmap;
    HDC memory;
    HGDIOBJ previous;
    int index, channel;
    int background_channels[3] = { GetRValue(background), GetGValue(background), GetBValue(background) };
    int foreground_channels[3] = { GetRValue(foreground), GetGValue(foreground), GetBValue(foreground) };

    memset(&information, 0, sizeof(information));
    information.bmiHeader.biSize = sizeof(information.bmiHeader);
    information.bmiHeader.biWidth = icon->size;
    information.bmiHeader.biHeight = -icon->size;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(context, &information, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!bitmap || !pixels) {
        if (bitmap)
            DeleteObject(bitmap);
        return;
    }
    for (index = 0; index < icon->size * icon->size; index++) {
        int amount = icon->alpha[index];
        int mixed[3];

        for (channel = 0; channel < 3; channel++)
            mixed[channel] = background_channels[channel] + (foreground_channels[channel] - background_channels[channel]) * amount / 255;
        pixels[index] = (DWORD)mixed[0] << 16 | (DWORD)mixed[1] << 8 | (DWORD)mixed[2];
    }
    memory = CreateCompatibleDC(context);
    previous = SelectObject(memory, bitmap);
    BitBlt(context, left, top, icon->size, icon->size, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteDC(memory);
    DeleteObject(bitmap);
}

static int section_at(int item)
{
    if (item < 0 || item >= list_count)
        return -1;
    return list_sections[item];
}

static BOOL narrow_layout(HWND dialog)
{
    RECT client;

    GetClientRect(dialog, &client);
    return client.right - client.left < NARROW_WIDTH;
}

static void draw_section(HWND dialog, const DRAWITEMSTRUCT *item)
{
    const struct section *section;
    BOOL selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    COLORREF foreground = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    const struct brain_icon *icon;
    int section_index = section_at((int)item->itemID);
    BOOL narrow = narrow_layout(dialog);
    HBRUSH brush;
    RECT text_area;
    int icon_left;

    if (section_index < 0)
        return;
    section = &sections[section_index];
    icon = section_icon(section);
    brush = CreateSolidBrush(background);
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    icon_left = narrow ? item->rcItem.left + (item->rcItem.right - item->rcItem.left - icon->size) / 2 : item->rcItem.left + ROW_PADDING / 2;
    draw_icon(item->hDC, icon, icon_left, item->rcItem.top + (item->rcItem.bottom - item->rcItem.top - icon->size) / 2, background, foreground);
    if (narrow)
        return;
    text_area = item->rcItem;
    text_area.left += icon->size + ROW_PADDING;
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, foreground);
    DrawTextW(item->hDC, section->title, -1, &text_area, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static int widest_title(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_SECTIONS);
    HDC context = GetDC(list);
    HGDIOBJ previous = SelectObject(context, (HGDIOBJ)SendMessageW(list, WM_GETFONT, 0, 0));
    int widest = 0;
    unsigned int index;
    SIZE size;

    for (index = 0; index < SECTION_COUNT; index++)
        if (GetTextExtentPoint32W(context, sections[index].title, (int)wcslen(sections[index].title), &size) && size.cx > widest)
            widest = size.cx;
    SelectObject(context, previous);
    ReleaseDC(list, context);
    return widest;
}

static int text_height(HWND dialog)
{
    TEXTMETRICW metrics;
    HDC context = GetDC(dialog);
    HGDIOBJ previous = SelectObject(context, (HGDIOBJ)SendMessageW(dialog, WM_GETFONT, 0, 0));

    if (!GetTextMetricsW(context, &metrics))
        metrics.tmHeight = 16;
    SelectObject(context, previous);
    ReleaseDC(dialog, context);
    return metrics.tmHeight;
}

static void create_title_font(HWND dialog)
{
    LOGFONTW description;
    HFONT dialog_font = (HFONT)SendMessageW(dialog, WM_GETFONT, 0, 0);

    title_font = NULL;
    if (!dialog_font || !GetObjectW(dialog_font, sizeof(description), &description))
        return;
    description.lfHeight = description.lfHeight * TITLE_SCALE_PERCENT / 100;
    description.lfWeight = FW_BOLD;
    title_font = CreateFontIndirectW(&description);
}

static void release_title_font(void)
{
    if (title_font)
        DeleteObject(title_font);
    title_font = NULL;
}

static int title_height(HWND dialog)
{
    TEXTMETRICW metrics;
    HDC context;
    HGDIOBJ previous;

    if (!title_font)
        return text_height(dialog);
    context = GetDC(dialog);
    previous = SelectObject(context, title_font);
    if (!GetTextMetricsW(context, &metrics))
        metrics.tmHeight = text_height(dialog);
    SelectObject(context, previous);
    ReleaseDC(dialog, context);
    return metrics.tmHeight;
}

static void paint_heading(HWND dialog)
{
    const struct section *section = selected_section >= 0 ? &sections[selected_section] : NULL;
    PAINTSTRUCT paint;
    HDC context = BeginPaint(dialog, &paint);
    HGDIOBJ previous;
    HBRUSH rule;
    RECT area;
    int title_bottom;

    if (section) {
        SetBkMode(context, TRANSPARENT);
        SetTextColor(context, GetSysColor(COLOR_WINDOWTEXT));
        area = heading_area;
        title_bottom = area.top + title_height(dialog);
        area.bottom = title_bottom;
        previous = SelectObject(context, title_font ? title_font : (HFONT)SendMessageW(dialog, WM_GETFONT, 0, 0));
        DrawTextW(context, section->title, -1, &area, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(context, (HFONT)SendMessageW(dialog, WM_GETFONT, 0, 0));
        area.top = title_bottom + RULE_GAP;
        area.bottom = area.top + 1;
        rule = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        FillRect(context, &area, rule);
        DeleteObject(rule);
        area.top = area.bottom + RULE_GAP;
        area.bottom = heading_area.bottom;
        DrawTextW(context, section->description, -1, &area, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(context, previous);
    }
    EndPaint(dialog, &paint);
}

static int row_height(HWND dialog)
{
    return max(section_icon(&sections[0])->size, text_height(dialog)) + ROW_PADDING;
}

static void layout(HWND dialog)
{
    RECT client, button = { 0, 0, BUTTON_WIDTH_UNITS, BUTTON_HEIGHT_UNITS };
    int icon_size, list_width, pane_left, pane_width, heading_height, button_top;
    unsigned int index;

    GetClientRect(dialog, &client);
    MapDialogRect(dialog, &button);
    icon_size = section_icon(&sections[0])->size;
    list_width = icon_size + ROW_PADDING + GetSystemMetrics(SM_CXVSCROLL);
    if (!narrow_layout(dialog))
        list_width += widest_title(dialog) + ROW_PADDING;
    heading_height = title_height(dialog) + RULE_GAP * 2 + 1 + text_height(dialog) * DESCRIPTION_LINES + MARGIN;
    pane_left = list_width + MARGIN * 2;
    pane_width = client.right - pane_left - MARGIN;
    button_top = client.bottom - button.bottom - MARGIN;
    MoveWindow(GetDlgItem(dialog, IDC_SECTIONS), 0, 0, list_width, client.bottom, TRUE);
    SetRect(&heading_area, pane_left, MARGIN, pane_left + pane_width, MARGIN + heading_height);
    InvalidateRect(dialog, &heading_area, TRUE);
    MoveWindow(GetDlgItem(dialog, IDCANCEL), client.right - MARGIN - button.right, button_top, button.right, button.bottom, TRUE);
    MoveWindow(GetDlgItem(dialog, IDOK), client.right - MARGIN * 2 - button.right * 2, button_top, button.right, button.bottom, TRUE);
    for (index = 0; index < SECTION_COUNT; index++)
        if (sections[index].page)
            MoveWindow(sections[index].page, pane_left, MARGIN * 2 + heading_height, pane_width,
                button_top - MARGIN * 3 - heading_height, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_SECTIONS), NULL, TRUE);
}

static void fill_work_area(HWND dialog)
{
    RECT area;

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0))
        SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    MoveWindow(dialog, area.left, area.top, area.right - area.left, area.bottom - area.top, TRUE);
}

static void select_section(HWND dialog, int index)
{
    if (index < 0 || index >= (int)SECTION_COUNT || !sections[index].page || index == selected_section)
        return;
    if (selected_section >= 0) {
        ShowWindow(sections[selected_section].page, SW_HIDE);
        if (sections[selected_section].shown)
            sections[selected_section].shown(sections[selected_section].page, FALSE);
    }
    selected_section = index;
    InvalidateRect(dialog, &heading_area, TRUE);
    ShowWindow(sections[index].page, SW_SHOW);
    if (sections[index].shown)
        sections[index].shown(sections[index].page, TRUE);
}

static void selection_changed(HWND dialog)
{
    LRESULT item = SendDlgItemMessageW(dialog, IDC_SECTIONS, LB_GETCURSEL, 0, 0);

    if (item != LB_ERR)
        select_section(dialog, section_at((int)item));
}

static LRESULT CALLBACK list_procedure(HWND list, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result = CallWindowProcW(list_procedure_original, list, message, wparam, lparam);

    if (message == WM_LBUTTONUP || message == WM_KEYUP)
        PostMessageW(GetParent(list), SYNC_SECTION_MESSAGE, 0, 0);
    return result;
}

static void set_window_identity(HWND dialog)
{
    WCHAR path[MAX_PATH];

    SetWindowTextW(dialog, WINDOW_TITLE);
    window_large_icon = NULL;
    window_small_icon = NULL;
    if (!GetModuleFileNameW(module_instance, path, MAX_PATH))
        return;
    ExtractIconExW(path, 0, &window_large_icon, &window_small_icon, 1);
    if (window_large_icon)
        SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)window_large_icon);
    if (window_small_icon)
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)window_small_icon);
}

static void release_window_identity(void)
{
    if (window_large_icon)
        DestroyIcon(window_large_icon);
    if (window_small_icon)
        DestroyIcon(window_small_icon);
    window_large_icon = NULL;
    window_small_icon = NULL;
}

static void initialise(HWND dialog)
{
    DWORD model = model_detect();
    int screen_short = min(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    unsigned int index;

    set_window_identity(dialog);
    create_title_font(dialog);
    large_icons = screen_short >= LARGE_ICON_SCREEN;
    selected_section = -1;
    list_count = 0;
    for (index = 0; index < SECTION_COUNT; index++) {
        sections[index].page = NULL;
        if (!section_shown(&sections[index], model))
            continue;
        sections[index].page = CreateDialogParamW(module_instance, MAKEINTRESOURCEW(sections[index].dialog), dialog,
            sections[index].procedure, 0);
        if (sections[index].page) {
            LRESULT item = SendDlgItemMessageW(dialog, IDC_SECTIONS, LB_ADDSTRING, 0, (LPARAM)sections[index].title);

            if (item >= 0 && item < (LRESULT)SECTION_COUNT && item == list_count)
                list_sections[list_count++] = (int)index;
        }
    }
    SendDlgItemMessageW(dialog, IDC_SECTIONS, LB_SETITEMHEIGHT, 0, row_height(dialog));
    list_procedure_original = (WNDPROC)GetWindowLongW(GetDlgItem(dialog, IDC_SECTIONS), GWL_WNDPROC);
    SetWindowLongW(GetDlgItem(dialog, IDC_SECTIONS), GWL_WNDPROC, (LONG)list_procedure);
    fill_work_area(dialog);
    layout(dialog);
    SendDlgItemMessageW(dialog, IDC_SECTIONS, LB_SETCURSEL, 0, 0);
    selection_changed(dialog);
}

static void finish(HWND dialog, BOOL save)
{
    unsigned int index;

    for (index = 0; index < SECTION_COUNT; index++) {
        if (!sections[index].page)
            continue;
        if (save && sections[index].save)
            sections[index].save(sections[index].page);
        if (!save && sections[index].revert)
            sections[index].revert(sections[index].page);
    }
    EndDialog(dialog, save ? IDOK : IDCANCEL);
}

static BOOL CALLBACK dialog_procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        initialise(dialog);
        return TRUE;
    case WM_SIZE:
        layout(dialog);
        return TRUE;
    case WM_PAINT:
        paint_heading(dialog);
        return TRUE;
    case WM_MEASUREITEM:
        if (wparam == IDC_SECTIONS) {
            ((MEASUREITEMSTRUCT *)lparam)->itemHeight = row_height(dialog);
            return TRUE;
        }
        break;
    case WM_SETTINGCHANGE:
        if (wparam == SPI_SETWORKAREA)
            fill_work_area(dialog);
        return TRUE;
    case WM_DRAWITEM:
        if (wparam == IDC_SECTIONS) {
            draw_section(dialog, (const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;
    case SYNC_SECTION_MESSAGE:
        selection_changed(dialog);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_SECTIONS && HIWORD(wparam) == LBN_SELCHANGE) {
            selection_changed(dialog);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
            finish(dialog, LOWORD(wparam) == IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        module_instance = instance;
    return TRUE;
}

LONG CPlApplet(HWND parent, UINT message, LONG first, LONG second)
{
    NEWCPLINFOW *information;

    switch (message) {
    case CPL_INIT:
        common_controls = LoadLibraryW(L"commctrl.dll");
        if (common_controls) {
            init_common_controls_function init = (init_common_controls_function)GetProcAddressW(common_controls, L"InitCommonControlsEx");
            INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_BAR_CLASSES | ICC_DATE_CLASSES };

            if (init)
                init(&controls);
        }
        return TRUE;
    case CPL_GETCOUNT:
        return 1;
    case CPL_NEWINQUIRE:
        information = (NEWCPLINFOW *)second;
        memset(information, 0, sizeof(*information));
        information->dwSize = sizeof(*information);
        information->hIcon = applet_icon(module_instance);
        wcscpy(information->szName, APPLET_NAME);
        wcscpy(information->szInfo, APPLET_INFO);
        return 0;
    case CPL_DBLCLK:
        DialogBoxW(module_instance, MAKEINTRESOURCEW(IDD_BRAIN_CONTROL), parent, dialog_procedure);
        release_window_identity();
        release_title_font();
        return 0;
    case CPL_STOP:
        return 0;
    case CPL_EXIT:
        if (common_controls)
            FreeLibrary(common_controls);
        common_controls = NULL;
        return 0;
    }
    return 0;
}
