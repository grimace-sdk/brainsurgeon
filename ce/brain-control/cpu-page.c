#include <windows.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "board_ioctl.h"

#define BOARD_DEVICE L"BRD1:"
#define CPU_SCALING_OFF 0
#define CPU_SCALING_FAST 1
#define CPU_SCALING_DYNAMIC 2
#define STATUS_TIMER 1
#define STATUS_INTERVAL_MILLISECONDS 1000
#define IDLE_UNSUPPORTED 0xFFFFFFFF
#define GRAPH_SAMPLES 60
#define GRAPH_GAP 4
#define GRAPH_MINIMUM_HEIGHT 24
#define GRAPH_MAXIMUM_HEIGHT 120
#define HIGH_CLOCK_MHZ 300
#define HIGH_CLOCK_COLOUR RGB(255, 224, 178)

static const struct {
    DWORD mode;
    int button;
} modes[] = {
    { CPU_SCALING_OFF, IDC_CPU_SHARP },
    { CPU_SCALING_FAST, IDC_CPU_FAST },
    { CPU_SCALING_DYNAMIC, IDC_CPU_DYNAMIC },
};

#define MODE_COUNT (sizeof(modes) / sizeof(modes[0]))

static DWORD original_mode;
static BOOL changed;
static DWORD sample_idle;
static DWORD sample_tick;
static BOOL sample_valid;
static BYTE history_busy[GRAPH_SAMPLES];
static BOOL history_fast[GRAPH_SAMPLES];
static int history_count;
static HDC graph_context;
static HBITMAP graph_bitmap;
static HGDIOBJ graph_previous_bitmap;
static RECT graph_area;
static int graph_column;

BOOL cpu_page_available(void)
{
    return device_present(BOARD_DEVICE);
}

static BOOL cpu_scaling(const DWORD *request, BRD_CPU_STATUS *status)
{
    HANDLE board = device_open(BOARD_DEVICE);
    DWORD returned = 0;
    BOOL succeeded;

    if (board == INVALID_HANDLE_VALUE)
        return FALSE;
    memset(status, 0, sizeof(*status));
    succeeded = DeviceIoControl(board, BRD_IOCTL_CPU_SCALING, (void *)request, request ? sizeof(*request) : 0,
        status, sizeof(*status), &returned, NULL) && returned >= sizeof(*status);
    CloseHandle(board);
    return succeeded;
}

static void set_mode(DWORD mode, BOOL save)
{
    BRD_CPU_STATUS status;
    DWORD request = mode | (save ? BRD_CPU_SCALING_SAVE : 0);

    cpu_scaling(&request, &status);
}

static DWORD chosen_mode(HWND page)
{
    unsigned int index;

    for (index = 0; index < MODE_COUNT; index++)
        if (IsDlgButtonChecked(page, modes[index].button) == BST_CHECKED)
            return modes[index].mode;
    return CPU_SCALING_DYNAMIC;
}

static void start_sample(void)
{
    sample_idle = GetIdleTime();
    sample_tick = GetTickCount();
    sample_valid = sample_idle != IDLE_UNSUPPORTED;
}

static BOOL busy_percent(DWORD *busy)
{
    DWORD idle = GetIdleTime(), tick = GetTickCount(), elapsed, idle_elapsed;

    if (!sample_valid || idle == IDLE_UNSUPPORTED) {
        start_sample();
        return FALSE;
    }
    elapsed = tick - sample_tick;
    idle_elapsed = idle - sample_idle;
    if (!elapsed)
        return FALSE;
    if (idle_elapsed > elapsed)
        idle_elapsed = elapsed;
    *busy = 100 - idle_elapsed * 100 / elapsed;
    sample_idle = idle;
    sample_tick = tick;
    return TRUE;
}

static void release_graph(void)
{
    if (graph_context) {
        SelectObject(graph_context, graph_previous_bitmap);
        DeleteDC(graph_context);
    }
    if (graph_bitmap)
        DeleteObject(graph_bitmap);
    graph_context = NULL;
    graph_bitmap = NULL;
}

static void fill(HDC context, int left, int top, int right, int bottom, COLORREF colour)
{
    RECT area = { left, top, right, bottom };
    HBRUSH brush = CreateSolidBrush(colour);

    FillRect(context, &area, brush);
    DeleteObject(brush);
}

static void draw_column(int right, int busy, BOOL fast)
{
    int height = graph_area.bottom - graph_area.top;
    int left = right - graph_column;

    fill(graph_context, left, 0, right, height, fast ? HIGH_CLOCK_COLOUR : GetSysColor(COLOR_WINDOW));
    fill(graph_context, left, height - height * busy / 100, right, height, GetSysColor(COLOR_HIGHLIGHT));
}

static void redraw_graph(void)
{
    int width = graph_area.right - graph_area.left, index;

    fill(graph_context, 0, 0, width, graph_area.bottom - graph_area.top, GetSysColor(COLOR_WINDOW));
    for (index = 0; index < history_count; index++)
        draw_column(width - (history_count - 1 - index) * graph_column, history_busy[index], history_fast[index]);
}

static void layout_graph(HWND page)
{
    RECT client, status;
    HDC context;

    release_graph();
    GetClientRect(page, &client);
    GetWindowRect(GetDlgItem(page, IDC_CPU_STATUS), &status);
    MapWindowPoints(NULL, page, (POINT *)&status, 2);
    SetRect(&graph_area, 0, status.bottom + GRAPH_GAP, client.right, client.bottom);
    if (graph_area.bottom - graph_area.top > GRAPH_MAXIMUM_HEIGHT)
        graph_area.bottom = graph_area.top + GRAPH_MAXIMUM_HEIGHT;
    if (graph_area.bottom - graph_area.top < GRAPH_MINIMUM_HEIGHT || graph_area.right < GRAPH_SAMPLES)
        return;
    graph_column = graph_area.right / GRAPH_SAMPLES;
    context = GetDC(page);
    graph_context = CreateCompatibleDC(context);
    graph_bitmap = CreateCompatibleBitmap(context, graph_area.right - graph_area.left, graph_area.bottom - graph_area.top);
    ReleaseDC(page, context);
    if (!graph_context || !graph_bitmap) {
        release_graph();
        return;
    }
    graph_previous_bitmap = SelectObject(graph_context, graph_bitmap);
    redraw_graph();
    InvalidateRect(page, &graph_area, FALSE);
}

static void add_sample(HWND page, DWORD busy, DWORD mhz)
{
    int width = graph_area.right - graph_area.left, height = graph_area.bottom - graph_area.top;

    if (history_count == GRAPH_SAMPLES) {
        memmove(history_busy, history_busy + 1, GRAPH_SAMPLES - 1);
        memmove(history_fast, history_fast + 1, (GRAPH_SAMPLES - 1) * sizeof(history_fast[0]));
        history_count--;
    }
    history_busy[history_count] = (BYTE)(busy > 100 ? 100 : busy);
    history_fast[history_count] = mhz >= HIGH_CLOCK_MHZ;
    history_count++;
    if (!graph_context)
        return;
    BitBlt(graph_context, 0, 0, width - graph_column, height, graph_context, graph_column, 0, SRCCOPY);
    draw_column(width, history_busy[history_count - 1], history_fast[history_count - 1]);
    InvalidateRect(page, &graph_area, FALSE);
}

static void paint_graph(HWND page)
{
    PAINTSTRUCT paint;
    HDC context = BeginPaint(page, &paint);

    if (graph_context)
        BitBlt(context, graph_area.left, graph_area.top, graph_area.right - graph_area.left, graph_area.bottom - graph_area.top,
            graph_context, 0, 0, SRCCOPY);
    EndPaint(page, &paint);
}

static void show_status(HWND page, BOOL sample)
{
    BRD_CPU_STATUS status;
    WCHAR text[TEXT_LENGTH * 2];
    BOOL measured;
    DWORD busy;

    if (!cpu_scaling(NULL, &status) || !status.CpuMHz) {
        SetDlgItemTextW(page, IDC_CPU_STATUS, L"CPU scaling unavailable");
        return;
    }
    wsprintfW(text, L"Now %u MHz, bus %u MHz\r\n", status.CpuMHz, status.AhbMHz);
    measured = busy_percent(&busy);
    if (!measured && GetIdleTime() == IDLE_UNSUPPORTED) {
        busy = status.BusyPercent;
        measured = TRUE;
    }
    if (measured) {
        wsprintfW(text + wcslen(text), L"%u%% busy\r\n", busy);
        if (sample)
            add_sample(page, busy, status.CpuMHz);
    } else {
        wcscat(text, L"Measuring\r\n");
    }
    wsprintfW(text + wcslen(text), L"%u clock changes since start", status.Switches);
    SetDlgItemTextW(page, IDC_CPU_STATUS, text);
}

static void show_settings(HWND page)
{
    BRD_CPU_STATUS status;
    unsigned int index;

    changed = FALSE;
    history_count = 0;
    original_mode = cpu_scaling(NULL, &status) ? status.Mode : CPU_SCALING_DYNAMIC;
    for (index = 0; index < MODE_COUNT; index++)
        if (modes[index].mode == original_mode)
            CheckRadioButton(page, IDC_CPU_SHARP, IDC_CPU_DYNAMIC, modes[index].button);
    sample_valid = FALSE;
    show_status(page, FALSE);
}

BOOL CALLBACK cpu_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        show_settings(page);
        return TRUE;
    case WM_SIZE:
        layout_graph(page);
        return TRUE;
    case WM_PAINT:
        paint_graph(page);
        return TRUE;
    case WM_TIMER:
        if (wparam == STATUS_TIMER) {
            show_status(page, TRUE);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= IDC_CPU_SHARP && LOWORD(wparam) <= IDC_CPU_DYNAMIC) {
            changed = TRUE;
            set_mode(chosen_mode(page), FALSE);
            show_status(page, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void cpu_page_shown(HWND page, BOOL visible)
{
    if (!visible) {
        KillTimer(page, STATUS_TIMER);
        return;
    }
    start_sample();
    show_status(page, FALSE);
    SetTimer(page, STATUS_TIMER, STATUS_INTERVAL_MILLISECONDS, NULL);
}

void cpu_page_save(HWND page)
{
    KillTimer(page, STATUS_TIMER);
    release_graph();
    if (changed)
        set_mode(chosen_mode(page), TRUE);
}

void cpu_page_revert(HWND page)
{
    KillTimer(page, STATUS_TIMER);
    release_graph();
    if (changed)
        set_mode(original_mode, FALSE);
}
