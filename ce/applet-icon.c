#include "applet-icon.h"

HICON applet_icon(HINSTANCE module)
{
    WCHAR path[MAX_PATH];
    HICON icon = NULL;

    if (!GetModuleFileNameW(module, path, MAX_PATH))
        return NULL;
    ExtractIconExW(path, 0, &icon, NULL, 1);
    return icon;
}
