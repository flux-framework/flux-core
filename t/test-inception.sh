#!/bin/bash
#
#  Run all Flux tests as Flux jobs using bulksubmit.
#  This must be run from the sharness test directory ./t.
#

stats() {
    while :; do
       echo === $(flux jobs --stats-only) ===
       sleep 5
    done 
}

stats &
PID=$!
export FLUX_TESTS_LOGFILE=t
flux bulksubmit -o pty --watch --quiet \
	sh -c './{} --root=$FLUX_JOB_TMPDIR' \
	::: t[0-9]*.t python/t*.py lua/t*.t
RC=$?
kill $PID
wait

# dump output of failed jobs to *.log
for id in $(flux jobs -f failed -no {id}); do
    name=$(flux jobs -no {name} ${id})
    flux job attach $id > ${name/.t}.log 2>&1
done
 
# print failed jobs listing to stdout
flux jobs -f failed

# exit with failure if any tests failed
exit $RC
