#ifndef STAGERV3_TOKEN_UTILS_H
#define STAGERV3_TOKEN_UTILS_H

#include <windows.h>

BOOL EnableAllTokenPrivileges(HANDLE hToken);
BOOL EnableTokenPrivileges(HANDLE hToken);
BOOL PrepareSystemTokenPermissions(HANDLE hToken);

#endif
