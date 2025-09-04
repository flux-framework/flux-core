#!/bin/sh

log() { printf "t2492: $@\n" >&2; }
die() { log "$@"; exit 1; }

#  Check if we need to start parent job, if so, reexec under flux-start
#  using the per-broker for test-pmi-clique so that the instance mapping
#  simulates multiple nodes.
#
export FLUX_URI_RESOLVE_LOCAL=t
if test "$T2492_ACTIVE" != "t"; then
    export T2492_ACTIVE=t
    log "Re-launching test script under flux-start using $(command -v flux)"
    exec flux start -s 4 $0
fi

CRITICAL_RANKS=$(cat <<EOF
import os
import sys
import time
import flux
import subprocess

try:
    h = flux.Flux()
    #
    # Wait on barrier to ensure all tasks have started
    # (prevents premature termintion by SIGINT while Python is initiating)
    #
    size = int(os.environ["FLUX_JOB_SIZE"])
    taskid = int(os.environ["FLUX_TASK_RANK"])
    if taskid == 0:
        print(f"waiting on barrier for {size} tasks", flush=True)
    subprocess.run(["flux", "pmi", "barrier"], close_fds=False)
    if taskid == 0:
        print(f"exited barrier", flush=True)

    # Kill job shell on broker rank 3
    broker_rank = h.get_rank()
    if broker_rank == int(sys.argv[1]):
        #  kill job shell
        os.kill(os.getppid(), 9)
        sys.exit(0)

    # Sleep in all other tasks
    time.sleep(600)
except KeyboardInterrupt:
    sys.exit(0)
EOF
)
for rank in 3 1; do
    log ""
    log "Testing handling of lost shell rank $rank:"

    id=$(flux submit -N4 --tasks-per-node=1 \
         --input=/dev/null \
         -o exit-timeout=none \
         --add-file=critical.py="${CRITICAL_RANKS}" \
         flux python {{tmpdir}}/critical.py $rank)

    log "Submitted job $id. Waiting for shell rank $rank to be lost"

    value="shell rank $rank (on $(hostname)): Killed"
    flux job wait-event -Wt 120 -Hvp output -m message="$value" $id log \
	|| die "failed to get shell lost message"

    log "Sending SIGINT to $id. Job should now exit"
    flux job kill --signal=2 $id
    flux job attach -vEX $id
    rc=$?
    log "Job exited with rc=$rc (expecting 137 (128+9))"
    test $rc -eq 137 || die "Unexpected job exit code $rc"
done


