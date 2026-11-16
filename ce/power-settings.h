#ifndef PWSH2_BOOT_POWER_SETTINGS_H
#define PWSH2_BOOT_POWER_SETTINGS_H

#include <windows.h>

#define POWER_SETTINGS_FILE L"power.txt"
#define POWER_SOURCE_BATTERY 0
#define POWER_SOURCE_AC 1
#define POWER_SOURCE_COUNT 2
#define POWER_STEP_DIM 0
#define POWER_STEP_OFF 1
#define POWER_STEP_SLEEP 2
#define POWER_STEP_COUNT 3
#define POWER_NEVER 0

struct power_timeouts {
    DWORD seconds[POWER_SOURCE_COUNT][POWER_STEP_COUNT];
};

void power_timeouts_read(struct power_timeouts *timeouts);
BOOL power_timeouts_apply(const struct power_timeouts *timeouts);
BOOL power_settings_write(const WCHAR *battery, const struct power_timeouts *timeouts);

#endif
