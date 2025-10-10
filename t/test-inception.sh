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
# run sharness tests with -d -v
flux bulksubmit -o pty --quiet --job-name={./%} \
	sh -c './{} -d -v --root=$FLUX_JOB_TMPDIR' \
	::: t[0-9]*.t
# python and lua tests do not support -d, -v, or --root options
flux bulksubmit -o pty --quiet --job-name={./%} flux python {} ::: python/t*.py
flux bulksubmit -o pty --quiet --job-name={./%} ./{} ::: lua/t*.t
flux watch --all
RC=$?
kill $PID
wait

# dump output of failed jobs to *.log
for id in $(flux jobs -f failed -no {id}); do
    name=$(flux jobs -no {name} ${id})
    flux job attach $id > ${name}.output 2>&1
done

# print failed jobs listing to stdout
flux jobs -f failed \
    -no "{id.f58:>12} {status_abbrev:>2.2} {contextual_time!F:>8h} {name}"

# exit with failure if any tests failed
exit $RC
