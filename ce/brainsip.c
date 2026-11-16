#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

BOOL WINAPI SipGetCurrentIM(void *input_method)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI SipSetCurrentIM(void *input_method)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI SipShowIM(DWORD flags)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
