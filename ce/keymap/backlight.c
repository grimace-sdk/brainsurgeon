#include "backlight.h"

#define SUB_ACTIVE_EVENT L"Display_Event_Backlight_SubActive"
#define SUB_UPDATE_EVENT L"Display_Event_Backlight_SubUpdate"
#define UPDATE_EVENT L"Display_Event_Backlight_Update"
#define SLD_DEVICE L"SLD1:"
#define SLD_IOCTL_GET_INFO 0x100
#define SLD_IOCTL_SET_BACKLIGHT 0x102
#define SLD_IOCTL_GET_BACKLIGHT 0x103
#define SLD_INFO_MAGIC 0x32444C53
#define SLD_CAPS_BACKLIGHT_LEVEL 0x00000002

struct sld_info {
    DWORD magic;
    DWORD version;
    DWORD capabilities;
};

static void signal_named_event(const WCHAR *name)
{
    HANDLE event = OpenEventW(EVENT_ALL_ACCESS, FALSE, name);

    if (!event)
        return;
    SetEvent(event);
    CloseHandle(event);
}

static HANDLE open_dimmable_driver(void)
{
    HANDLE device = CreateFileW(SLD_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    struct sld_info info = { 0, 0, 0 };
    DWORD returned = 0;

    if (device == INVALID_HANDLE_VALUE)
        return NULL;
    if (DeviceIoControl(device, SLD_IOCTL_GET_INFO, NULL, 0, &info, sizeof(info), &returned, NULL)
        && returned >= sizeof(info) && info.magic == SLD_INFO_MAGIC && (info.capabilities & SLD_CAPS_BACKLIGHT_LEVEL))
        return device;
    CloseHandle(device);
    return NULL;
}

BOOL sublcd_set_backlight(BOOL on)
{
    HANDLE active = OpenEventW(EVENT_ALL_ACCESS, FALSE, SUB_ACTIVE_EVENT);

    if (!active)
        return FALSE;
    if (on)
        SetEvent(active);
    else
        ResetEvent(active);
    CloseHandle(active);
    signal_named_event(SUB_UPDATE_EVENT);
    signal_named_event(UPDATE_EVENT);
    return TRUE;
}

int sublcd_backlight_level(void)
{
    HANDLE device = open_dimmable_driver();
    DWORD level = 0, returned = 0;

    if (!device)
        return 0;
    if (!DeviceIoControl(device, SLD_IOCTL_GET_BACKLIGHT, NULL, 0, &level, sizeof(level), &returned, NULL) || returned < sizeof(level))
        level = 0;
    CloseHandle(device);
    return (int)level;
}

BOOL sublcd_set_backlight_level(int percent)
{
    HANDLE device = open_dimmable_driver();
    DWORD level = (DWORD)(percent < 1 ? 1 : percent > 100 ? 100 : percent), returned;
    BOOL result;

    if (!device)
        return FALSE;
    result = DeviceIoControl(device, SLD_IOCTL_SET_BACKLIGHT, &level, sizeof(level), NULL, 0, &returned, NULL);
    CloseHandle(device);
    return result;
}
