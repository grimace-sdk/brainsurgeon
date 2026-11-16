#include <windows.h>

#define LOG_PATH L"\\Storage Card\\pwsh2-boot\\crash.log"
#define CONTEXT_BYTES 1024
#define DEBUG_EVENT_WAIT_MILLISECONDS 5000
#define MAXIMUM_CANDIDATES 4
#define MAXIMUM_THREADS 64
#define STACK_WORDS 16
#define IMAGE_NAME_CHARACTERS 64
#define BREAKPOINT_EXCEPTION 0x80000003

struct thread_entry {
    DWORD id;
    HANDLE handle;
};

static HANDLE log_file = INVALID_HANDLE_VALUE;
static struct thread_entry threads[MAXIMUM_THREADS];
static unsigned int thread_count;
static HANDLE debugged_process;

static void log_text(const WCHAR *text)
{
    char line[256];
    int length = 0;
    DWORD written;

    if (log_file == INVALID_HANDLE_VALUE)
        return;
    while (text[length] && length < (int)sizeof(line) - 2) {
        line[length] = text[length] < 0x80 ? (char)text[length] : '?';
        length++;
    }
    line[length++] = '\r';
    line[length++] = '\n';
    WriteFile(log_file, line, length, &written, NULL);
    FlushFileBuffers(log_file);
}

static void remember_thread(DWORD id, HANDLE handle)
{
    if (thread_count < MAXIMUM_THREADS) {
        threads[thread_count].id = id;
        threads[thread_count].handle = handle;
        thread_count++;
    }
}

static HANDLE find_thread(DWORD id)
{
    unsigned int index;

    for (index = 0; index < thread_count; index++)
        if (threads[index].id == id)
            return threads[index].handle;
    return NULL;
}

static unsigned int parse_numbers(const WCHAR *text, DWORD *numbers, unsigned int maximum)
{
    unsigned int count = 0;

    while (text && *text && count < maximum) {
        DWORD value = 0;
        BOOL hexadecimal = FALSE;

        if (*text < L'0' || *text > L'9') {
            text++;
            continue;
        }
        if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
            hexadecimal = TRUE;
            text += 2;
        }
        for (;; text++) {
            if (*text >= L'0' && *text <= L'9')
                value = value * (hexadecimal ? 16 : 10) + (DWORD)(*text - L'0');
            else if (hexadecimal && *text >= L'a' && *text <= L'f')
                value = value * 16 + (DWORD)(*text - L'a' + 10);
            else if (hexadecimal && *text >= L'A' && *text <= L'F')
                value = value * 16 + (DWORD)(*text - L'A' + 10);
            else
                break;
        }
        numbers[count++] = value;
    }
    return count;
}

static void log_image_name(const WCHAR *label, LPVOID base, LPVOID name_pointer_address, WORD unicode)
{
    WCHAR text[160];
    WCHAR name[IMAGE_NAME_CHARACTERS];
    LPVOID name_address = NULL;
    DWORD read = 0;

    name[0] = 0;
    if (name_pointer_address && unicode &&
        ReadProcessMemory(debugged_process, name_pointer_address, &name_address, sizeof(name_address), &read) && name_address &&
        ReadProcessMemory(debugged_process, name_address, name, sizeof(name) - sizeof(WCHAR), &read))
        name[IMAGE_NAME_CHARACTERS - 1] = 0;
    wsprintfW(text, L"%s base %08x %s", label, (unsigned)base, name);
    log_text(text);
}

static void log_exception(const DEBUG_EVENT *event)
{
    const EXCEPTION_RECORD *record = &event->u.Exception.ExceptionRecord;
    DWORD context_words[CONTEXT_BYTES / sizeof(DWORD)];
    CONTEXT *context = (CONTEXT *)context_words;
    HANDLE thread = find_thread(event->dwThreadId);
    WCHAR text[200];
    DWORD stack[STACK_WORDS];
    DWORD read = 0;
    unsigned int index;

    wsprintfW(text, L"exception %08x at %08x, flags %08x, first chance %u, thread %08x", (unsigned)record->ExceptionCode,
        (unsigned)record->ExceptionAddress, (unsigned)record->ExceptionFlags, (unsigned)event->u.Exception.dwFirstChance,
        (unsigned)event->dwThreadId);
    log_text(text);
    for (index = 0; index < record->NumberParameters && index < EXCEPTION_MAXIMUM_PARAMETERS; index++) {
        wsprintfW(text, L"  parameter %u: %08x", index, (unsigned)record->ExceptionInformation[index]);
        log_text(text);
    }
    memset(context_words, 0, sizeof(context_words));
    context->ContextFlags = CONTEXT_FULL;
    if (!thread || !GetThreadContext(thread, context)) {
        wsprintfW(text, L"  no thread context (error %u)", (unsigned)GetLastError());
        log_text(text);
        return;
    }
    wsprintfW(text, L"  r0 %08x r1 %08x r2 %08x r3 %08x", (unsigned)context->R0, (unsigned)context->R1, (unsigned)context->R2,
        (unsigned)context->R3);
    log_text(text);
    wsprintfW(text, L"  r4 %08x r5 %08x r6 %08x r7 %08x", (unsigned)context->R4, (unsigned)context->R5, (unsigned)context->R6,
        (unsigned)context->R7);
    log_text(text);
    wsprintfW(text, L"  r8 %08x r9 %08x r10 %08x r11 %08x r12 %08x", (unsigned)context->R8, (unsigned)context->R9,
        (unsigned)context->R10, (unsigned)context->R11, (unsigned)context->R12);
    log_text(text);
    wsprintfW(text, L"  sp %08x lr %08x pc %08x psr %08x", (unsigned)context->Sp, (unsigned)context->Lr, (unsigned)context->Pc,
        (unsigned)context->Psr);
    log_text(text);
    if (ReadProcessMemory(debugged_process, (LPCVOID)context->Sp, stack, sizeof(stack), &read)) {
        for (index = 0; index < read / sizeof(DWORD); index += 4) {
            wsprintfW(text, L"  stack +%02x: %08x %08x %08x %08x", index * 4, (unsigned)stack[index], (unsigned)stack[index + 1],
                (unsigned)stack[index + 2], (unsigned)stack[index + 3]);
            log_text(text);
        }
    }
}

static void debug_process(void)
{
    DEBUG_EVENT event;
    WCHAR text[120];

    for (;;) {
        DWORD status = DBG_CONTINUE;

        if (!WaitForDebugEvent(&event, DEBUG_EVENT_WAIT_MILLISECONDS)) {
            log_text(L"no debug event within 5 s");
            return;
        }
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            debugged_process = event.u.CreateProcessInfo.hProcess;
            remember_thread(event.dwThreadId, event.u.CreateProcessInfo.hThread);
            log_image_name(L"process", event.u.CreateProcessInfo.lpBaseOfImage, event.u.CreateProcessInfo.lpImageName,
                event.u.CreateProcessInfo.fUnicode);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            remember_thread(event.dwThreadId, event.u.CreateThread.hThread);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            log_image_name(L"module", event.u.LoadDll.lpBaseOfDll, event.u.LoadDll.lpImageName, event.u.LoadDll.fUnicode);
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode == BREAKPOINT_EXCEPTION) {
                log_text(L"breakpoint, continuing");
                break;
            }
            log_exception(&event);
            if (debugged_process)
                TerminateProcess(debugged_process, 1);
            log_text(L"process ended");
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            wsprintfW(text, L"process exited, code %08x", (unsigned)event.u.ExitProcess.dwExitCode);
            log_text(text);
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
            return;
        default:
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    DWORD candidates[MAXIMUM_CANDIDATES];
    unsigned int count, index;
    SYSTEMTIME now;
    WCHAR text[200];

    log_file = CreateFileW(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (log_file != INVALID_HANDLE_VALUE)
        SetFilePointer(log_file, 0, NULL, FILE_END);
    GetLocalTime(&now);
    wsprintfW(text, L"crashlog %04u-%02u-%02u %02u:%02u:%02u, tick %u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, (unsigned)GetTickCount());
    log_text(text);
    log_text(L"command line:");
    log_text(command_line ? command_line : L"(none)");
    count = parse_numbers(command_line, candidates, MAXIMUM_CANDIDATES);
    for (index = 0; index < count; index++) {
        if (candidates[index] && DebugActiveProcess(candidates[index])) {
            wsprintfW(text, L"attached to process %08x", (unsigned)candidates[index]);
            log_text(text);
            debug_process();
            CloseHandle(log_file);
            return 0;
        }
        wsprintfW(text, L"cannot attach to %08x (error %u)", (unsigned)candidates[index], (unsigned)GetLastError());
        log_text(text);
    }
    log_text(L"no process to attach to");
    CloseHandle(log_file);
    return 1;
}
