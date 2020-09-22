#!/bin/sh
# future/rpc.get() should be interruptible:

SERVICE="t3186"
waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

log() { echo "t3186: $@" >&2; }
die() { log "$@"; exit 1; }

cat <<EOF >get-intr.py || die "Failed to create test script"
import flux
from flux.future import Future

f = flux.Flux()
Future(f.service_register("$SERVICE")).get()
print("get-intr.py: Added service $SERVICE", flush=True)

# The following should block until interrupted:
f.rpc("${SERVICE}.echo").get()
EOF

log "Created test script get-intr.py"

flux python get-intr.py >t3186.log 2>&1 &
pid=$!

log "Started PID=$pid"

$waitfile --timeout=10 --pattern="Added service" t3186.log

log "Sending SIGINT to $pid"
kill -INT $pid || die "Failed to kill PID $pid"

while kill -0 $pid; do
    log "PID $pid still running, trying again"
    kill -INT $pid
    sleep 0.25
done

log "Waiting for $pid to exit"
wait $!
STATUS=$?

test $STATUS -eq 130 || die "process exited with $STATUS expected 130"
log "Python script exited with status $STATUS"
