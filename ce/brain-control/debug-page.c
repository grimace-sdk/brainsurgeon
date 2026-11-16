#include <windows.h>
#include "brain-control.h"
#include "brain-control-resource.h"
#include "../debug-agent.h"

#define STATUS_TIMER 1
#define STATUS_INTERVAL_MILLISECONDS 1000

static BOOL original_at_boot;

static void show_status(HWND page)
{
    WCHAR text[TEXT_LENGTH * 2];
    BOOL installed = debug_agent_installed();
    BOOL running = debug_agent_running();

    if (!installed && !running)
        wcscpy(text, L"Not on the card: pwsh2-boot\\debug\\debugmgr.exe is missing");
    else
        wsprintfW(text, L"%s\r\nTransport: %s", running ? L"Running" : L"Not running",
            debug_agent_transport_description(debug_agent_transport()));
    SetDlgItemTextW(page, IDC_DEBUG_STATUS, text);
    EnableWindow(GetDlgItem(page, IDC_DEBUG_START), installed && !running);
    EnableWindow(GetDlgItem(page, IDC_DEBUG_STOP), running);
}

BOOL CALLBACK debug_page_procedure(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        original_at_boot = debug_agent_at_boot();
        CheckDlgButton(page, IDC_DEBUG_AT_BOOT, original_at_boot ? BST_CHECKED : BST_UNCHECKED);
        show_status(page);
        return TRUE;
    case WM_TIMER:
        if (wparam == STATUS_TIMER) {
            show_status(page);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) != BN_CLICKED)
            break;
        if (LOWORD(wparam) == IDC_DEBUG_START) {
            if (!debug_agent_start())
                MessageBoxW(page, L"Could not start Debug Agent.", L"Debug", MB_OK | MB_ICONEXCLAMATION);
            show_status(page);
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_DEBUG_STOP) {
            debug_agent_stop();
            show_status(page);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void debug_page_shown(HWND page, BOOL visible)
{
    if (!visible) {
        KillTimer(page, STATUS_TIMER);
        return;
    }
    show_status(page);
    SetTimer(page, STATUS_TIMER, STATUS_INTERVAL_MILLISECONDS, NULL);
}

void debug_page_save(HWND page)
{
    BOOL at_boot = IsDlgButtonChecked(page, IDC_DEBUG_AT_BOOT) == BST_CHECKED;

    KillTimer(page, STATUS_TIMER);
    if (at_boot == original_at_boot)
        return;
    if (!debug_agent_save_at_boot(at_boot))
        MessageBoxW(page, L"Could not save debug.txt on the card.", L"Debug", MB_OK | MB_ICONEXCLAMATION);
}

void debug_page_revert(HWND page)
{
    KillTimer(page, STATUS_TIMER);
}
