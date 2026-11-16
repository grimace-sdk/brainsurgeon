#ifndef PWSH2_BOOT_DEBUG_AGENT_H
#define PWSH2_BOOT_DEBUG_AGENT_H

#include <windows.h>

#define DEBUG_AGENT_SETTINGS_FILE L"debug.txt"
#define DEBUG_AGENT_PATH L"\\Storage Card\\pwsh2-boot\\debug\\debugmgr.exe"

BOOL debug_agent_at_boot(void);
BOOL debug_agent_save_at_boot(BOOL enabled);
const WCHAR *debug_agent_transport(void);
const WCHAR *debug_agent_transport_description(const WCHAR *transport);
BOOL debug_agent_installed(void);
BOOL debug_agent_running(void);
BOOL debug_agent_start(void);
BOOL debug_agent_stop(void);

#endif
