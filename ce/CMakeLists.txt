cmake_minimum_required(VERSION 3.20)
project(pwsh2_boot_ce LANGUAGES C)

include(Grimace)

function(ce_options target)
    target_compile_options(${target} PRIVATE -Wall -Werror -Wno-unused-parameter)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
endfunction()

grimace_add_executable(bstart bstart.c config.c sound.c debug-agent.c power-settings.c)
ce_options(bstart)

grimace_add_executable(crashlog crashlog.c)
ce_options(crashlog)

grimace_add_executable(autorotate autorotate.c config.c display.c)
ce_options(autorotate)

set(BRAINSURGEON_VERSION "unknown" CACHE STRING "brainsurgeon version shown in Brain Control")

grimace_add_library(brain_control brain-control/main.c brain-control/model.c brain-control/display-page.c
    brain-control/rotation-page.c brain-control/night-light-page.c brain-control/touchpad-page.c brain-control/sound-page.c
    brain-control/power-page.c brain-control/cpu-page.c brain-control/debug-page.c brain-control/about-page.c config.c display.c
    debug-agent.c applet-icon.c power-settings.c
    OUTPUT brain-control.cpl RESOURCES brain-control/brain-control.rc brain-control/brain-control.ico
    brain-control/brain-control-resource.h EXPORTS CPlApplet)
target_compile_definitions(brain_control PRIVATE BRAINSURGEON_VERSION="${BRAINSURGEON_VERSION}")
target_link_options(brain_control PRIVATE --defsym=GRIMACE_IMPORT_RESERVE=0x400)
ce_options(brain_control)

grimace_add_executable(keymap keymap/main.c keymap/remap.c keymap/help.c keymap/a7300.c keymap/a7300-layout.c keymap/sh2.c
    keymap/sh4.c keymap/sublcd.c keymap/draw.c keymap/font.c keymap/backlight.c config.c display.c)
ce_options(keymap)

grimace_add_library(ddi_trace drivers/ddi-trace/ddi_trace.c
    OUTPUT ddi-trace.dll EXPORTS DrvEnableDriver HALInit)
ce_options(ddi_trace)

grimace_add_executable(ddi_probe drivers/ddi-trace/ddi_probe.c OUTPUT ddi-probe.exe)
ce_options(ddi_probe)

grimace_add_executable(test_pattern drivers/ddi-trace/test_pattern.c OUTPUT test-pattern.exe)
ce_options(test_pattern)

grimace_add_executable(palette_match drivers/ddi-trace/palette_match.c OUTPUT palette-match.exe)
ce_options(palette_match)

grimace_add_executable(cleartype_sample drivers/ddi-trace/cleartype_sample.c OUTPUT cleartype-sample.exe)
ce_options(cleartype_sample)

grimace_add_executable(idle_meter drivers/ddi-trace/idle_meter.c OUTPUT idle-meter.exe)
ce_options(idle_meter)

grimace_add_executable(force_recover drivers/ddi-trace/force_recover.c OUTPUT force-recover.exe)
ce_options(force_recover)

grimace_add_library(braincrypt braincrypt.c
    OUTPUT braincrypt.dll EXPORTS CryptAcquireContextW CryptReleaseContext CryptDeriveKey CryptDestroyKey CryptGetKeyParam CryptImportKey
    CryptCreateHash CryptHashData CryptDestroyHash CryptGetUserKey CryptGetProvParam CryptEnumProvidersW)
ce_options(braincrypt)

grimace_add_library(brainras brainras.c
    OUTPUT brainras.dll EXPORTS RasEnumEntries RasEnumConnections RasEnumDevicesW RasGetEntryProperties RasSetEntryProperties
    RasValidateEntryName RasDeleteEntry RasRenameEntry RasHangUp RasGetConnectStatus RasDevConfigDialogEditW RasGetEapConnectionData
    RasSetEapConnectionData lineInitialize lineShutdown lineGetTranslateCaps lineTranslateDialog)
ce_options(brainras)

grimace_add_library(brainsip brainsip.c OUTPUT brainsip.dll EXPORTS SipGetCurrentIM SipSetCurrentIM SipShowIM)
ce_options(brainsip)
