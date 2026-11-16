#include <windows.h>

#define EXIT_TIMER 1
#define EXIT_MILLISECONDS 60000
#define RAMP_ROWS 20
#define BAR_ROWS 60
#define STRIPE_ROWS 40
#define MARKER_SIZE 24

static const COLORREF s_Bars[] = {
    RGB(255, 255, 255), RGB(255, 255, 0), RGB(0, 255, 255), RGB(0, 255, 0),
    RGB(255, 0, 255), RGB(255, 0, 0), RGB(0, 0, 255), RGB(0, 0, 0),
};

static void Fill(HDC hdc, int Left, int Top, int Right, int Bottom, COLORREF Colour)
{
    RECT Area;
    HBRUSH hBrush = CreateSolidBrush(Colour);

    SetRect(&Area, Left, Top, Right, Bottom);
    FillRect(hdc, &Area, hBrush);
    DeleteObject(hBrush);
}

static void DrawRamp(HDC hdc, int Top, int Width, int Red, int Green, int Blue)
{
    int Column;

    for (Column = 0; Column < Width; Column++)
    {
        int Level = Width > 1 ? Column * 255 / (Width - 1) : 0;

        Fill(hdc, Column, Top, Column + 1, Top + RAMP_ROWS, RGB(Red * Level, Green * Level, Blue * Level));
    }
}

static void DrawMarker(HDC hdc, int Left, int Top, COLORREF Colour, const WCHAR *pLabel)
{
    RECT Area;

    Fill(hdc, Left, Top, Left + MARKER_SIZE, Top + MARKER_SIZE, Colour);
    SetRect(&Area, Left, Top, Left + MARKER_SIZE, Top + MARKER_SIZE);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawText(hdc, pLabel, -1, &Area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawPattern(HDC hdc, int Width, int Height)
{
    int Index, Column, Row, Top = 0;
    int BarWidth = Width / (int)(sizeof(s_Bars) / sizeof(s_Bars[0]));

    Fill(hdc, 0, 0, Width, Height, RGB(128, 128, 128));
    DrawRamp(hdc, Top, Width, 1, 0, 0);
    DrawRamp(hdc, Top + RAMP_ROWS, Width, 0, 1, 0);
    DrawRamp(hdc, Top + RAMP_ROWS * 2, Width, 0, 0, 1);
    DrawRamp(hdc, Top + RAMP_ROWS * 3, Width, 1, 1, 1);
    Top += RAMP_ROWS * 4;
    for (Index = 0; Index < (int)(sizeof(s_Bars) / sizeof(s_Bars[0])); Index++)
        Fill(hdc, Index * BarWidth, Top, Index * BarWidth + BarWidth, Top + BAR_ROWS, s_Bars[Index]);
    Top += BAR_ROWS;
    for (Column = 0; Column < Width; Column++)
        Fill(hdc, Column, Top, Column + 1, Top + STRIPE_ROWS, Column & 1 ? RGB(255, 255, 255) : RGB(0, 0, 0));
    Top += STRIPE_ROWS;
    for (Row = Top; Row < Top + STRIPE_ROWS; Row++)
        Fill(hdc, 0, Row, Width / 2, Row + 1, Row & 1 ? RGB(255, 255, 255) : RGB(0, 0, 0));
    Fill(hdc, Width / 2, Top, Width, Top + STRIPE_ROWS, RGB(0, 0, 0));
    for (Row = Top; Row < Top + STRIPE_ROWS; Row += 2)
        for (Column = Width / 2 + (Row & 2 ? 1 : 0); Column < Width; Column += 2)
            Fill(hdc, Column, Row, Column + 1, Row + 2, RGB(255, 255, 255));
    DrawMarker(hdc, 0, 0, RGB(255, 0, 0), L"TL");
    DrawMarker(hdc, Width - MARKER_SIZE, 0, RGB(0, 255, 0), L"TR");
    DrawMarker(hdc, 0, Height - MARKER_SIZE, RGB(0, 0, 255), L"BL");
    DrawMarker(hdc, Width - MARKER_SIZE, Height - MARKER_SIZE, RGB(255, 255, 255), L"BR");
}

static BOOL s_bSubpixel;

static void DrawSampleText(HDC hdc, int Left, int Top, COLORREF Foreground, COLORREF Background, const WCHAR *pText)
{
    LOGFONT Font;
    HFONT hFont, hOld;

    memset(&Font, 0, sizeof(Font));
    Font.lfHeight = -13;
    Font.lfQuality = CLEARTYPE_QUALITY;
    lstrcpy(Font.lfFaceName, L"Tahoma");
    hFont = CreateFontIndirect(&Font);
    hOld = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, Foreground);
    SetBkColor(hdc, Background);
    SetBkMode(hdc, OPAQUE);
    ExtTextOut(hdc, Left, Top, 0, NULL, pText, lstrlen(pText), NULL);
    SelectObject(hdc, hOld);
    DeleteObject(hFont);
}

static void DrawSubpixel(HDC hdc, int Width, int Height)
{
    static const COLORREF Lines[] = { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 255) };
    static const WCHAR *const Names[] = { L"R", L"G", L"B", L"W" };
    int Index, Line;

    Fill(hdc, 0, 0, Width, Height, RGB(0, 0, 0));
    for (Index = 0; Index < 4; Index++)
    {
        int Left = 10 + Index * (Width / 4);

        DrawSampleText(hdc, Left, 4, RGB(255, 255, 255), RGB(0, 0, 0), Names[Index]);
        for (Line = 0; Line < 8; Line++)
            Fill(hdc, Left, 24 + Line * 6, Left + Width / 4 - 20, 25 + Line * 6, Lines[Index]);
        for (Line = 0; Line < 8; Line++)
            Fill(hdc, Left + Line * 6, 80, Left + Line * 6 + 1, 140, Lines[Index]);
    }
    Fill(hdc, 0, 150, Width, Height, RGB(255, 255, 255));
    DrawSampleText(hdc, 10, 160, RGB(0, 0, 0), RGB(255, 255, 255), L"ClearType black on white: The quick brown fox 0123456789");
    DrawSampleText(hdc, 10, 190, RGB(255, 0, 0), RGB(255, 255, 255), L"Red on white: The quick brown fox");
    Fill(hdc, 0, 220, Width, Height, RGB(0, 0, 0));
    DrawSampleText(hdc, 10, 230, RGB(255, 255, 255), RGB(0, 0, 0), L"White on black: The quick brown fox 0123456789");
    DrawSampleText(hdc, 10, 260, RGB(0, 255, 0), RGB(0, 0, 0), L"Green on black: The quick brown fox");
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT Paint;
        RECT Client;
        HDC hdc = BeginPaint(hWnd, &Paint);

        GetClientRect(hWnd, &Client);
        if (s_bSubpixel)
            DrawSubpixel(hdc, Client.right, Client.bottom);
        else
            DrawPattern(hdc, Client.right, Client.bottom);
        EndPaint(hWnd, &Paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_TIMER:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, Message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    WNDCLASS Class;
    HWND hWnd;
    MSG Message;

    s_bSubpixel = pCommandLine && wcsstr(pCommandLine, L"subpixel") != NULL;
    memset(&Class, 0, sizeof(Class));
    Class.lpfnWndProc = WindowProc;
    Class.hInstance = hInstance;
    Class.lpszClassName = L"TestPattern";
    if (!RegisterClass(&Class))
        return 1;
    hWnd = CreateWindowEx(WS_EX_TOPMOST, L"TestPattern", L"Test Pattern", WS_POPUP | WS_VISIBLE, 0, 0,
                          GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInstance, NULL);
    if (!hWnd)
        return 1;
    SetTimer(hWnd, EXIT_TIMER, EXIT_MILLISECONDS, NULL);
    while (GetMessage(&Message, NULL, 0, 0))
    {
        TranslateMessage(&Message);
        DispatchMessage(&Message);
    }
    return 0;
}
