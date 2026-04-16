#ifndef STAGERV3_PERSISTENCE_H
#define STAGERV3_PERSISTENCE_H

#include <windows.h>

#define STAGE_PERSISTENCE_SERVICE 1
#define STAGE_PERSISTENCE_TASK 2
#define STAGE_PERSISTENCE_SERVICE_AND_TASK 3

BOOL InstallServicePersistence(LPCSTR lpBinaryPath);
BOOL InstallTaskPersistence(LPCSTR lpBinaryPath);
BOOL RemovePersistenceArtifacts(void);
BOOL InstallPersistenceArtifacts(LPCSTR lpBinaryPath, int mode);
BOOL InstallPersistenceArtifactsWithToken(HANDLE hSystemToken, LPCSTR lpBinaryPath, int mode);

#endif
