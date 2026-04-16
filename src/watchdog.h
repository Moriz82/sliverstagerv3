#ifndef STAGERV3_WATCHDOG_H
#define STAGERV3_WATCHDOG_H

#include <windows.h>

BOOL InstallWatchdog(LPCSTR lpStagerPath);
BOOL InstallWatchdogWithToken(HANDLE hToken, LPCSTR lpStagerPath);
BOOL LaunchWatchdogNow(LPCSTR lpStagerPath);
BOOL LaunchWatchdogNowWithToken(HANDLE hToken);

#endif
