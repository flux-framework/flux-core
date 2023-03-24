#!/bin/bash -e

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

echo pid=$pid
flux resource list
flux jobs -a

jobid=$(flux submit --wait-event=start sh -c "echo foo; sleep 300")

kvsdir=$(flux job id --to=kvs $jobid)

# issue a command that will watch / monitor the job's eventlog
flux job attach $jobid > t4612.out &

# ensure backgrounded process has started to monitor eventlog
$waitfile --count=1 --timeout=30 --pattern=foo t4612.out

# now overwrite the eventlog without changing its length
flux kvs get --raw ${kvsdir}.eventlog \
	| sed -e s/submit/foobar/ \
	| flux kvs put --raw ${kvsdir}.eventlog=-

wait

# if flux broker segfaulted, this won't work
flux cancel $jobid
flux job status $jobid || true
