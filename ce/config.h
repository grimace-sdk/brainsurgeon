#ifndef PWSH2_BOOT_CONFIG_H
#define PWSH2_BOOT_CONFIG_H

#include <windows.h>

#define CONFIG_VALUE_LENGTH MAX_PATH
#define CONFIG_FOLDER L"\\Storage Card\\pwsh2-boot"
#define CONFIG_PROFILE_LENGTH 32

struct config_entry {
    const WCHAR *key;
    WCHAR *value;
};

BOOL config_read(const WCHAR *path, const struct config_entry *entries, unsigned int count);
BOOL config_write(const WCHAR *path, const struct config_entry *entries, unsigned int count);
BOOL config_profile(WCHAR *profile);
BOOL config_profile_path(WCHAR *path, const WCHAR *name);
void config_settings_path(WCHAR *path, const WCHAR *name);
BOOL config_read_settings(const WCHAR *name, const struct config_entry *entries, unsigned int count);
BOOL config_write_settings(const WCHAR *name, const struct config_entry *entries, unsigned int count);

#endif
