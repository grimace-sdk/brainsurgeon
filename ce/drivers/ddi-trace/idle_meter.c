#include <windows.h>

#define LOG_PATH TEXT("\\Storage Card\\idle-meter.log")
#define DEFAULT_SECONDS 10

static void LogNumber(HANDLE hLog, const char *pLabel, DWORD Value)
{
    char Text[64];
    char Digits[12];
    int Length = 0, Count = 0;
    DWORD Written;

    while (*pLabel)
        Text[Length++] = *pLabel++;
    do
    {
        Digits[Count++] = (char)('0' + Value % 10);
        Value /= 10;
    } while (Value);
    while (Count)
        Text[Length++] = Digits[--Count];
    Text[Length++] = '\r';
    Text[Length++] = '\n';
    WriteFile(hLog, Text, Length, &Written, NULL);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPWSTR pCommandLine, int Show)
{
    DWORD Seconds = DEFAULT_SECONDS, StartTick, StartIdle, Elapsed, Idle;
    HANDLE hLog;

    if (pCommandLine && *pCommandLine)
        Seconds = wcstoul(pCommandLine, NULL, 10);
    if (!Seconds)
        Seconds = DEFAULT_SECONDS;
    StartTick = GetTickCount();
    StartIdle = GetIdleTime();
    Sleep(Seconds * 1000);
    Elapsed = GetTickCount() - StartTick;
    Idle = GetIdleTime() - StartIdle;
    hLog = CreateFile(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE)
        return 1;
    LogNumber(hLog, "elapsed ms ", Elapsed);
    LogNumber(hLog, "idle ms ", Idle);
    LogNumber(hLog, "idle permille ", Elapsed ? (DWORD)((ULONGLONG)Idle * 1000 / Elapsed) : 0);
    CloseHandle(hLog);
    return 0;
}
