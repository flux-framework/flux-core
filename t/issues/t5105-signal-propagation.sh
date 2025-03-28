#!/bin/sh
# Run an 3 level nested instance, with a test job at the final level

waitfile=$SHARNESS_TEST_SRCDIR/scripts/waitfile.lua

cat <<EOF >test.py
import signal
import time
import flux
import os

h = flux.Flux()
level = h.attr_get("instance-level")
jobid = os.getenv("FLUX_JOB_ID")
signal.signal(
    signal.SIGUSR1,
    lambda x, y: print(f"job {jobid} in level {level} got SIGUSR1", flush=True),
)
open("ready", 'a').close()
signal.pause()
EOF

id=$(flux submit --output=log flux start \
     flux run flux start \
     flux run flux python ./test.py)

$waitfile -t 100 -v ready

flux job kill -s SIGUSR1 $id

$waitfile -t 100 -v -p "got SIGUSR1" log

flux job status --json -v $id || true

# vi: ts=4 sw=4 expandtab

