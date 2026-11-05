# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: What every bash script of deploy/ shares: messages, the Python helper, locks and cleanup.
# Contributors: None
#
# Sourced, never run. The scripts run on Linux and on macOS as it comes: bash 3.2 and BSD tools, so
# nothing here uses associative arrays, mapfile, ${x,,}, GNU-only options of sed, stat, date, readlink
# or grep, or flock and timeout, which macOS lacks. An empty array is expanded as ${a[@]+"${a[@]}"},
# as bash before 4.4 calls "${a[@]}" of an empty one unbound under set -u. What bash cannot do alike on
# both, digests, JSON and binary formats, deploy/lib/csg_tool.py does, under any python3 from 3.8 on.
#
# Messages go to stderr and results to stdout, so a caller can take a script's result alone.

CSG_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
CSG_DEPLOY_DIR="$(cd "$CSG_LIB_DIR/.." && pwd -P)"
CSG_REPO_ROOT="$(cd "$CSG_DEPLOY_DIR/.." && pwd -P)"
CSG_TEMP="$CSG_REPO_ROOT/temp"
export CSG_REPO_ROOT

csg_say() { printf '%s\n' "$*" >&2; }
csg_warn() { printf 'WARNING: %s\n' "$*" >&2; }
csg_die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# The commands run when the script ends, however it ends: the last added first, as finally blocks nest.
CSG_CLEANUP=()
csg_on_exit() { CSG_CLEANUP[${#CSG_CLEANUP[@]}]="$1"; }
csg_run_cleanup() {
    local status=$? index
    trap - EXIT
    index=${#CSG_CLEANUP[@]}
    while [ "$index" -gt 0 ]; do
        index=$((index - 1))
        eval "${CSG_CLEANUP[$index]}" || true
    done
    exit "$status"
}
trap csg_run_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# The python3 the helper runs under: CSG_PYTHON if set, else python3, from 3.8 on. Checked once, here,
# and the answer handed down to the scripts this one runs.
if [ -z "${CSG_PYTHON_CHECKED:-}" ]; then
    CSG_PYTHON="${CSG_PYTHON:-python3}"
    command -v "$CSG_PYTHON" >/dev/null 2>&1 ||
        csg_die "No $CSG_PYTHON on PATH: the scripts need Python 3.8 or later, as PlatformIO does."
    "$CSG_PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' ||
        csg_die "$CSG_PYTHON is older than 3.8, which the scripts need."
    CSG_PYTHON_CHECKED=1
    export CSG_PYTHON CSG_PYTHON_CHECKED
fi
csg_python() { "$CSG_PYTHON" -B -E -s "$@"; }

# The helper: no bytecode written beside it, and nothing of the shell's PYTHON variables taken.
csg_tool() { csg_python "$CSG_LIB_DIR/csg_tool.py" "$@"; }

csg_require() {
    command -v "$1" >/dev/null 2>&1 || csg_die "${2:-$1 is not on PATH.}"
}

csg_os() {
    case "$(uname -s)" in
        Linux) printf 'linux\n' ;;
        Darwin) printf 'darwin\n' ;;
        *) printf '%s\n' "$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
    esac
}

# A path made whole against the working directory, its folder resolved, as realpath -m would.
csg_abs() {
    local dir base
    case "$1" in
        /*) dir="$(dirname "$1")" ;;
        *) dir="$(pwd -P)/$(dirname "$1")" ;;
    esac
    base="$(basename "$1")"
    mkdir -p "$dir"
    printf '%s/%s\n' "$(cd "$dir" && pwd -P)" "$base"
}

csg_lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
csg_upper() { printf '%s' "$1" | tr '[:lower:]' '[:upper:]'; }
csg_trim() {
    local text="$1"
    text="${text#"${text%%[![:space:]]*}"}"
    printf '%s' "${text%"${text##*[![:space:]]}"}"
}

# Release or Dev, as PowerShell's ValidateSet takes them: in any case.
csg_profile() {
    case "$(csg_lower "$1")" in
        release) printf 'Release\n' ;;
        dev) printf 'Dev\n' ;;
        *) csg_die "--build-profile is Release or Dev, not $1." ;;
    esac
}

# The value of an option that takes one, or a refusal when it is missing.
csg_need_value() {
    [ "$2" -ge 2 ] || csg_die "$1 needs a value."
}

# A folder of this run's own under temp/, put in the variable named, and removed when the script ends
# unless asked to keep it. Not a command substitution: a cleanup added in one would be lost with it.
csg_temp_dir() {
    local dir
    mkdir -p "$CSG_TEMP"
    dir="$(mktemp -d "$CSG_TEMP/$2-XXXXXXXX")" || csg_die "Could not make a folder under $CSG_TEMP."
    if [ "${3:-}" != keep ]; then
        # Quoted for the shell, as the cleanup is run as a command: a path may hold a quote.
        csg_on_exit "rm -rf $(printf '%q' "$dir")"
    fi
    printf -v "$1" '%s' "$dir"
}

# The first line a command prints, read whole first: head in a pipeline under pipefail may cut the
# command off with SIGPIPE, and fail it.
csg_first_line() {
    local said
    said="$("$@")" || return 1
    printf '%s\n' "${said%%$'\n'*}"
}

# How many lines a text holds, as a plain number: wc -l pads it with spaces on macOS.
csg_count_lines() {
    local count
    if [ -z "$1" ]; then
        printf '0\n'
        return 0
    fi
    count="$(printf '%s\n' "$1" | wc -l)"
    printf '%s\n' "$((count + 0))"
}

# A yes from the owner at the terminal; no terminal is a no, as nothing may be done unasked.
csg_confirm() {
    local answer
    [ -t 0 ] || return 1
    printf '%s [y/N] ' "$1" >&2
    read -r answer || return 1
    case "$answer" in
        y | Y | yes | YES | Yes) return 0 ;;
        *) return 1 ;;
    esac
}

# A file written whole beside its place and then moved there, so a failure leaves the one before.
csg_write_file() {
    local path="$1" partial
    partial="$path.$$.new"
    printf '%s' "$2" >"$partial" && mv -f "$partial" "$path"
}

# The lock of one kind of build in this checkout, as Lock-csiGarlandBuild holds it: firmware, utility or
# publish. An exclusive flock on temp/build-<kind>.lock, the file the PowerShell scripts open for themselves
# alone, held by a small process of deploy/lib/csg_tool.py for as long as the shell that took it lives: flock is not a
# command on macOS, and the kernel lets go of the lock when its holder ends, however it ends, so no lock is
# ever left behind to take over. One held by another waits up to the seconds given, then fails. Released
# when the script ends.
csg_lock() {
    local kind="$1" wait="${2:-0}" file status holder said owner
    if csg_lock_held "$kind"; then
        return 0
    fi
    # This shell's own pid, a subshell's too, which $$ is not and bash 3.2 has no BASHPID for: exec makes the
    # command substitution sh itself, whose parent is this shell. Named to the holder before it starts.
    owner="$(exec sh -c 'echo "$PPID"')"
    mkdir -p "$CSG_TEMP"
    file="$CSG_TEMP/build-$kind.lock"
    status="$CSG_TEMP/build-$kind.lock.$$.status"
    rm -f "$status"
    # The Python itself in the background, not a function, so that $! is the holder and not a subshell.
    "$CSG_PYTHON" -B -E -s "$CSG_LIB_DIR/csg_tool.py" lock-hold "$file" "$wait" "$status" "$owner" </dev/null >/dev/null 2>&1 &
    holder=$!
    said=''
    while [ -z "$said" ]; do
        if [ -s "$status" ]; then
            said="$(cat "$status")"
        elif ! kill -0 "$holder" 2>/dev/null; then
            said="$(cat "$status" 2>/dev/null || true)"
            said="${said:-failed}"
        else
            sleep 0.1
        fi
    done
    rm -f "$status"
    case "$said" in
        held) ;;
        busy)
            wait "$holder" 2>/dev/null || true
            if [ "$kind" = publish ]; then
                csg_die "Another publish-release of this checkout runs, as $file is held: wait for it to end, then run this again."
            fi
            csg_die "Another $kind build of this checkout runs, as $file is held: wait for it to end, then run this again."
            ;;
        *) csg_die "The lock $file could not be taken: Python could not hold it." ;;
    esac
    eval "CSG_LOCK_$kind=\$holder"
    csg_on_exit "csg_unlock $kind"
}

# Whether this script holds the lock of a kind, by a holder that still lives.
csg_lock_held() {
    local holder
    eval "holder=\${CSG_LOCK_$1:-}"
    [ -n "$holder" ] && kill -0 "$holder" 2>/dev/null
}

csg_unlock() {
    local holder
    eval "holder=\${CSG_LOCK_$1:-}"
    if [ -n "$holder" ]; then
        # SIGUSR1, as a holder lets a stop sent to the whole group pass, to hold on through the cleanup.
        kill -USR1 "$holder" 2>/dev/null || true
        wait "$holder" 2>/dev/null || true
        eval "CSG_LOCK_$1="
    fi
}

# A unique id for this run, as the PowerShell scripts take a GUID's first eight digits.
csg_run_id() {
    csg_python -c 'import secrets; print(secrets.token_hex(4))'
}
