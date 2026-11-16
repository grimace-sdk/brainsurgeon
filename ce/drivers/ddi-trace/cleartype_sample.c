#include <windows.h>

#define MARK_ESCAPE 0x44444900
#define SAMPLE_PHASE 30
#define SAMPLE_WIDTH 400
#define SAMPLE_HEIGHT 300
#define OUTPUT_PATH TEXT("\\Storage Card\\cleartype-sample.bmp")
#define PAIR_COUNT 9

static BYTE s_MarkBuffer[256];

static const COLORREF s_Foregrounds[PAIR_COUNT] = {
    RGB(0, 0, 0), RGB(255, 255, 255), RGB(0, 0, 0), RGB(255, 255, 255), RGB(0, 0, 0), RGB(255, 255, 255), RGB(0, 0, 0),
    RGB(255, 255, 255), RGB(255, 0, 0),
};

static const COLORREF s_Backgrounds[PAIR_COUNT] = {
    RGB(255, 255, 255), RGB(0, 0, 0), RGB(64, 64, 64), RGB(64, 64, 64), RGB(128, 128, 128), RGB(128, 128, 128),
    RGB(192, 192, 192), RGB(192, 192, 192), RGB(255, 255, 255),
};

static void WriteBitmapTo(const TCHAR *pPath, const void *pBits);

static void WriteBitmap(const void *pBits)
{
    WriteBitmapTo(OUTPUT_PATH, pBits);
}

static void WriteBitmapTo(const TCHAR *pPath, const void *pBits)
{
    BITMAPFILEHEADER File;
    BITMAPINFOHEADER Header;
    HANDLE hFile;
    DWORD Written, Bytes = SAMPLE_WIDTH * SAMPLE_HEIGHT * 4;

    memset(&Header, 0, sizeof(Header));
    Header.biSize = sizeof(Header);
    Header.biWidth = SAMPLE_WIDTH;
    Header.biHeight = -SAMPLE_HEIGHT;
    Header.biPlanes = 1;
    Header.biBitCount = 32;
    Header.biCompression = BI_RGB;
    memset(&File, 0, sizeof(File));
    File.bfType = 0x4D42;
    File.bfOffBits = sizeof(File) + sizeof(Header);
    File.bfSize = File.bfOffBits + Bytes;
    hFile = CreateFile(pPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(hFile, &File, sizeof(File), &Written, NULL);
    WriteFile(hFile, &Header, sizeof(Header), &Written, NULL);
    WriteFile(hFile, pBits, Bytes, &Written, NULL);
    CloseHandle(hFile);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    BITMAPINFOHEADER Header;
    LOGFONT Font;
    HFONT hFont, hOldFont;
    HBITMAP hBitmap;
    HGDIOBJ hOldBitmap;
    HDC hScreen, hMemory;
    RECT Area;
    void *pBits;

    memset(&Header, 0, sizeof(Header));
    Header.biSize = sizeof(Header);
    Header.biWidth = SAMPLE_WIDTH;
    Header.biHeight = -SAMPLE_HEIGHT;
    Header.biPlanes = 1;
    Header.biBitCount = 32;
    Header.biCompression = BI_RGB;
    hBitmap = CreateDIBSection(NULL, (BITMAPINFO *)&Header, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBitmap)
        return 1;
    if (pCommandLine && wcsstr(pCommandLine, L"pairs"))
    {
        int Pair;

        hScreen = GetDC(NULL);
        hMemory = CreateCompatibleDC(hScreen);
        hOldBitmap = SelectObject(hMemory, hBitmap);
        for (Pair = 0; Pair < PAIR_COUNT; Pair++)
        {
            HBRUSH hBackground = CreateSolidBrush(s_Backgrounds[Pair]);
            TCHAR Path[MAX_PATH];
            int Size, Top = 2;

            SetRect(&Area, 0, 0, SAMPLE_WIDTH, SAMPLE_HEIGHT);
            FillRect(hMemory, &Area, hBackground);
            DeleteObject(hBackground);
            SetTextColor(hMemory, s_Foregrounds[Pair]);
            SetBkMode(hMemory, TRANSPARENT);
            for (Size = 9; Size <= 20; Size++)
            {
                WCHAR Characters[48];
                int Index;

                for (Index = 0; Index < 47; Index++)
                    Characters[Index] = (WCHAR)(48 + Index);
                Characters[47] = 0;
                memset(&Font, 0, sizeof(Font));
                Font.lfHeight = -Size;
                Font.lfQuality = CLEARTYPE_QUALITY;
                lstrcpy(Font.lfFaceName, L"Tahoma");
                hFont = CreateFontIndirect(&Font);
                hOldFont = (HFONT)SelectObject(hMemory, hFont);
                ExtTextOut(hMemory, 2, Top, 0, NULL, Characters, 47, NULL);
                SelectObject(hMemory, hOldFont);
                DeleteObject(hFont);
                Top += Size + 3;
            }
            wsprintf(Path, TEXT("\\Storage Card\\cleartype-pair%d.bmp"), Pair);
            WriteBitmapTo(Path, pBits);
        }
        SelectObject(hMemory, hOldBitmap);
        DeleteDC(hMemory);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);
        return 0;
    }
    hScreen = GetDC(NULL);
    hMemory = CreateCompatibleDC(hScreen);
    hOldBitmap = SelectObject(hMemory, hBitmap);
    SetRect(&Area, 0, 0, SAMPLE_WIDTH, SAMPLE_HEIGHT);
    FillRect(hMemory, &Area, (HBRUSH)GetStockObject(WHITE_BRUSH));
    memset(&Font, 0, sizeof(Font));
    Font.lfHeight = -13;
    Font.lfQuality = CLEARTYPE_QUALITY;
    lstrcpy(Font.lfFaceName, L"Tahoma");
    hFont = CreateFontIndirect(&Font);
    hOldFont = (HFONT)SelectObject(hMemory, hFont);
    SetTextColor(hMemory, RGB(0, 0, 0));
    SetBkMode(hMemory, TRANSPARENT);
    SelectObject(hMemory, hOldFont);
    DeleteObject(hFont);
    {
        WCHAR Characters[95];
        int Index, Size, Top = 2;

        for (Index = 0; Index < 94; Index++)
            Characters[Index] = (WCHAR)(33 + Index);
        Characters[94] = 0;
        ExtEscape(hScreen, MARK_ESCAPE, SAMPLE_PHASE, (LPCSTR)s_MarkBuffer, 0, NULL);
        for (Size = 9; Size <= 22 && Top < SAMPLE_HEIGHT - 24; Size += 1)
        {
            static const WCHAR *const Faces[] = { L"Tahoma", L"Courier New", L"Times New Roman" };

            Font.lfHeight = -Size;
            Font.lfWeight = Size & 1 ? FW_BOLD : FW_NORMAL;
            Font.lfItalic = (BYTE)((Size & 2) != 0);
            lstrcpy(Font.lfFaceName, Faces[Size % 3]);
            hFont = CreateFontIndirect(&Font);
            hOldFont = (HFONT)SelectObject(hMemory, hFont);
            for (Index = 0; Index < 94; Index += 32)
            {
                int Count = Index + 32 <= 94 ? 32 : 94 - Index;

                ExtTextOut(hMemory, 2 + (Size & 3), Top, 0, NULL, &Characters[Index], Count, NULL);
                Top += Size + 2;
            }
            SelectObject(hMemory, hOldFont);
            DeleteObject(hFont);
        }
        ExtEscape(hScreen, MARK_ESCAPE, 0, (LPCSTR)s_MarkBuffer, 0, NULL);
    }
    WriteBitmap(pBits);
    BitBlt(hScreen, 0, 0, SAMPLE_WIDTH, SAMPLE_HEIGHT, hMemory, 0, 0, SRCCOPY);
    SelectObject(hMemory, hOldBitmap);
    DeleteDC(hMemory);
    ReleaseDC(NULL, hScreen);
    DeleteObject(hBitmap);
    return 0;
}
