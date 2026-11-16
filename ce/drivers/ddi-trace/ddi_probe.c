#include <windows.h>

#define MARK_ESCAPE 0x44444900
#define LOG_PATH TEXT("\\Storage Card\\ddi-probe.log")
#define CAPTURE_FOLDER TEXT("\\Storage Card\\ddi-probe")
#define WINDOW_CLASS TEXT("DdiProbe")
#define SOURCE_SIZE 64
#define DEPTH_COUNT 7

typedef struct
{
    HWND hWindow;
    HDC hScreen;
    HDC hMemory;
    int Width;
    int Height;
    HBITMAP hDibs[DEPTH_COUNT];
    HBITMAP hCompatible;
    HBITMAP hMono;
} PROBE;

typedef void (*PHASE_FUNCTION)(PROBE *pProbe);

typedef struct
{
    const char *pName;
    PHASE_FUNCTION pFunction;
} PHASE;

static const WORD s_Depths[DEPTH_COUNT] = { 1, 2, 4, 8, 16, 24, 32 };
static BYTE s_MarkBuffer[256];
static HANDLE s_hLog = INVALID_HANDLE_VALUE;

static void Log(const char *pText)
{
    DWORD Written;

    if (s_hLog != INVALID_HANDLE_VALUE)
        WriteFile(s_hLog, pText, strlen(pText), &Written, NULL);
}

static void LogPhase(int Number, const char *pName)
{
    char Text[96];
    int Length = 0, Divisor;

    Text[Length++] = 'p';
    for (Divisor = 100; Divisor; Divisor /= 10)
        Text[Length++] = (char)('0' + Number / Divisor % 10);
    Text[Length++] = ' ';
    while (*pName && Length < (int)sizeof(Text) - 3)
        Text[Length++] = *pName++;
    Text[Length++] = '\r';
    Text[Length++] = '\n';
    Text[Length] = 0;
    Log(Text);
}

static void Mark(PROBE *pProbe, int Phase)
{
    ExtEscape(pProbe->hScreen, MARK_ESCAPE, Phase, (LPCSTR)s_MarkBuffer, 0, NULL);
}

static HBITMAP CreateDib(WORD Depth, int Width, int Height)
{
    struct
    {
        BITMAPINFOHEADER Header;
        RGBQUAD Colors[256];
    } Info;
    void *pBits;
    HBITMAP hBitmap;
    int Index, Colors = Depth <= 8 ? 1 << Depth : 0;
    BYTE *pBytes;
    int Bytes;

    memset(&Info, 0, sizeof(Info));
    Info.Header.biSize = sizeof(Info.Header);
    Info.Header.biWidth = Width;
    Info.Header.biHeight = Height;
    Info.Header.biPlanes = 1;
    Info.Header.biBitCount = Depth;
    Info.Header.biCompression = BI_RGB;
    Info.Header.biClrUsed = Colors;
    for (Index = 0; Index < Colors; Index++)
    {
        Info.Colors[Index].rgbRed = (BYTE)(Index * 37);
        Info.Colors[Index].rgbGreen = (BYTE)(Index * 91);
        Info.Colors[Index].rgbBlue = (BYTE)(255 - Index * 53);
    }
    hBitmap = CreateDIBSection(NULL, (BITMAPINFO *)&Info, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBitmap)
        return NULL;
    pBytes = (BYTE *)pBits;
    Bytes = ((Width * Depth + 31) / 32) * 4 * Height;
    for (Index = 0; Index < Bytes; Index++)
        pBytes[Index] = (BYTE)(Index * 29 + (Index >> 5));
    return hBitmap;
}

static void Capture(PROBE *pProbe, int Phase)
{
    BITMAPFILEHEADER File;
    BITMAPINFOHEADER Header;
    TCHAR Path[MAX_PATH];
    HBITMAP hBitmap, hOld;
    HANDLE hFile;
    void *pBits;
    DWORD Written, ImageBytes;

    memset(&Header, 0, sizeof(Header));
    Header.biSize = sizeof(Header);
    Header.biWidth = pProbe->Width;
    Header.biHeight = pProbe->Height;
    Header.biPlanes = 1;
    Header.biBitCount = 24;
    Header.biCompression = BI_RGB;
    ImageBytes = ((pProbe->Width * 3 + 3) & ~3) * pProbe->Height;
    hBitmap = CreateDIBSection(NULL, (BITMAPINFO *)&Header, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBitmap)
        return;
    hOld = (HBITMAP)SelectObject(pProbe->hMemory, hBitmap);
    BitBlt(pProbe->hMemory, 0, 0, pProbe->Width, pProbe->Height, pProbe->hScreen, 0, 0, SRCCOPY);
    SelectObject(pProbe->hMemory, hOld);
    wsprintf(Path, TEXT("%s\\p%03d.bmp"), CAPTURE_FOLDER, Phase);
    hFile = CreateFile(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        memset(&File, 0, sizeof(File));
        File.bfType = 0x4D42;
        File.bfOffBits = sizeof(File) + sizeof(Header);
        File.bfSize = File.bfOffBits + ImageBytes;
        WriteFile(hFile, &File, sizeof(File), &Written, NULL);
        WriteFile(hFile, &Header, sizeof(Header), &Written, NULL);
        WriteFile(hFile, pBits, ImageBytes, &Written, NULL);
        CloseHandle(hFile);
    }
    DeleteObject(hBitmap);
}

static void Clear(PROBE *pProbe)
{
    RECT Rectangle = { 0, 0, pProbe->Width, pProbe->Height };

    FillRect(pProbe->hScreen, &Rectangle, (HBRUSH)GetStockObject(WHITE_BRUSH));
}

static void Fills(PROBE *pProbe)
{
    static const DWORD Rops[] = { PATCOPY, PATINVERT, DSTINVERT, BLACKNESS, WHITENESS, 0x00A000C9, 0x005A0049 };
    HBRUSH hSolid = CreateSolidBrush(RGB(200, 40, 90));
    HBRUSH hPattern = CreatePatternBrush(pProbe->hMono);
    HBRUSH hColorPattern = CreatePatternBrush(pProbe->hCompatible);
    HBRUSH Brushes[3] = { hSolid, hPattern, hColorPattern };
    int Brush, Rop;

    for (Brush = 0; Brush < 3; Brush++)
    {
        SelectObject(pProbe->hScreen, Brushes[Brush]);
        for (Rop = 0; Rop < (int)(sizeof(Rops) / sizeof(Rops[0])); Rop++)
            PatBlt(pProbe->hScreen, 10 + Rop * 60, 10 + Brush * 60, 50, 50, Rops[Rop]);
    }
    SelectObject(pProbe->hScreen, GetStockObject(WHITE_BRUSH));
    DeleteObject(hSolid);
    DeleteObject(hPattern);
    DeleteObject(hColorPattern);
}

static void DibPatternBrushes(PROBE *pProbe)
{
    int Depth;

    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        struct
        {
            BITMAPINFOHEADER Header;
            RGBQUAD Colors[256];
            BYTE Bits[8 * 8 * 4];
        } Packed;
        HBRUSH hBrush;
        int Index;

        memset(&Packed, 0, sizeof(Packed));
        Packed.Header.biSize = sizeof(Packed.Header);
        Packed.Header.biWidth = 8;
        Packed.Header.biHeight = 8;
        Packed.Header.biPlanes = 1;
        Packed.Header.biBitCount = s_Depths[Depth];
        Packed.Header.biCompression = BI_RGB;
        Packed.Header.biClrUsed = s_Depths[Depth] <= 8 ? 1 << s_Depths[Depth] : 0;
        for (Index = 0; Index < 256; Index++)
            Packed.Colors[Index].rgbGreen = (BYTE)(Index * 7);
        for (Index = 0; Index < (int)sizeof(Packed.Bits); Index++)
            Packed.Bits[Index] = (BYTE)(Index * 13);
        if (Packed.Header.biClrUsed < 256)
            memmove((BYTE *)Packed.Colors + Packed.Header.biClrUsed * sizeof(RGBQUAD), Packed.Bits, sizeof(Packed.Bits));
        hBrush = CreateDIBPatternBrushPt(&Packed, DIB_RGB_COLORS);
        if (!hBrush)
            continue;
        SelectObject(pProbe->hScreen, hBrush);
        PatBlt(pProbe->hScreen, 10 + Depth * 60, 200, 50, 50, PATCOPY);
        SelectObject(pProbe->hScreen, GetStockObject(WHITE_BRUSH));
        DeleteObject(hBrush);
    }
}

static void AllRops(PROBE *pProbe)
{
    HBRUSH hBrush = CreateSolidBrush(RGB(30, 160, 220));
    HGDIOBJ hOld = SelectObject(pProbe->hMemory, pProbe->hCompatible);
    int Rop;

    SelectObject(pProbe->hScreen, hBrush);
    for (Rop = 0; Rop < 256; Rop++)
        BitBlt(pProbe->hScreen, (Rop % 16) * 48, (Rop / 16) * 28, 40, 24, pProbe->hMemory, Rop % 8, Rop % 5, ((DWORD)Rop << 16) | 0x0001);
    SelectObject(pProbe->hScreen, GetStockObject(WHITE_BRUSH));
    SelectObject(pProbe->hMemory, hOld);
    DeleteObject(hBrush);
}

static void FromDibs(PROBE *pProbe)
{
    int Depth;

    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        HGDIOBJ hOld;

        if (!pProbe->hDibs[Depth])
            continue;
        hOld = SelectObject(pProbe->hMemory, pProbe->hDibs[Depth]);
        BitBlt(pProbe->hScreen, 10 + Depth * 70, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCCOPY);
        BitBlt(pProbe->hScreen, 10 + Depth * 70, 90, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCAND);
        BitBlt(pProbe->hScreen, 10 + Depth * 70, 170, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCINVERT);
        SelectObject(pProbe->hMemory, hOld);
    }
}

static void ToDibs(PROBE *pProbe)
{
    int Depth;

    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        HGDIOBJ hOld;

        if (!pProbe->hDibs[Depth])
            continue;
        hOld = SelectObject(pProbe->hMemory, pProbe->hDibs[Depth]);
        BitBlt(pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, pProbe->hScreen, 100, 100, SRCCOPY);
        PatBlt(pProbe->hMemory, 4, 4, 20, 20, PATINVERT);
        SelectObject(pProbe->hMemory, hOld);
    }
}

static void DibToDib(PROBE *pProbe)
{
    HDC hOther = CreateCompatibleDC(pProbe->hScreen);
    int Source, Target;

    for (Source = 0; Source < DEPTH_COUNT; Source++)
    {
        for (Target = 0; Target < DEPTH_COUNT; Target++)
        {
            HGDIOBJ hOldSource, hOldTarget;

            if (!pProbe->hDibs[Source] || !pProbe->hDibs[Target] || Source == Target)
                continue;
            hOldSource = SelectObject(pProbe->hMemory, pProbe->hDibs[Source]);
            hOldTarget = SelectObject(hOther, pProbe->hDibs[Target]);
            BitBlt(hOther, 0, 0, 32, 32, pProbe->hMemory, 8, 8, SRCCOPY);
            SelectObject(hOther, hOldTarget);
            SelectObject(pProbe->hMemory, hOldSource);
        }
    }
    DeleteDC(hOther);
}

static void Stretches(PROBE *pProbe)
{
    static const int Modes[] = { BLACKONWHITE, WHITEONBLACK, COLORONCOLOR, HALFTONE };
    int Mode, Depth;

    for (Mode = 0; Mode < 4; Mode++)
    {
        SetStretchBltMode(pProbe->hScreen, Modes[Mode]);
        for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
        {
            HGDIOBJ hOld;
            int X = 10 + Depth * 110, Y = 10 + Mode * 110;

            if (!pProbe->hDibs[Depth])
                continue;
            hOld = SelectObject(pProbe->hMemory, pProbe->hDibs[Depth]);
            StretchBlt(pProbe->hScreen, X, Y, 100, 100, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, SRCCOPY);
            StretchBlt(pProbe->hScreen, X, Y, 30, 30, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, SRCCOPY);
            StretchBlt(pProbe->hScreen, X + 100, Y, -60, 60, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, SRCCOPY);
            StretchBlt(pProbe->hScreen, X, Y + 100, 60, -60, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, SRCINVERT);
            SelectObject(pProbe->hMemory, hOld);
        }
    }
    SetStretchBltMode(pProbe->hScreen, COLORONCOLOR);
}

static void DeviceIndependent(PROBE *pProbe)
{
    int Depth;

    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        BITMAP Bitmap;
        DIBSECTION Section;

        if (!pProbe->hDibs[Depth] || !GetObject(pProbe->hDibs[Depth], sizeof(Section), &Section))
            continue;
        Bitmap = Section.dsBm;
        {
            struct
            {
                BITMAPINFOHEADER Header;
                RGBQUAD Colors[256];
            } Info;
            int Index;

            memset(&Info, 0, sizeof(Info));
            Info.Header = Section.dsBmih;
            for (Index = 0; Index < 256; Index++)
                Info.Colors[Index].rgbRed = (BYTE)Index;
            SetDIBitsToDevice(pProbe->hScreen, 10 + Depth * 70, 10, SOURCE_SIZE, SOURCE_SIZE, 0, 0, 0, SOURCE_SIZE, Bitmap.bmBits,
                              (BITMAPINFO *)&Info, DIB_RGB_COLORS);
            StretchDIBits(pProbe->hScreen, 10 + Depth * 110, 100, 100, 100, 0, 0, SOURCE_SIZE, SOURCE_SIZE, Bitmap.bmBits,
                          (BITMAPINFO *)&Info, DIB_RGB_COLORS, SRCCOPY);
        }
    }
}

static void Masks(PROBE *pProbe)
{
    HGDIOBJ hOld = SelectObject(pProbe->hMemory, pProbe->hCompatible);
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 200, 0));

    SelectObject(pProbe->hScreen, hBrush);
    MaskBlt(pProbe->hScreen, 10, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, pProbe->hMono, 0, 0, MAKEROP4(SRCCOPY, PATCOPY));
    MaskBlt(pProbe->hScreen, 90, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, pProbe->hMono, 0, 0, MAKEROP4(SRCINVERT, 0x00AA0029));
    SelectObject(pProbe->hScreen, GetStockObject(WHITE_BRUSH));
    SelectObject(pProbe->hMemory, hOld);
    DeleteObject(hBrush);
}

static void Transparent(PROBE *pProbe)
{
    int Depth;

    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        HGDIOBJ hOld;

        if (!pProbe->hDibs[Depth])
            continue;
        hOld = SelectObject(pProbe->hMemory, pProbe->hDibs[Depth]);
        TransparentImage(pProbe->hScreen, 10 + Depth * 70, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE,
                         RGB(0, 0, 0));
        TransparentImage(pProbe->hScreen, 10 + Depth * 110, 100, 100, 100, pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, RGB(0, 0, 0));
        SelectObject(pProbe->hMemory, hOld);
        TransparentImage(pProbe->hScreen, 10 + Depth * 70, 250, SOURCE_SIZE, SOURCE_SIZE, pProbe->hDibs[Depth], 0, 0, SOURCE_SIZE, SOURCE_SIZE,
                         RGB(0, 0, 0));
    }
}

static void Gradients(PROBE *pProbe)
{
    TRIVERTEX Vertices[3] = {
        { 10, 10, 0xff00, 0x0000, 0x0000, 0 },
        { 400, 200, 0x0000, 0x0000, 0xff00, 0 },
        { 10, 400, 0x0000, 0xff00, 0x0000, 0 },
    };
    GRADIENT_RECT Rectangle = { 0, 1 };
    GRADIENT_TRIANGLE Triangle = { 0, 1, 2 };

    GradientFill(pProbe->hScreen, Vertices, 2, &Rectangle, 1, GRADIENT_FILL_RECT_H);
    GradientFill(pProbe->hScreen, Vertices, 2, &Rectangle, 1, GRADIENT_FILL_RECT_V);
    GradientFill(pProbe->hScreen, Vertices, 3, &Triangle, 1, GRADIENT_FILL_TRIANGLE);
}

static void Lines(PROBE *pProbe)
{
    static const int Styles[] = { PS_SOLID, PS_DASH, PS_NULL };
    static const int Widths[] = { 0, 1, 3, 9 };
    int Style, Width, Mix;

    for (Mix = R2_BLACK; Mix <= R2_WHITE; Mix++)
    {
        SetROP2(pProbe->hScreen, Mix);
        for (Style = 0; Style < 3; Style++)
        {
            for (Width = 0; Width < 4; Width++)
            {
                HPEN hPen = CreatePen(Styles[Style], Widths[Width], RGB(Mix * 15, 0, 255 - Mix * 15));
                POINT Points[4] = { { 10, 10 + Mix * 25 }, { 200, 20 + Mix * 25 }, { 210, 30 + Mix * 25 }, { 400 + Width * 50, 12 + Mix * 25 } };
                HGDIOBJ hOld = SelectObject(pProbe->hScreen, hPen);

                MoveToEx(pProbe->hScreen, 450, 10 + Mix * 25, NULL);
                LineTo(pProbe->hScreen, 790, 10 + Mix * 25);
                MoveToEx(pProbe->hScreen, 450 + Width * 30, 10, NULL);
                LineTo(pProbe->hScreen, 450 + Width * 30, 470);
                Polyline(pProbe->hScreen, Points, 4);
                SelectObject(pProbe->hScreen, hOld);
                DeleteObject(hPen);
            }
        }
    }
    SetROP2(pProbe->hScreen, R2_COPYPEN);
}

static void Shapes(PROBE *pProbe)
{
    HBRUSH hSolid = CreateSolidBrush(RGB(250, 200, 0));
    HBRUSH hPattern = CreatePatternBrush(pProbe->hMono);
    HBRUSH Brushes[3] = { hSolid, hPattern, (HBRUSH)GetStockObject(NULL_BRUSH) };
    HPEN hThick = CreatePen(PS_SOLID, 5, RGB(0, 0, 0));
    HPEN Pens[3] = { (HPEN)GetStockObject(BLACK_PEN), hThick, (HPEN)GetStockObject(NULL_PEN) };
    int Brush, Pen, Mix;

    for (Mix = R2_BLACK; Mix <= R2_WHITE; Mix += 3)
    {
        SetROP2(pProbe->hScreen, Mix);
        for (Brush = 0; Brush < 3; Brush++)
        {
            for (Pen = 0; Pen < 3; Pen++)
            {
                int X = 10 + (Brush * 3 + Pen) * 85, Y = 10 + (Mix / 3) * 80;
                POINT Points[5] = { { X, Y + 70 }, { X + 40, Y }, { X + 80, Y + 70 }, { X, Y + 25 }, { X + 80, Y + 25 } };

                SelectObject(pProbe->hScreen, Brushes[Brush]);
                SelectObject(pProbe->hScreen, Pens[Pen]);
                Rectangle(pProbe->hScreen, X, Y, X + 30, Y + 30);
                RoundRect(pProbe->hScreen, X + 35, Y, X + 75, Y + 30, 10, 10);
                Ellipse(pProbe->hScreen, X, Y + 35, X + 40, Y + 70);
                Polygon(pProbe->hScreen, Points, 5);
            }
        }
    }
    SetROP2(pProbe->hScreen, R2_COPYPEN);
    SelectObject(pProbe->hScreen, GetStockObject(BLACK_PEN));
    SelectObject(pProbe->hScreen, GetStockObject(WHITE_BRUSH));
    DeleteObject(hSolid);
    DeleteObject(hPattern);
    DeleteObject(hThick);
}

static void Text(PROBE *pProbe)
{
    static const BYTE Qualities[] = { DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY };
    static const LONG Escapements[] = { 0, 900, 2700 };
    int Quality, Escapement, Opaque;

    for (Quality = 0; Quality < 4; Quality++)
    {
        for (Escapement = 0; Escapement < 3; Escapement++)
        {
            for (Opaque = 0; Opaque < 2; Opaque++)
            {
                LOGFONTW Font;
                HFONT hFont;
                HGDIOBJ hOld;
                RECT Clip = { 0, 0, 600, 400 };
                int X = 20 + Quality * 190 + Opaque * 90, Y = 200 + Escapement * 80;

                memset(&Font, 0, sizeof(Font));
                Font.lfHeight = Opaque ? -28 : -14;
                Font.lfWeight = Opaque ? FW_BOLD : FW_NORMAL;
                Font.lfQuality = Qualities[Quality];
                Font.lfEscapement = Escapements[Escapement];
                Font.lfOrientation = Escapements[Escapement];
                hFont = CreateFontIndirectW(&Font);
                hOld = SelectObject(pProbe->hScreen, hFont);
                SetBkMode(pProbe->hScreen, Opaque ? OPAQUE : TRANSPARENT);
                SetBkColor(pProbe->hScreen, RGB(220, 220, 255));
                SetTextColor(pProbe->hScreen, RGB(Quality * 60, 0, 0));
                ExtTextOutW(pProbe->hScreen, X, Y, (Opaque ? ETO_OPAQUE : 0) | ETO_CLIPPED, &Clip, L"Probe Ag", 8, NULL);
                DrawTextW(pProbe->hScreen, L"Wrapped text in a box", -1, &Clip, DT_WORDBREAK | DT_CENTER);
                SelectObject(pProbe->hScreen, hOld);
                DeleteObject(hFont);
            }
        }
    }
    SetBkMode(pProbe->hScreen, OPAQUE);
}

static void Regions(PROBE *pProbe)
{
    HRGN hClip = CreateRectRgn(50, 50, 300, 300);
    HRGN hSecond = CreateRectRgn(200, 200, 600, 400);
    HRGN hFill = CreateRectRgn(0, 0, 1, 1);
    HBRUSH hBrush = CreateSolidBrush(RGB(120, 0, 200));
    HGDIOBJ hOld = SelectObject(pProbe->hMemory, pProbe->hCompatible);

    CombineRgn(hFill, hClip, hSecond, RGN_XOR);
    FillRgn(pProbe->hScreen, hFill, hBrush);
    SelectClipRgn(pProbe->hScreen, hFill);
    BitBlt(pProbe->hScreen, 0, 0, 700, 450, pProbe->hMemory, 0, 0, SRCCOPY);
    PatBlt(pProbe->hScreen, 0, 0, 700, 450, DSTINVERT);
    ExtTextOutW(pProbe->hScreen, 60, 60, 0, NULL, L"Clipped", 7, NULL);
    SelectClipRgn(pProbe->hScreen, NULL);
    SelectObject(pProbe->hMemory, hOld);
    DeleteObject(hBrush);
    DeleteObject(hFill);
    DeleteObject(hSecond);
    DeleteObject(hClip);
}

static void Scrolling(PROBE *pProbe)
{
    RECT Area = { 0, 0, pProbe->Width, pProbe->Height };
    int Step;

    for (Step = 0; Step < 4; Step++)
    {
        ScrollDC(pProbe->hScreen, 0, -16, &Area, &Area, NULL, NULL);
        ScrollDC(pProbe->hScreen, 16, 0, &Area, &Area, NULL, NULL);
        ScrollWindowEx(pProbe->hWindow, 0, 8, NULL, NULL, NULL, NULL, SW_INVALIDATE);
    }
    ValidateRect(pProbe->hWindow, NULL);
}

static void Palettes(PROBE *pProbe)
{
    struct
    {
        WORD Version;
        WORD Count;
        PALETTEENTRY Entries[16];
    } Logical;
    HPALETTE hPalette, hOld;
    RGBQUAD Colors[16];
    int Index;
    HGDIOBJ hOldBitmap;

    Logical.Version = 0x300;
    Logical.Count = 16;
    for (Index = 0; Index < 16; Index++)
    {
        Logical.Entries[Index].peRed = (BYTE)(Index * 16);
        Logical.Entries[Index].peGreen = (BYTE)(255 - Index * 16);
        Logical.Entries[Index].peBlue = 128;
        Logical.Entries[Index].peFlags = 0;
        Colors[Index].rgbRed = (BYTE)(255 - Index * 16);
        Colors[Index].rgbGreen = 0;
        Colors[Index].rgbBlue = (BYTE)(Index * 16);
        Colors[Index].rgbReserved = 0;
    }
    hPalette = CreatePalette((LOGPALETTE *)&Logical);
    hOld = SelectPalette(pProbe->hScreen, hPalette, FALSE);
    RealizePalette(pProbe->hScreen);
    if (pProbe->hDibs[3])
    {
        hOldBitmap = SelectObject(pProbe->hMemory, pProbe->hDibs[3]);
        SetDIBColorTable(pProbe->hMemory, 0, 16, Colors);
        BitBlt(pProbe->hScreen, 10, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCCOPY);
        SelectObject(pProbe->hMemory, hOldBitmap);
    }
    SelectPalette(pProbe->hScreen, hOld, FALSE);
    RealizePalette(pProbe->hScreen);
    DeleteObject(hPalette);
}

static void Cursors(PROBE *pProbe)
{
    int Step;

    ShowCursor(TRUE);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    for (Step = 0; Step < 10; Step++)
    {
        SetCursorPos(100 + Step * 50, 100 + Step * 30);
        Sleep(20);
    }
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    Sleep(200);
    SetCursor(NULL);
    ShowCursor(FALSE);
}

static void Controls(PROBE *pProbe)
{
    RECT Box = { 20, 20, 120, 60 };
    int Index;

    for (Index = 0; Index < 6; Index++)
    {
        OffsetRect(&Box, 0, 60);
        DrawEdge(pProbe->hScreen, &Box, Index & 1 ? EDGE_RAISED : EDGE_SUNKEN, BF_RECT);
        DrawFrameControl(pProbe->hScreen, &Box, DFC_BUTTON, Index & 1 ? DFCS_BUTTONCHECK | DFCS_CHECKED : DFCS_BUTTONPUSH);
        DrawFocusRect(pProbe->hScreen, &Box);
        InvertRect(pProbe->hScreen, &Box);
    }
    DrawIconEx(pProbe->hScreen, 300, 100, LoadIconW(NULL, IDI_APPLICATION), 32, 32, 0, NULL, DI_NORMAL);
    DrawIconEx(pProbe->hScreen, 340, 100, LoadIconW(NULL, IDI_APPLICATION), 64, 64, 0, NULL, DI_NORMAL);
}

static void Pixels(PROBE *pProbe)
{
    int Index;
    COLORREF Color = 0;
    HGDIOBJ hOld = SelectObject(pProbe->hMemory, pProbe->hCompatible);

    for (Index = 0; Index < 64; Index++)
    {
        SetPixel(pProbe->hScreen, 10 + Index, 10 + Index, RGB(Index * 4, 0, 0));
        Color ^= GetPixel(pProbe->hScreen, 10 + Index, 10);
        SetPixel(pProbe->hMemory, Index % SOURCE_SIZE, Index / 2, Color);
        Color ^= GetPixel(pProbe->hMemory, Index % SOURCE_SIZE, 0);
    }
    SelectObject(pProbe->hMemory, hOld);
}

static void Bitmaps(PROBE *pProbe)
{
    static BYTE Bits[SOURCE_SIZE * SOURCE_SIZE * 2];
    HBITMAP hMono, hColor;
    HGDIOBJ hOld;
    int Index;

    for (Index = 0; Index < (int)sizeof(Bits); Index++)
        Bits[Index] = (BYTE)(Index * 3);
    hMono = CreateBitmap(SOURCE_SIZE, SOURCE_SIZE, 1, 1, Bits);
    hColor = CreateCompatibleBitmap(pProbe->hScreen, SOURCE_SIZE, SOURCE_SIZE);
    SetBitmapBits(hColor, sizeof(Bits), Bits);
    hOld = SelectObject(pProbe->hMemory, hMono);
    BitBlt(pProbe->hScreen, 10, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCCOPY);
    SelectObject(pProbe->hMemory, hColor);
    BitBlt(pProbe->hScreen, 90, 10, SOURCE_SIZE, SOURCE_SIZE, pProbe->hMemory, 0, 0, SRCCOPY);
    BitBlt(pProbe->hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, pProbe->hScreen, 0, 0, SRCCOPY);
    SelectObject(pProbe->hMemory, hOld);
    DeleteObject(hMono);
    DeleteObject(hColor);
}

static void Rotation(PROBE *pProbe)
{
    static const DWORD Orientations[] = { DMDO_90, DMDO_180, DMDO_270, DMDO_0 };
    DEVMODEW Mode;
    int Index;

    for (Index = 0; Index < 4; Index++)
    {
        memset(&Mode, 0, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        Mode.dmFields = DM_DISPLAYORIENTATION;
        Mode.dmDisplayOrientation = Orientations[Index];
        ChangeDisplaySettingsEx(NULL, &Mode, NULL, CDS_RESET, NULL);
        Sleep(500);
    }
}

static const PHASE s_Phases[] = {
    { "PatBlt solid, mono and colour pattern brushes, 7 ROPs", Fills },
    { "PatBlt DIB pattern brushes 1-32 bpp", DibPatternBrushes },
    { "BitBlt compatible bitmap to screen, all 256 ROP3s with a solid brush", AllRops },
    { "BitBlt DIB sections 1-32 bpp to screen, SRCCOPY SRCAND SRCINVERT", FromDibs },
    { "BitBlt screen to DIB sections 1-32 bpp, PatBlt on DIB", ToDibs },
    { "BitBlt DIB section to DIB section, all depth pairs", DibToDib },
    { "StretchBlt 4 stretch modes, grow, shrink, mirror, 1-32 bpp", Stretches },
    { "SetDIBitsToDevice and StretchDIBits 1-32 bpp", DeviceIndependent },
    { "MaskBlt", Masks },
    { "TransparentImage 1-32 bpp, from DC and from bitmap", Transparent },
    { "GradientFill rect H, rect V, triangle", Gradients },
    { "Lines: 16 ROP2s, solid dash null pens, widths 0 1 3 9", Lines },
    { "Rectangle RoundRect Ellipse Polygon, brushes, pens, ROP2s", Shapes },
    { "Text: 4 qualities, 3 escapements, opaque and transparent", Text },
    { "Complex clip regions: FillRgn, BitBlt, PatBlt, text", Regions },
    { "ScrollDC and ScrollWindowEx", Scrolling },
    { "Palettes and SetDIBColorTable", Palettes },
    { "Cursor shown, moved, changed, hidden", Cursors },
    { "DrawEdge DrawFrameControl DrawFocusRect InvertRect DrawIconEx", Controls },
    { "SetPixel GetPixel on screen and bitmap", Pixels },
    { "CreateBitmap mono, CreateCompatibleBitmap, SetBitmapBits, blits", Bitmaps },
    { "Rotation 90 180 270 0 with ChangeDisplaySettingsEx", Rotation },
};

static LRESULT CALLBACK WindowProcedure(HWND hWindow, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_PAINT)
    {
        ValidateRect(hWindow, NULL);
        return 0;
    }
    return DefWindowProc(hWindow, Message, wParam, lParam);
}

static void Pump(void)
{
    MSG Message;

    while (PeekMessage(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessage(&Message);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    WNDCLASS Class;
    PROBE Probe;
    int Phase, Depth;
    BOOL bCapture = pCommandLine && wcsstr(pCommandLine, L"capture") != NULL;
    BOOL bSkipRotation = pCommandLine && wcsstr(pCommandLine, L"norotate") != NULL;

    memset(&Class, 0, sizeof(Class));
    Class.lpfnWndProc = WindowProcedure;
    Class.hInstance = hInstance;
    Class.lpszClassName = WINDOW_CLASS;
    Class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClass(&Class);
    memset(&Probe, 0, sizeof(Probe));
    Probe.Width = GetSystemMetrics(SM_CXSCREEN);
    Probe.Height = GetSystemMetrics(SM_CYSCREEN);
    Probe.hWindow = CreateWindowEx(WS_EX_TOPMOST, WINDOW_CLASS, TEXT("ddi-probe"), WS_POPUP | WS_VISIBLE, 0, 0, Probe.Width, Probe.Height,
                                   NULL, NULL, hInstance, NULL);
    if (!Probe.hWindow)
        return 1;
    Pump();
    if (bCapture)
        CreateDirectory(CAPTURE_FOLDER, NULL);
    s_hLog = CreateFile(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Probe.hScreen = GetDC(Probe.hWindow);
    Probe.hMemory = CreateCompatibleDC(Probe.hScreen);
    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
        Probe.hDibs[Depth] = CreateDib(s_Depths[Depth], SOURCE_SIZE, SOURCE_SIZE);
    Probe.hCompatible = CreateCompatibleBitmap(Probe.hScreen, SOURCE_SIZE, SOURCE_SIZE);
    Probe.hMono = CreateBitmap(8, 8, 1, 1, "\x55\x00\xaa\x00\x55\x00\xaa\x00\x55\x00\xaa\x00\x55\x00\xaa\x00");
    {
        HGDIOBJ hOld = SelectObject(Probe.hMemory, Probe.hCompatible);

        BitBlt(Probe.hMemory, 0, 0, SOURCE_SIZE, SOURCE_SIZE, Probe.hScreen, 0, 0, SRCCOPY);
        Ellipse(Probe.hMemory, 4, 4, 60, 60);
        SelectObject(Probe.hMemory, hOld);
    }
    for (Phase = 0; Phase < (int)(sizeof(s_Phases) / sizeof(s_Phases[0])); Phase++)
    {
        if (bSkipRotation && s_Phases[Phase].pFunction == Rotation)
            continue;
        LogPhase(Phase + 1, s_Phases[Phase].pName);
        Clear(&Probe);
        Mark(&Probe, Phase + 1);
        s_Phases[Phase].pFunction(&Probe);
        Mark(&Probe, 0);
        Pump();
        Sleep(300);
        if (bCapture)
        {
            Probe.Width = GetSystemMetrics(SM_CXSCREEN);
            Probe.Height = GetSystemMetrics(SM_CYSCREEN);
            Capture(&Probe, Phase + 1);
        }
    }
    Log("done\r\n");
    for (Depth = 0; Depth < DEPTH_COUNT; Depth++)
    {
        if (Probe.hDibs[Depth])
            DeleteObject(Probe.hDibs[Depth]);
    }
    DeleteObject(Probe.hCompatible);
    DeleteObject(Probe.hMono);
    DeleteDC(Probe.hMemory);
    ReleaseDC(Probe.hWindow, Probe.hScreen);
    DestroyWindow(Probe.hWindow);
    if (s_hLog != INVALID_HANDLE_VALUE)
        CloseHandle(s_hLog);
    return 0;
}
