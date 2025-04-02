#!/bin/sh
# reactor_run() is uninterruptible if callback makes synchronous RPC

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

log() { echo "t3432: $@" >&2; }
die() { log "$@"; cat t3423.log >&2; exit 1; }

cat <<EOF >reactor-intr.py || die "Failed to create test script"
import sys
import flux
from flux.core.watchers import TimerWatcher

def timeout_cb(handle, watcher, revents, _args):
    raise OSError("Timed out!")

def startup_cb(handle, watcher, revents, _args):
    print("sending ping request")
    h.rpc("kvs.ping", {}).then(ping_cb, do_sync=True)

def ping_cb(rpc, do_sync=False):
    print("ping_cb")
    print(rpc.get_str())
    if do_sync:
       print("synchronous ping in ping_cb:")
       print(rpc.get_flux().rpc("kvs.ping", {}).get_str())
       print("ready.")
       sys.stdout.flush()

do_sync = len(sys.argv) > 1 and sys.argv[1] == "sync"

h = flux.Flux()

print("starting timer watchers")
tw1 = TimerWatcher(h, 0.01, startup_cb)
tw2 = TimerWatcher(h, 20., timeout_cb)

print("starting reactor_run")
try:
    tw1.start()
    tw2.start()
    h.reactor_run()
except KeyboardInterrupt as exc:
    print("Got KeyboardInterrupt. Exiting...")
    pass
EOF

log "Created test script reactor-intr.py"

flux python reactor-intr.py sync >t3432.log 2>&1 &
pid=$!

log "Started PID=$pid"

$waitfile --timeout=10 --pattern="^ready" t3432.log || die "waitfile failed"

log "Sending SIGINT to $pid"
kill -INT $pid || die "Failed to kill PID $pid"

log "Waiting for $pid to exit"
wait $pid
STATUS=$?

test $STATUS -eq 0 || die "process exited with $STATUS expected 0"
log "Python script exited with status $STATUS"
