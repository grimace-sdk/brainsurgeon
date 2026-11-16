#include "keymap.h"

#define HELP_CLASS L"KeymapHelp"
#define HELP_MARGIN 8
#define HELP_NOTE_HEIGHT 14
#define HELP_GAP 3
#define HELP_LAYER_COLOUR RGB(0x00, 0x40, 0xc0)
#define HELP_SHIFTED_COLOUR RGB(0xc0, 0x20, 0x20)
#define HELP_NUMBER_COLOUR RGB(0x80, 0x00, 0xa0)
#define HELP_REMAP_COLOUR RGB(0x00, 0x80, 0x20)
#define HELP_TOP_REMAP_COLOUR RGB(0x80, 0xff, 0x80)

static const struct keymap_model *help_model;
static HWND help_window;
static BOOL help_standalone;

static HFONT create_sized_font(LONG height, LONG weight)
{
    LOGFONTW font = { 0 };

    font.lfHeight = height;
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(font.lfFaceName, L"Tahoma");
    return CreateFontIndirectW(&font);
}

static void key_extent(RECT *extent)
{
    int index;

    SetRect(extent, 0x7fff, 0x7fff, 0, 0);
    for (index = 0; index < help_model->key_count; index++) {
        const struct help_key *key = &help_model->keys[index];

        if (key->left < extent->left)
            extent->left = key->left;
        if (key->top < extent->top)
            extent->top = key->top;
        if (key->right > extent->right)
            extent->right = key->right;
        if (key->bottom > extent->bottom)
            extent->bottom = key->bottom;
    }
}

static void key_rect(const struct help_key *key, const RECT *extent, const RECT *area, RECT *rect)
{
    int extent_width = extent->right - extent->left, extent_height = extent->bottom - extent->top;
    int area_width = area->right - area->left, area_height = area->bottom - area->top;

    rect->left = area->left + (key->left - extent->left) * area_width / extent_width;
    rect->right = area->left + (key->right - extent->left) * area_width / extent_width - HELP_GAP;
    rect->top = area->top + (key->top - extent->top) * area_height / extent_height;
    rect->bottom = area->top + (key->bottom - extent->top) * area_height / extent_height - HELP_GAP;
}

static void draw_text(HDC context, HFONT font, COLORREF colour, const WCHAR *text, const RECT *rect, UINT format)
{
    if (!text[0])
        return;
    SelectObject(context, font);
    SetTextColor(context, colour);
    DrawTextW(context, text, -1, (RECT *)rect, format | DT_SINGLELINE);
}

static void paint_key(HDC context, const struct help_key *key, const RECT *rect, HFONT large, HFONT medium, HFONT small)
{
    struct help_legends legends = { 0 };
    BOOL top = key->style == STYLE_TOP;
    RECT text_rect;

    FillRect(context, rect, GetSysColorBrush(top ? COLOR_ACTIVECAPTION : COLOR_BTNFACE));
    DrawEdge(context, (RECT *)rect, EDGE_RAISED, BF_RECT);
    SetRect(&text_rect, rect->left + 4, rect->top + 2, rect->right - 2, rect->bottom);
    draw_text(context, key->label[1] ? medium : large, GetSysColor(top ? COLOR_CAPTIONTEXT : COLOR_BTNTEXT), key->label, &text_rect,
        DT_LEFT | DT_TOP);
    if (help_model->describe)
        help_model->describe(key, &legends);
    if (!legends.text[INK_REMAP][0] && key->vkey)
        remap_describe(key->vkey, legends.text[INK_REMAP], KEYMAP_LEGEND_LENGTH);
    SetRect(&text_rect, rect->left + 4, rect->top, rect->right - 2, rect->bottom - 2);
    draw_text(context, small, top ? HELP_TOP_REMAP_COLOUR : HELP_REMAP_COLOUR, legends.text[INK_REMAP], &text_rect,
        DT_LEFT | DT_BOTTOM | DT_END_ELLIPSIS);
    SetRect(&text_rect, rect->left + 2, rect->top, rect->right - 2, rect->bottom - 1);
    draw_text(context, small, HELP_NUMBER_COLOUR, legends.text[INK_NAVIGATION], &text_rect, DT_CENTER | DT_BOTTOM);
    SetRect(&text_rect, rect->left + 4, rect->top, rect->right, rect->bottom - 1);
    draw_text(context, medium, HELP_NUMBER_COLOUR, legends.text[INK_NUMBER], &text_rect, DT_LEFT | DT_BOTTOM);
    SetRect(&text_rect, rect->left, rect->top, rect->right - 4, rect->bottom - 1);
    draw_text(context, legends.text[INK_LAYER][1] ? small : large, HELP_LAYER_COLOUR, legends.text[INK_LAYER], &text_rect,
        DT_RIGHT | DT_BOTTOM);
    SetRect(&text_rect, rect->left, rect->top + 2, rect->right - 4, rect->bottom);
    draw_text(context, medium, HELP_SHIFTED_COLOUR, legends.text[INK_SHIFTED], &text_rect, DT_RIGHT | DT_TOP);
}

static void paint_help(HDC context, const RECT *client)
{
    HFONT large = create_sized_font(-18, FW_BOLD);
    HFONT medium = create_sized_font(-12, FW_BOLD);
    HFONT small = create_sized_font(-11, FW_NORMAL);
    HFONT previous = (HFONT)SelectObject(context, small);
    int notes = help_model->note_count + (help_standalone ? 1 : 0);
    RECT extent, area, rect;
    int index;

    FillRect(context, client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(context, TRANSPARENT);
    key_extent(&extent);
    SetRect(&area, client->left + HELP_MARGIN, client->top + HELP_MARGIN, client->right - HELP_MARGIN,
        client->bottom - HELP_MARGIN - notes * HELP_NOTE_HEIGHT - HELP_MARGIN);
    if (extent.right > extent.left && extent.bottom > extent.top && area.right > area.left && area.bottom > area.top) {
        for (index = 0; index < help_model->key_count; index++) {
            key_rect(&help_model->keys[index], &extent, &area, &rect);
            paint_key(context, &help_model->keys[index], &rect, large, medium, small);
        }
    }
    SetRect(&rect, client->left + HELP_MARGIN, area.bottom + HELP_MARGIN, client->right - HELP_MARGIN,
        area.bottom + HELP_MARGIN + HELP_NOTE_HEIGHT);
    for (index = 0; index < help_model->note_count; index++) {
        draw_text(context, small, GetSysColor(COLOR_WINDOWTEXT), help_model->notes[index], &rect, DT_LEFT | DT_END_ELLIPSIS);
        OffsetRect(&rect, 0, HELP_NOTE_HEIGHT);
    }
    if (help_standalone)
        draw_text(context, small, GetSysColor(COLOR_WINDOWTEXT), L"Tap or press any key to close.", &rect, DT_LEFT);
    SelectObject(context, previous);
    DeleteObject(large);
    DeleteObject(medium);
    DeleteObject(small);
}

static void place_window(HWND window)
{
    RECT work_area;

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    SetWindowPos(window, HWND_TOPMOST, work_area.left, work_area.top, work_area.right - work_area.left,
        work_area.bottom - work_area.top, SWP_NOACTIVATE);
}

static LRESULT CALLBACK help_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    RECT client;

    switch (message) {
    case WM_PAINT:
        BeginPaint(window, &paint);
        GetClientRect(window, &client);
        paint_help(paint.hdc, &client);
        EndPaint(window, &paint);
        return 0;
    case WM_LBUTTONUP:
    case WM_KEYUP:
        if (help_standalone)
            DestroyWindow(window);
        else
            ShowWindow(window, SW_HIDE);
        return 0;
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_LBUTTONDOWN:
        return 0;
    case WM_DESTROY:
        if (help_standalone)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void help_create(HINSTANCE instance, const struct keymap_model *model, BOOL standalone)
{
    WNDCLASSW window_class = { 0 };

    help_model = model;
    help_standalone = standalone;
    window_class.lpfnWndProc = help_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = HELP_CLASS;
    RegisterClassW(&window_class);
    help_window = CreateWindowExW(standalone ? WS_EX_TOPMOST : WS_EX_TOPMOST | WS_EX_NOACTIVATE, HELP_CLASS, L"Keyboard Help",
        WS_POPUP | WS_BORDER, 0, 0, 1, 1, NULL, NULL, instance, NULL);
}

void help_toggle(void)
{
    if (!help_window)
        return;
    if (IsWindowVisible(help_window)) {
        ShowWindow(help_window, SW_HIDE);
        return;
    }
    place_window(help_window);
    InvalidateRect(help_window, NULL, TRUE);
    SetWindowPos(help_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(help_window);
}

int help_run(void)
{
    MSG message;

    if (!help_window)
        return 1;
    place_window(help_window);
    ShowWindow(help_window, SW_SHOW);
    UpdateWindow(help_window);
    SetForegroundWindow(help_window);
    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
