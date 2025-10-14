#!/bin/bash -e
#
#  Test that batch/alloc jobs are resilient to leaf not failures
#

log ()
{
    printf "free-range-test: $@"
}

list_descendants ()
{
    local children=$(ps -o pid= --ppid "$1")

    for pid in $children; do
        list_descendants "$pid"
    done

    echo "$children"
}


#  Check if we need to start parent job, if so, reexec under flux-start
#  using the per-broker for test-pmi-clique so that the instance mapping
#  simulates multiple nodes.
#
export FLUX_URI_RESOLVE_LOCAL=t
if test "$FREE_RANGE_TEST_ACTIVE" != "t"; then
    export FREE_RANGE_TEST_ACTIVE=t
    log "Re-launching test script under flux-start\n"
    exec flux start -s 4 \
        --test-exit-mode=leader \
        --test-pmi-clique=per-broker \
        -Stbon.topo=kary:0 $0
fi

#  Start a job with tbon.topo=kary:0
log "Starting a child instance with flat topology\n"
jobid=$(flux alloc -N4 --bg --broker-opts=-Stbon.topo=kary:0)

log "Started job $jobid\n"

#  Run a job on all ranks of child job
log "Current overlay status of $jobid:\n"
flux proxy $jobid flux overlay status

log "Launch a sleep job within $jobid:\n"
flux proxy $jobid flux submit -N4 sleep inf

flux pstree -x --skip-root=no

#  Now simulate a node failure by killing a broker in parent
#  instance, along with all its children
broker_pid=$(flux exec -r 3 flux getattr broker.pid)
descendants=$(list_descendants $broker_pid)

log "disconnect rank 3 broker to simulate node crash\n"
flux overlay disconnect 3

log "Wait for exception event in $jobid\n"
flux job wait-event -vt 100 $jobid exception

log "Killing rank 3 broker (pid %d) and all children\n" $broker_pid
kill -9 $descendants $broker_pid

log "But running a 3 node job in $jobid still works:\n"
flux proxy $jobid flux run -vvv -t 100s -N3 hostname

log "Overlay status of $jobid should show rank lost:\n"
flux proxy $jobid flux overlay status

log "Call flux shutdown on $jobid\n"
flux shutdown $jobid

log "job $jobid should exit cleanly (no hang) and a zero exit code:\n"
flux job wait-event -vt 100 $jobid finish

log "dump output from job:\n\n"
flux job attach -vEX $jobid
rc=$?
printf "\n"
log "flux-job attach exited with code=$rc\n"

exit $rc
