#include <windows.h>

static BOOL EnableSpecificPrivilege(HANDLE hToken, LPCSTR privilegeName) {
    if (!hToken || !privilegeName || !*privilegeName) {
        return FALSE;
    }

    LUID luid = { 0 };
    if (!LookupPrivilegeValueA(NULL, privilegeName, &luid)) {
        return FALSE;
    }

    TOKEN_PRIVILEGES tokenPrivileges = { 0 };
    tokenPrivileges.PrivilegeCount = 1;
    tokenPrivileges.Privileges[0].Luid = luid;
    tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tokenPrivileges, sizeof(tokenPrivileges), NULL, NULL)) {
        return FALSE;
    }
    return GetLastError() == ERROR_SUCCESS || GetLastError() == ERROR_NOT_ALL_ASSIGNED;
}

BOOL EnableTokenPrivileges(HANDLE hToken) {
    static const char* kPrivilegeSet[] = {
        SE_ASSIGNPRIMARYTOKEN_NAME,
        SE_INCREASE_QUOTA_NAME,
        SE_TCB_NAME,
        SE_DEBUG_NAME,
        SE_BACKUP_NAME,
        SE_RESTORE_NAME,
        SE_IMPERSONATE_NAME,
        SE_LOAD_DRIVER_NAME,
        SE_SECURITY_NAME,
        SE_SYSTEM_ENVIRONMENT_NAME,
        SE_SHUTDOWN_NAME,
        SE_TAKE_OWNERSHIP_NAME,
        SE_CHANGE_NOTIFY_NAME
    };

    BOOL enabledAny = FALSE;
    for (int i = 0; i < (int)(sizeof(kPrivilegeSet) / sizeof(kPrivilegeSet[0])); ++i) {
        if (EnableSpecificPrivilege(hToken, kPrivilegeSet[i])) {
            enabledAny = TRUE;
        }
    }

    return enabledAny;
}

BOOL EnableAllTokenPrivileges(HANDLE hToken) {
    if (!hToken) {
        return FALSE;
    }

    return EnableTokenPrivileges(hToken);
}

BOOL PrepareSystemTokenPermissions(HANDLE hToken) {
    if (!hToken) {
        return FALSE;
    }

    HANDLE hProcessToken = NULL;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES,
            &hProcessToken)) {
        return FALSE;
    }

    BOOL result = EnableAllTokenPrivileges(hProcessToken) && EnableAllTokenPrivileges(hToken);
    CloseHandle(hProcessToken);
    return result;
}
