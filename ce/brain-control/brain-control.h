#ifndef BRAIN_CONTROL_H
#define BRAIN_CONTROL_H

#include <windows.h>

#define MODEL_UNKNOWN 0x00
#define MODEL_PWSH2 0x01
#define MODEL_PWSB1 0x02
#define MODEL_PWSH4 0x04
#define MODEL_PWA7300 0x08
#define MODEL_ALL (MODEL_PWSH2 | MODEL_PWSB1 | MODEL_PWSH4 | MODEL_PWA7300)

#define BRAIN_KEY L"Software\\Brain"
#define BRAIN_MODULES_KEY L"Software\\Brain\\Modules"
#define TEXT_LENGTH 128

DWORD model_detect(void);
DWORD model_current(void);

DWORD registry_read_dword(HKEY root, const WCHAR *key_name, const WCHAR *name, DWORD fallback);
BOOL registry_write_dwords(HKEY root, const WCHAR *key_name, const WCHAR *const *names, const DWORD *values, unsigned int count);
BOOL registry_read_string(HKEY root, const WCHAR *key_name, const WCHAR *name, WCHAR *text, DWORD characters);
HANDLE device_open(const WCHAR *name);
BOOL device_present(const WCHAR *name);

BOOL display_page_available(void);
BOOL CALLBACK display_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void display_page_save(HWND page);
void display_page_revert(HWND page);

BOOL CALLBACK rotation_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void rotation_page_save(HWND page);

BOOL night_light_page_available(void);
BOOL CALLBACK night_light_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void night_light_page_save(HWND page);
void night_light_page_revert(HWND page);
void night_light_page_shown(HWND page, BOOL visible);

BOOL touchpad_page_available(void);
BOOL CALLBACK touchpad_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void touchpad_page_save(HWND page);
void touchpad_page_revert(HWND page);

BOOL CALLBACK sound_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void sound_page_save(HWND page);
void sound_page_revert(HWND page);

BOOL CALLBACK power_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void power_page_save(HWND page);
void power_page_revert(HWND page);
void power_page_shown(HWND page, BOOL visible);

BOOL cpu_page_available(void);
BOOL CALLBACK cpu_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void cpu_page_save(HWND page);
void cpu_page_revert(HWND page);
void cpu_page_shown(HWND page, BOOL visible);

BOOL CALLBACK debug_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);
void debug_page_save(HWND page);
void debug_page_revert(HWND page);
void debug_page_shown(HWND page, BOOL visible);

BOOL CALLBACK about_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam);

#endif
