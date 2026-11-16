#include <windows.h>

#define LOG_PATH TEXT("\\Storage Card\\palette-match.log")
#define CELL 4
#define DEPTH_COUNT 4

static const WORD s_Depths[DEPTH_COUNT] = { 1, 2, 4, 8 };

static const COLORREF s_Colours[] = {
    RGB(255, 255, 255), RGB(0, 0, 0), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255),
    RGB(128, 128, 128), RGB(192, 192, 192), RGB(64, 64, 64), RGB(255, 255, 0), RGB(0, 255, 255),
    RGB(255, 0, 255), RGB(200, 40, 90), RGB(30, 160, 220), RGB(37, 91, 202), RGB(185, 199, 246),
    RGB(100, 150, 50),
};

#define COLOUR_COUNT (sizeof(s_Colours) / sizeof(s_Colours[0]))

static HANDLE s_hLog = INVALID_HANDLE_VALUE;

static void Log(const char *pText)
{
    DWORD Written;

    if (s_hLog != INVALID_HANDLE_VALUE)
        WriteFile(s_hLog, pText, strlen(pText), &Written, NULL);
}

static void LogNumber(unsigned long Value, int Base)
{
    static const char Digits[] = "0123456789abcdef";
    char Text[12];
    int Length = sizeof(Text) - 1;

    Text[Length] = 0;
    do
    {
        Text[--Length] = Digits[Value % Base];
        Value /= Base;
    } while (Value);
    Log(&Text[Length]);
}

static void PaletteEntry(int Index, RGBQUAD *pEntry)
{
    pEntry->rgbRed = (BYTE)(Index * 37);
    pEntry->rgbGreen = (BYTE)(Index * 91);
    pEntry->rgbBlue = (BYTE)(255 - Index * 53);
    pEntry->rgbReserved = 0;
}

static int ReadIndex(const BYTE *pRow, WORD Depth, int X)
{
    int Bit = X * Depth;
    int Shift = 8 - Depth - (Bit & 7);

    return (pRow[Bit >> 3] >> Shift) & ((1 << Depth) - 1);
}

static void MeasureDepth(HDC hScreen, HDC hMemory, WORD Depth)
{
    struct
    {
        BITMAPINFOHEADER Header;
        RGBQUAD Colors[256];
    } Info;
    int Colours = 1 << Depth, Width = COLOUR_COUNT * CELL, Index, Stride;
    HBITMAP hBitmap;
    HGDIOBJ hOld;
    BYTE *pBits;
    memset(&Info, 0, sizeof(Info));
    Info.Header.biSize = sizeof(Info.Header);
    Info.Header.biWidth = Width;
    Info.Header.biHeight = -CELL;
    Info.Header.biPlanes = 1;
    Info.Header.biBitCount = Depth;
    Info.Header.biCompression = BI_RGB;
    Info.Header.biClrUsed = Colours;
    for (Index = 0; Index < Colours; Index++)
        PaletteEntry(Index, &Info.Colors[Index]);
    hBitmap = CreateDIBSection(NULL, (BITMAPINFO *)&Info, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    if (!hBitmap)
    {
        Log("CreateDIBSection failed\r\n");
        return;
    }
    hOld = SelectObject(hMemory, hBitmap);
    BitBlt(hMemory, 0, 0, Width, CELL, hScreen, 0, 0, SRCCOPY);
    SelectObject(hMemory, hOld);
    Stride = ((Width * Depth + 31) / 32) * 4;
    Log("depth ");
    LogNumber(Depth, 10);
    Log(":");
    for (Index = 0; Index < (int)COLOUR_COUNT; Index++)
    {
        Log(" ");
        LogNumber(ReadIndex(pBits + Stride * (CELL / 2), Depth, Index * CELL + CELL / 2), 10);
    }
    Log("\r\n");
    DeleteObject(hBitmap);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    HWND hWindow;
    HDC hScreen, hMemory;
    int Index, Depth;

    hWindow = CreateWindowEx(WS_EX_TOPMOST, TEXT("STATIC"), TEXT(""), WS_POPUP | WS_VISIBLE, 0, 0, COLOUR_COUNT * CELL, CELL, NULL, NULL,
                             hInstance, NULL);
    if (!hWindow)
        return 1;
    s_hLog = CreateFile(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hScreen = GetDC(hWindow);
    hMemory = CreateCompatibleDC(hScreen);
    for (Index = 0; Index < (int)COLOUR_COUNT; Index++)
    {
        RECT Cell;
        HBRUSH hBrush = CreateSolidBrush(s_Colours[Index]);

        SetRect(&Cell, Index * CELL, 0, Index * CELL + CELL, CELL);
        FillRect(hScreen, &Cell, hBrush);
        DeleteObject(hBrush);
        Log("colour ");
        LogNumber(Index, 10);
        Log(": ");
        LogNumber(GetRValue(s_Colours[Index]), 10);
        Log(" ");
        LogNumber(GetGValue(s_Colours[Index]), 10);
        Log(" ");
        LogNumber(GetBValue(s_Colours[Index]), 10);
        Log(", screen ");
        LogNumber(GetPixel(hScreen, Index * CELL + 1, 1), 16);
        Log("\r\n");
    }
    Log("palette i: (i*37, i*91, 255-i*53) mod 256\r\n");
    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
        MeasureDepth(hScreen, hMemory, s_Depths[Depth]);
    Log("done\r\n");
    DeleteDC(hMemory);
    ReleaseDC(hWindow, hScreen);
    DestroyWindow(hWindow);
    if (s_hLog != INVALID_HANDLE_VALUE)
        CloseHandle(s_hLog);
    return 0;
}
