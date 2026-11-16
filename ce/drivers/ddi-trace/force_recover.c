#include <windows.h>

#define FORCE_RECOVER_ESCAPE 0x42524543

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    DWORD Level = pCommandLine ? wcstoul(pCommandLine, NULL, 10) : 0;
    HDC hScreen;
    int Result;

    if (Level < 1 || Level > 4)
        return 2;
    hScreen = GetDC(NULL);
    Result = ExtEscape(hScreen, FORCE_RECOVER_ESCAPE, sizeof(Level), (LPCSTR)&Level, 0, NULL);
    ReleaseDC(NULL, hScreen);
    return Result == 0 ? 0 : 1;
}
