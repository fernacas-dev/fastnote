#!/bin/bash
# FastNote performance test plan (§28): generate tiered files and measure
# startup, load+render, and memory headlessly (dummy video driver).
set -u
BIN="${1:-./build/fastnote}"
OUTDIR="$(mktemp -d /tmp/fastnote-perf.XXXXXX)"
trap 'rm -rf "$OUTDIR"' EXIT

gen() { # size_bytes path
    head -c "$1" /dev/urandom | base64 | fold -w 100 > "$2"
}

echo "== generating fixtures =="
gen 1024 "$OUTDIR/small.txt"
gen 1048576 "$OUTDIR/medium.txt"
gen 10485760 "$OUTDIR/large.txt"
gen 104857600 "$OUTDIR/xlarge.txt"
ls -la "$OUTDIR"

run_case() { # label file timeout_s
    echo "== $1 ($2) =="
    # GNU time can't report when timeout kills it, so sample peak RSS from
    # /proc while the app runs headlessly under the dummy video driver.
    SDL_VIDEODRIVER=dummy "$BIN" "$2" & 
    pid=$!
    peak=0
    alive=0
    ticks=$(( $3 * 5 ))
    for ((i = 0; i < ticks; i++)); do
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        alive=1
        if [ -r "/proc/$pid/status" ]; then
            hwm=$(awk '/VmHWM/ {print $2}' "/proc/$pid/status" 2>/dev/null)
            if [ -n "$hwm" ] && [ "$hwm" -gt "$peak" ] 2>/dev/null; then
                peak=$hwm
            fi
        fi
        sleep 0.2
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    if [ "$alive" = 1 ]; then
        echo "survived ${3}s headless run; peak RSS: ${peak} kB"
    else
        echo "APP EXITED EARLY (check above for errors)"
    fi
}

run_case "empty"   "/dev/null"        5
run_case "small"   "$OUTDIR/small.txt"  5
run_case "medium"  "$OUTDIR/medium.txt" 8
run_case "large"   "$OUTDIR/large.txt"  15
run_case "xlarge"  "$OUTDIR/xlarge.txt" 30

echo "done. For idle CPU, run normally and observe 'top -p <pid>'."
