#!/usr/bin/env bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Install over Wi-Fi an update file with firmware, effects, or both.
# Contributors: None
#
# The bash twin of deploy/ota-update.ps1, for Linux and macOS, by curl, with the same token file and the
# same watch of the garland until it has confirmed what it was sent. The first time, the garland shows a
# code and asks for POWER and SPEED, like a browser's first visit, and the token it gives is kept in
# deploy/portal-token.local.txt for its 100 days. HTTPS is checked against what curl trusts: the system's
# roots on Linux, where the garland's root is installed as docs/guides/user-guide.md says; on macOS curl
# does not read the Keychain, so name the root with --ca-file. Without trust nothing is sent, not even the
# token. The garland refuses anything not signed under the root its firmware holds, and goes back to the
# previous firmware by itself if a new one does not survive its first minute.
#
# Options
#
#   --address <host>      The garland, as its certificate has it: csigarland.local, or your domain or address.
#   --no-build            Send the update file from the last build of the profile --build-profile names;
#                         without it, the one profile built here, or a refusal when both are.
#   --firmware-only       Build and send only the firmware. The effects on the garland stay.
#   --effects-only        Send only the effects of firmware/app/effects, with no build.
#   --file <path>         Send this update file as it is, with no build.
#   --build-profile Release|Dev
#                         What to build: Release, the default, or Dev.
#   --ca-file <path>      Trust HTTPS signed under this root, as the garland's own from its portal's Info page.
#   --insecure            For recovery: do not check who answers, and fall back to plain HTTP if HTTPS does
#                         not answer. Whoever stands between can then read the token and fake the
#                         confirmation. An update's signature still guards what gets installed.
#   --what-if             Show what the file holds and stop.
#
# Examples:
#   bash deploy/ota-update.sh --address csigarland.local
#   bash deploy/ota-update.sh --address csigarland.local --no-build --ca-file ~/csigarland-root.crt
#   bash deploy/ota-update.sh --address 192.168.1.57 --no-build --insecure
#   bash deploy/ota-update.sh --address csigarland.local --effects-only
set -euo pipefail

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/lib/common.sh"
. "$CSG_LIB_DIR/build_version.sh"
. "$CSG_LIB_DIR/signing_python.sh"

address=''
no_build=0 firmware_only=0 effects_only=0 insecure=0 what_if=0 profile_given=0
file=''
ca_file=''
profile=Release
while [ "$#" -gt 0 ]; do
    case "$1" in
        --address) csg_need_value "$1" "$#"; address="$2"; shift ;;
        --no-build) no_build=1 ;;
        --firmware-only) firmware_only=1 ;;
        --effects-only) effects_only=1 ;;
        --file) csg_need_value "$1" "$#"; file="$2"; shift ;;
        --ca-file) csg_need_value "$1" "$#"; ca_file="$2"; shift ;;
        --insecure) insecure=1 ;;
        --build-profile) csg_need_value "$1" "$#"; profile="$(csg_profile "$2")"; profile_given=1; shift ;;
        --what-if) what_if=1 ;;
        *) csg_die "ota-update.sh does not take $1." ;;
    esac
    shift
done
[ -n "$address" ] || csg_die '--address is required: the garland, as its certificate has it.'
csg_require curl 'curl is not on PATH.'
if [ $((firmware_only + effects_only + (${#file} > 0 ? 1 : 0))) -gt 1 ]; then
    csg_die 'Use one of --firmware-only, --effects-only and --file.'
fi

firmware_dir="$CSG_REPO_ROOT/firmware/app"
ini="$firmware_dir/platformio.ini"
build_dir="$firmware_dir/.pio/build/$(csg_firmware_environment "$profile")"
token_path="$CSG_DEPLOY_DIR/portal-token.local.txt"
# A garland is sent only what its own root signed: never a test build's throwaway chain.
csg_assert_own_signing
csg_temp_dir work ota-update
tls=()
if [ "$insecure" -eq 1 ]; then tls=(-k); fi
if [ -n "$ca_file" ]; then
    [ -f "$ca_file" ] || csg_die "No certificate at $ca_file."
    tls[${#tls[@]}]=--cacert
    tls[${#tls[@]}]="$ca_file"
fi

# The effects as they are in the repository now, signed, in a folder of their own under temp/.
new_effects_update() {
    local python generated
    python="$(csg_signing_python)"
    generated="$work/effects"
    mkdir -p "$generated"
    "$python" "$firmware_dir/scripts/effect_pack_tool.py" "$firmware_dir/effects" "$generated" >&2 ||
        csg_die 'The effects pack was not made. Nothing was sent.'
    "$python" "$firmware_dir/scripts/ota_sign_tool.py" sign "$CSG_REPO_ROOT/cert" "$generated/effects.pack" - "$generated/effects.ota" >&2 ||
        csg_die 'The effects pack was not signed. Nothing was sent.'
    update_path="$generated/effects.ota"
}

# What is wrong with a firmware of this version for a build of this profile from this checkout, or nothing.
build_version_problem() {
    local version="$1" kind="$2" number wanted
    number="$(printf '%s' "$version" | sed -n 's/^[0-9]*\.[0-9]*\.[0-9]*\.\([0-9]*\)\(-.*\)\{0,1\}$/\1/p')"
    if [ -n "$number" ] && [ "$version" = "$(csg_firmware_identity "$ini" "$number" "$kind")" ]; then
        return 0
    fi
    wanted="$(csg_firmware_identity "$ini" 0 "$kind" | sed -E 's/\.0(-|$)/.N\1/')"
    printf 'firmware %s, not a %s build of this checkout, such as %s\n' "${version:-of unknown version}" "$kind" "$wanted"
}

update_name=firmware.ota
if [ "$firmware_only" -eq 1 ]; then update_name=firmware-only.ota; fi
update_path=''
if [ -n "$file" ]; then
    update_path="$file"
elif [ "$effects_only" -eq 1 ]; then
    new_effects_update
else
    # The firmware build lock, from the build, or the look at the last one, to the bytes read.
    csg_lock firmware
    if [ "$no_build" -eq 0 ]; then
        bash "$CSG_DEPLOY_DIR/flash-device.sh" --build-only --build-profile "$profile" --lock-held >&2 || csg_die 'Build failed. Nothing was sent.'
    elif [ "$profile_given" -eq 0 ]; then
        # Each profile builds in its own folder, so the last build may be either: only a file of the kind
        # asked for, whose firmware is of its folder's profile and this checkout's base, counts.
        built=''
        count=0
        for kind in Release Dev; do
            candidate="$firmware_dir/.pio/build/$(csg_firmware_environment "$kind")/$update_name"
            [ -f "$candidate" ] || continue
            held="$(csg_tool ota-contents "$candidate" 2>/dev/null)" || continue
            if [ -z "$(build_version_problem "$(printf '%s' "$held" | csg_tool json-get - version '')" "$kind")" ] && csg_test_own_signature "$candidate"; then
                built="$kind"
                count=$((count + 1))
            fi
        done
        [ "$count" -le 1 ] || csg_die 'Both a Release and a Dev build of this version are here: name the one to send with --build-profile Release or --build-profile Dev. Nothing was sent.'
        if [ "$count" -eq 1 ]; then
            build_dir="$firmware_dir/.pio/build/$(csg_firmware_environment "$built")"
            csg_say "Sending the last $built build, the only one of this version here."
        fi
    fi
    update_path="$build_dir/$update_name"
fi
[ -f "$update_path" ] || csg_die "No update file at $update_path. Build first."
# Read once, here: a build may replace the file while this runs, and what goes out has to be what was
# shown, and what the garland is then held to.
sending="$work/sending.ota"
cp -f "$update_path" "$sending"
csg_unlock firmware
contents="$(csg_tool ota-contents "$sending" "$update_path")"
field_of() { printf '%s' "$2" | csg_tool json-get - "$1" ''; }
if [ -z "$file" ] && [ "$effects_only" -eq 0 ]; then
    folder_profile=Dev
    if [ "$(basename "$build_dir")" = "$(csg_firmware_environment Release)" ]; then folder_profile=Release; fi
    problem="$(build_version_problem "$(field_of version "$contents")" "$folder_profile")"
    [ -z "$problem" ] || csg_die "$update_path holds $problem: a build left from before, it seems. Build again, or send it with --file if it is meant. Nothing was sent."
fi
# The bytes to be sent, judged as the garland will judge them, so a refusal costs no restart.
csg_test_own_signature "$sending" || csg_die "$update_path is not signed under this repository's root. Nothing was sent."
holds="$(field_of text "$contents")"
csg_say "This file holds $holds."
if [ "$what_if" -eq 1 ]; then
    csg_say "What if: install $holds on $address."
    exit 0
fi

base="https://$address"
# The token is for this user alone: anyone else on the computer who read it could run the garland for its
# 100 days. A file there before with more is narrowed.
protect_token() {
    if [ -f "$token_path" ]; then chmod 600 "$token_path"; fi
}
protect_token
token=''
if [ -f "$token_path" ]; then token="$(csg_trim "$(cat "$token_path")")"; fi
# The token goes to curl in a file of this user's alone, never on its command line, which anyone on the
# computer can read in the list of processes for as long as curl runs.
auth_header="$work/auth.header"
write_auth_header() {
    (umask 077 && printf 'Cookie: csig=%s\n' "$token" >"$auth_header") ||
        csg_die "The token could not be handed to curl in $auth_header. Nothing was sent."
}
write_auth_header
# One request: the body to a file, the status in http_status, and curl's own exit as the result, so a
# connection that broke, which has no status, is told from a refusal, which has one.
http_status=''
request() {
    local timeout="$1" out="$2"
    shift 2
    http_status="$(curl -sS --max-time "$timeout" ${tls[@]+"${tls[@]}"} -H "@$auth_header" -o "$out" -w '%{http_code}' "$@")"
}

# Asks the owner at the panel, and keeps the token the garland gives this script.
request_token() {
    local asked code seconds id deadline state
    request 15 "$work/asked.json" -X POST -H 'Content-Type: application/json' --data '{"label":"ota-update.sh"}' "$base/api/auth/request" ||
        csg_die "The garland did not answer the request for access. Nothing was sent."
    [ "$http_status" = 200 ] || csg_die "The garland refused the request for access ($http_status). Nothing was sent."
    code="$(csg_tool json-get "$work/asked.json" code '')"
    seconds="$(csg_tool json-get "$work/asked.json" seconds 60)"
    id="$(csg_tool json-get "$work/asked.json" request '')"
    csg_say "If the garland shows the code $code, press POWER and SPEED together there."
    # The request's id is what the token is handed over for, so it goes in a file as the token does.
    (umask 077 && printf '{"request":%s}' "$(printf '%s' "$id" | csg_python -c 'import json, sys; print(json.dumps(sys.stdin.read()))')" \
        >"$work/wait-body.json") || csg_die 'The request for access could not be written. Nothing was sent.'
    deadline=$(($(date +%s) + ${seconds%.*}))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        sleep 2
        request 15 "$work/wait.json" -D "$work/wait.headers" -X POST -H 'Content-Type: application/json' \
            --data "@$work/wait-body.json" "$base/api/auth/wait" ||
            csg_die 'The garland stopped answering while access was asked. Nothing was sent.'
        state="$(csg_tool json-get "$work/wait.json" state '')"
        if [ "$state" = approved ]; then
            token="$(tr -d '\r' <"$work/wait.headers" | sed -n 's/^[Ss]et-[Cc]ookie: csigh\{0,1\}=\([0-9a-f]*\);.*/\1/p')"
            token="${token%%$'\n'*}"
            [ -n "$token" ] || csg_die 'The garland answered without a token. Nothing was sent.'
            # For this user alone: written to a new file made so, then put in the old one's place, so whoever
            # held the old file open keeps its old bytes, never the new token.
            (umask 077 && printf '%s\n' "$token" >"$token_path.$$.new") && chmod 600 "$token_path.$$.new" &&
                mv -f "$token_path.$$.new" "$token_path" || {
                rm -f "$token_path.$$.new"
                csg_die "The token could not be kept in $token_path. Nothing was sent."
            }
            write_auth_header
            return 0
        fi
        [ "$state" = waiting ] || break
    done
    csg_die 'The request was not approved in time. Nothing was sent.'
}

# A check of who answers comes before the token goes anywhere: no Cookie header until it passes.
if ! curl -sS --max-time 15 ${tls[@]+"${tls[@]}"} -o /dev/null "$base/api/ota" 2>"$work/probe.err"; then
    if [ "$insecure" -eq 0 ]; then
        csg_die "No trusted HTTPS at $address: $(cat "$work/probe.err") Nothing was sent, not even the token. Use a name or address the garland's certificate holds, and install its root on this computer (docs/guides/user-guide.md, Stopping the Certificate Warning), or name it with --ca-file. If its HTTPS has failed, or to go on without knowing who answers, add --insecure."
    fi
    csg_warn "No answer over HTTPS: $(cat "$work/probe.err"). Trying plain HTTP."
    base="http://$address"
fi
uri="$base/api/ota"
request 30 "$work/before.json" "$uri" || csg_die "The garland did not answer at $uri."
if [ "$http_status" = 401 ]; then
    request_token
    request 30 "$work/before.json" "$uri" || csg_die "The garland did not answer at $uri."
fi
[ "$http_status" = 200 ] || csg_die "The garland answered $http_status at $uri."
before="$(cat "$work/before.json")"
# By number, as mDNS can be slow to find a garland that has just restarted.
host_address="$(csg_python -c 'import socket, sys
try:
    print(socket.getaddrinfo(sys.argv[1], None, socket.AF_INET)[0][4][0])
except OSError:
    print(sys.argv[1])' "$address")"
before_boot="$(field_of boot "$before")"
csg_say "Running now: $(field_of version "$before") with $(field_of effects "$before") built-in effects. Sending the update..."
# A refusal comes with a status, and is the answer. A connection lost with none may have taken the update
# whole, the garland restarting before its answer reached here: what it runs next says which.
sent_unknown=''
if request 600 "$work/reply.json" -X POST -H 'Content-Type: application/octet-stream' --data-binary "@$sending" "$uri" 2>"$work/send.err"; then
    case "$http_status" in
        2*) csg_say "Installed: firmware $(csg_tool json-get "$work/reply.json" firmware ''), effects $(csg_tool json-get "$work/reply.json" effects ''). The garland is restarting." ;;
        *) csg_die "The garland refused the update ($http_status): $(cat "$work/reply.json")" ;;
    esac
else
    sent_unknown="$(cat "$work/send.err")"
    csg_warn "No answer to the update: $sent_unknown. It may have been installed: watching what the garland runs..."
fi

# Over only when the garland has restarted, runs what was sent and has confirmed it. Meanwhile the
# firmware on trial's log comes over plain HTTP, which is all there is to see of it on a closed unit.
log_path="$CSG_TEMP/trial-log.$(date +%Y%m%d-%H%M%S).txt"
mkdir -p "$CSG_TEMP"
: >"$work/seen"
went_down=0 https_failed=0 proved=0
last_check=0
said=''
unproved=''
image_before="$(field_of image "$before")"
pack_before="$(field_of pack_digest "$before")"
want_image="$(field_of image "$contents")"
want_pack="$(field_of pack_digest "$contents")"
# In a variable, as bash 3.2 and later read a pattern written inline differently.
plain_pattern='^version=([^ ]+) on_trial=([0-9]) https=0 boot=([^ ]*) image=([^ ]*) pack=([^ ]*)'
deadline=$(($(date +%s) + 180))
while [ "$(date +%s)" -lt "$deadline" ]; do
    sleep 0.5
    plain_state=''
    if status="$(curl -s --max-time 2 -o "$work/trial.txt" -w '%{http_code}' "http://$host_address/trial-log")"; then
        if [ "$status" = 200 ]; then
            plain_state="$(sed -n '1p' "$work/trial.txt" | tr -d '\r')"
            while IFS= read -r line; do
                line="${line%"${line##*[![:space:]]}"}"
                if [ -n "$line" ] && ! grep -Fxq -- "$line" "$work/seen"; then
                    printf '%s\n' "$line" >>"$work/seen"
                    # On trial plain HTTP starts first, so https=0 alone says only "not yet".
                    case "$line" in *'no HTTPS, the portal stays'*) https_failed=1 ;; esac
                    csg_say "  | $line"
                    printf '%s\n' "$line" >>"$log_path"
                fi
            done < <(sed '1d' "$work/trial.txt")
        fi
    else
        went_down=1
    fi
    now="$(date +%s)"
    [ $((now - last_check)) -ge 3 ] || continue
    last_check="$now"
    after=''
    if request 3 "$work/after.json" "$uri" 2>/dev/null && [ "$http_status" = 200 ]; then
        after="$(cat "$work/after.json")"
    elif [ "$https_failed" -eq 1 ] && [[ "$plain_state" =~ $plain_pattern ]]; then
        # A firmware whose HTTPS failed still says so here, though anyone could.
        if [ "$insecure" -eq 1 ]; then
            trial=false
            if [ "${BASH_REMATCH[2]}" = 1 ]; then trial=true; fi
            after="$(csg_tool json-build --str version "${BASH_REMATCH[1]}" --bool on_trial "$trial" --str boot "${BASH_REMATCH[3]}" \
                --str image "${BASH_REMATCH[4]}" --str pack_digest "${BASH_REMATCH[5]}" --str effects '?')"
        fi
        if [ "$said" != plain ]; then
            said=plain
            csg_warn 'The new firmware has no working HTTPS and runs on plain HTTP. Its log above says why.'
        fi
    else
        went_down=1
    fi
    [ -n "$after" ] || continue
    # Before the restart the old firmware still answers, which says nothing yet. One too old to give its
    # boot id shows the restart only by having gone away.
    if [ "$(field_of boot "$after")" = "$before_boot" ] && { [ -n "$before_boot" ] || [ "$went_down" -eq 0 ]; }; then
        continue
    fi
    running_image="$(field_of image "$after")"
    running_pack="$(field_of pack_digest "$after")"
    running_version="$(field_of version "$after")"
    on_trial="$(field_of on_trial "$after")"
    # An answer that cannot say what runs proves nothing either way, and a version may repeat.
    if { [ -n "$want_image" ] && [ -z "$running_image" ]; } || { [ -n "$want_pack" ] && [ -z "$running_pack" ]; }; then
        unproved="$running_version"
        continue
    fi
    # With the answer lost, only a trial of the file's image, or effects alone that were not running with
    # the firmware as it was, say the garland took this file.
    if [ -n "$sent_unknown" ] && [ "$proved" -eq 0 ]; then
        took=0
        if [ -n "$want_image" ] && [ "$on_trial" = true ] && [ "$running_image" = "$want_image" ]; then took=1; fi
        if [ -z "$want_image" ] && [ -n "$want_pack" ] && [ "$pack_before" != "$want_pack" ] && [ "$running_pack" = "$want_pack" ] && [ "$running_image" = "$image_before" ]; then took=1; fi
        if [ "$took" -eq 0 ]; then
            trial_said=''
            if [ "$on_trial" = true ]; then trial_said=', on trial'; fi
            csg_die "Not confirmed: the garland restarted and runs $running_version$trial_said, but the update got no answer ($sent_unknown), so whether it took this file cannot be told. Check its Info page before sending it again."
        fi
        proved=1
    fi
    if [ -n "$want_image" ] && [ "$running_image" != "$want_image" ]; then
        csg_die "Not installed: running $running_version, not the firmware in this file. It went back to the previous firmware. What it logged is in $log_path"
    fi
    if [ -n "$want_pack" ] && [ "$running_pack" != "$want_pack" ]; then
        csg_die 'Not installed: the garland is not running the effects in this file.'
    fi
    if [ "$on_trial" != true ]; then
        csg_say "Confirmed: running $running_version with $(field_of effects "$after") built-in effects."
        exit 0
    fi
    if [ -z "$said" ]; then
        said=trial
        csg_say "Running $running_version, which confirms itself after its first minute..."
    fi
done
if [ "$https_failed" -eq 1 ] && [ "$insecure" -eq 0 ]; then
    csg_die "Not confirmed: the new firmware has no working HTTPS, and a plain answer proves nothing. Its Info page and the log in $log_path say more. Its next update needs --insecure."
fi
if [ -n "$unproved" ]; then
    csg_die "Not confirmed: the garland answers with version $unproved but cannot say which firmware or effects it runs. It may have gone back to the previous firmware. What it logged is in $log_path"
fi
if [ -n "$sent_unknown" ] && [ "$went_down" -eq 0 ]; then
    csg_die "Not installed, as far as can be seen: the update got no answer ($sent_unknown), and in 180 s the garland did not restart. Check its Info page before sending it again."
fi
csg_die "The garland did not confirm the update within 180 s. If it restarted more than once it went back to the previous firmware. What it logged is in $log_path"
