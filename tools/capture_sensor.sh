#!/usr/bin/env bash
# Camera-side sensor trace capture, via NFS-staged ipctool.
#
# Three subcommands:
#
#   build       Build ipctool for ARM (CI-canonical toolchain) and UPX-pack
#               so the binary runs on both OpenIPC and XiongMai HiLinux kernels.
#               Stages at $NFS_LOCAL/ipctool-upx.
#
#   majestic    Capture from an OpenIPC/Majestic camera over ssh. Kills the
#               running majestic, starts a fresh one under `ipctool trace
#               --output=PATH`, lets it run for $SECS seconds, kills it, and
#               restarts majestic via /etc/init.d/S95majestic.
#
#   sofia       Capture from a XiongMai/Sofia camera over telnet (expect).
#               Uses bind-mount over /usr/bin/Sofia (because XmServices_Mgr is
#               not a supervisor), then reboots the camera to clean up.
#
# Environment defaults (override on the command line):
#
#   NFS_HOST=10.216.128.227         NFS server holding /utils content
#   NFS_LOCAL=/mnt/noc              local mount point of the NFS share
#   NFS_REMOTE=/srv/nfsroot         path exported by the NFS server
#                                   mounted as /utils on the camera
#
# After capture, post-process locally with:
#   tools/trace_segment.py    <log>
#   tools/trace_to_driver.py  <log>.segments.json --sensor sc2315e
#   tools/trace_diff.py       <generated.c> <reference.c> \
#                             --gen-scope sc2315e_linear_init \
#                             --ref-scope sc2315e_linear_1080P30_init

set -euo pipefail

NFS_HOST="${NFS_HOST:-10.216.128.227}"
NFS_LOCAL="${NFS_LOCAL:-/mnt/noc}"
NFS_REMOTE="${NFS_REMOTE:-/srv/nfsroot}"

OPENIPC_TOOLCHAIN_URL="https://github.com/OpenIPC/firmware/releases/download/toolchain/toolchain.hisilicon-hi3516cv100.tgz"
OPENIPC_TOOLCHAIN_DIR="/tmp/openipc-tc"
OPENIPC_GCC="$OPENIPC_TOOLCHAIN_DIR/arm-openipc-linux-musleabi_sdk-buildroot/bin/arm-openipc-linux-musleabi-gcc"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
log() { printf '== %s\n' "$*" >&2; }

usage() {
    awk '/^# Camera-side/,/^set -/{ if (/^set -/) exit; sub(/^# ?/,""); print }' "$0"
    echo
    echo "Usage:"
    echo "  $0 build"
    echo "  $0 majestic --host HOST [--user USER] [--secs N] [--out FILE]"
    echo "  $0 sofia    --host HOST [--password P] [--secs N] [--out FILE]"
    exit 1
}

# ----- build -----

cmd_build() {
    [ -d "$NFS_LOCAL" ] || die "$NFS_LOCAL not present (NFS not mounted?)"
    command -v upx >/dev/null || die "upx not installed"

    if [ ! -x "$OPENIPC_GCC" ]; then
        log "downloading OpenIPC CI toolchain"
        mkdir -p "$OPENIPC_TOOLCHAIN_DIR"
        curl -sL "$OPENIPC_TOOLCHAIN_URL" \
            | tar xzf - -C "$OPENIPC_TOOLCHAIN_DIR"
    fi

    local builddir=build-arm-ci
    rm -rf "$builddir"
    PATH="$(dirname "$OPENIPC_GCC"):$PATH" \
        cmake -H. -B"$builddir" \
            -DCMAKE_C_COMPILER=arm-openipc-linux-musleabi-gcc \
            -DCMAKE_BUILD_TYPE=Release >/dev/null
    PATH="$(dirname "$OPENIPC_GCC"):$PATH" \
        cmake --build "$builddir" >/dev/null

    log "UPX-packing"
    cp "$builddir/ipctool" "$NFS_LOCAL/ipctool-upx.unpacked"
    upx --best -q "$NFS_LOCAL/ipctool-upx.unpacked" \
        -o "$NFS_LOCAL/ipctool-upx" >/dev/null
    rm -f "$NFS_LOCAL/ipctool-upx.unpacked"

    ls -la "$NFS_LOCAL/ipctool-upx"
    log "staged $NFS_LOCAL/ipctool-upx (=> /utils/ipctool-upx on the camera)"
}

# ----- majestic (ssh, OpenIPC) -----

cmd_majestic() {
    local host="" user="root" secs=35 out=""
    while [ $# -gt 0 ]; do
        case $1 in
            --host) host=$2; shift 2 ;;
            --user) user=$2; shift 2 ;;
            --secs) secs=$2; shift 2 ;;
            --out)  out=$2;  shift 2 ;;
            *)      die "unknown majestic arg: $1" ;;
        esac
    done
    [ -n "$host" ] || die "--host required"
    [ -n "$out" ]  || out="tools/dumps/majestic-$(date +%s).log"

    [ -x "$NFS_LOCAL/ipctool-upx" ] \
        || die "$NFS_LOCAL/ipctool-upx missing - run '$0 build' first"

    local remote_log=/utils/dumps/majestic-capture.log
    log "capturing $secs s from $host -> $out"
    ssh -o StrictHostKeyChecking=accept-new "$user@$host" "
        set -e
        mount -o nolock,vers=3 $NFS_HOST:$NFS_REMOTE /utils 2>/dev/null || true
        mkdir -p /utils/dumps
        rm -f $remote_log
        killall -q majestic 2>/dev/null || true
        sleep 1
        /utils/ipctool-upx trace --output=$remote_log /usr/bin/majestic \
            >/dev/null 2>&1 &
        TRACE_PID=\$!
        sleep $secs
        kill -TERM \$TRACE_PID 2>/dev/null || true
        killall -q -TERM ipctool-upx majestic 2>/dev/null || true
        sleep 2
        killall -q -KILL ipctool-upx majestic 2>/dev/null || true
        /etc/init.d/S95majestic start >/dev/null 2>&1
        wc -l $remote_log
    "
    cp "$NFS_LOCAL/dumps/majestic-capture.log" "$out"
    log "saved $out"
    wc -l "$out"
}

# ----- sofia (telnet, XiongMai HiLinux) -----

cmd_sofia() {
    command -v expect >/dev/null || die "expect required for sofia mode"

    local host="" password="xmhdipc" secs=55 out=""
    while [ $# -gt 0 ]; do
        case $1 in
            --host)     host=$2;     shift 2 ;;
            --password) password=$2; shift 2 ;;
            --secs)     secs=$2;     shift 2 ;;
            --out)      out=$2;      shift 2 ;;
            *)          die "unknown sofia arg: $1" ;;
        esac
    done
    [ -n "$host" ] || die "--host required"
    [ -n "$out" ]  || out="tools/dumps/sofia-$(date +%s).log"

    [ -x "$NFS_LOCAL/ipctool-upx" ] \
        || die "$NFS_LOCAL/ipctool-upx missing - run '$0 build' first"

    local remote_log=/utils/dumps/sofia-capture.log
    log "capturing $secs s from $host (telnet, will reboot afterwards)"

    # Generous timeout: 60s setup + capture + cleanup
    local total=$((secs + 30))

    expect <<EXPECT >/dev/null
set timeout $total
log_user 0
spawn telnet $host
expect {
    -re "login:"    { send "root\r"; exp_continue }
    -re "Password:" { send "$password\r"; exp_continue }
    -re "(#|\\\$) ?\$" {}
    timeout { puts "could not connect"; exit 1 }
}

# Mount NFS, set up bind-mount wrapper
send "mount -o nolock,vers=3 $NFS_HOST:$NFS_REMOTE /utils 2>/dev/null; mkdir -p /utils/dumps; rm -f $remote_log\r"
expect -re "(#|\\\$) ?\$"
send "cp /usr/bin/Sofia /utils/Sofia.real\r"
expect -re "(#|\\\$) ?\$"
send "cat > /utils/sofia.wrap.sh <<'EOF'\r"
expect -re ">"
send "#!/bin/sh\r"
expect -re ">"
send "exec /utils/ipctool-upx trace --skip=usleep --output=$remote_log /utils/Sofia.real\r"
expect -re ">"
send "EOF\r"
expect -re "(#|\\\$) ?\$"
send "chmod +x /utils/sofia.wrap.sh; mount --bind /utils/sofia.wrap.sh /usr/bin/Sofia\r"
expect -re "(#|\\\$) ?\$"

# Kill chain, manually start SofiaRun.sh through the wrapper. stdin redirect
# avoids SIGTTIN on the backgrounded chain.
send "killall Sofia SofiaRun.sh 2>/dev/null; sleep 1; killall -9 Sofia SofiaRun.sh 2>/dev/null; sleep 1\r"
expect -re "(#|\\\$) ?\$"
send "/usr/sbin/SofiaRun.sh </dev/null >/dev/null 2>&1 &\r"
expect -re "(#|\\\$) ?\$"

send "sleep $secs; echo capture-done\r"
expect -re "capture-done"
expect -re "(#|\\\$) ?\$"

# Cleanup via reboot - the bind-mount goes away with it
send "ls -la $remote_log\r"
expect -re "(#|\\\$) ?\$"
send "reboot\r"
EXPECT

    # File arrives on NFS even with the camera mid-reboot
    sleep 5
    [ -f "$NFS_LOCAL/dumps/sofia-capture.log" ] \
        || die "no $NFS_LOCAL/dumps/sofia-capture.log produced"
    cp "$NFS_LOCAL/dumps/sofia-capture.log" "$out"
    log "saved $out (camera is rebooting)"
    wc -l "$out"
}

# ----- main -----

[ $# -ge 1 ] || usage
sub=$1; shift
case $sub in
    build)    cmd_build "$@" ;;
    majestic) cmd_majestic "$@" ;;
    sofia)    cmd_sofia "$@" ;;
    -h|--help|help) usage ;;
    *)        die "unknown subcommand: $sub" ;;
esac
