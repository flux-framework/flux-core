#!/bin/sh

test_description='Test flux job execution service with dummy job shell'

. $(dirname $0)/sharness.sh

#  Configure dummy job shell:
if ! test -f dummy.toml; then
	cat <<-EOF >dummy.toml
	[exec]
	job-shell = "$SHARNESS_TEST_SRCDIR/job-exec/dummy.sh"
	EOF
fi

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job

flux setattr log-stderr-level 1

job_kvsdir()    { flux job id --to=kvs $1; }
exec_eventlog() { flux kvs get -r $(job_kvsdir $1).guest.exec.eventlog; }


test_expect_success 'job-exec: execute dummy job shell across all ranks' '
	id=$(flux submit -n4 -N4 \
		"flux kvs put test1.\$BROKER_RANK=\$JOB_SHELL_RANK") &&
	flux job wait-event $id clean &&
	kvsdir=$(flux job id --to=kvs $id).guest &&
	test $(flux kvs get ${kvsdir}.test1.0) = 0 &&
	test $(flux kvs get ${kvsdir}.test1.1) = 1 &&
	test $(flux kvs get ${kvsdir}.test1.2) = 2 &&
	test $(flux kvs get ${kvsdir}.test1.3) = 3
'
test_expect_success 'job-exec: job shell output available in flux-job attach' '
	id=$(flux submit "echo Hello from job \$JOBID") &&
	flux job attach -vEX $id &&
	flux job attach $id 2>&1 | grep "dummy.sh.*Hello from job"
'
test_expect_success 'job-exec: job shell failure recorded' '
	id=$(flux submit -n4 -N4  "test \$JOB_SHELL_RANK = 0 && exit 1") &&
	flux job wait-event -vt 10 $id finish | grep status=256
'
test_expect_success 'job-exec: status is maximum job shell exit codes' '
	id=$(flux submit -n4 -N4 "exit \$JOB_SHELL_RANK") &&
	flux job wait-event -vt 10 $id finish | grep status=768
'
test_expect_success 'job-exec: job exception kills job shells' '
	id=$(flux submit -n4 -N4 sleep 300) &&
	flux job wait-event -vt 5 $id start &&
	flux job wait-event -vt 5 -p exec $id shell.start &&
	flux cancel $id &&
	flux job wait-event -vt 5 $id clean &&
	flux job eventlog $id | grep -E "status=(15|36608)"
'
test_expect_success 'job-exec: job exception uses SIGKILL after kill-timeout' '
	flux module reload job-exec kill-timeout=0.2 &&
	cat <<-EOF >trap-sigterm.sh &&
	#!/bin/sh
	trap "echo trap-sigterm got SIGTERM >&2" 15
	flux kvs put trap-ready=1
	sleep 60 &
	pid=\$!
	wait \$pid
	sleep 60
	EOF
	chmod +x trap-sigterm.sh &&
	id=$(TRAP=15 flux submit -n4 -N4 ./trap-sigterm.sh) &&
	flux job wait-event -vt 5 $id start &&
	flux kvs get --waitcreate \
		--namespace=$(flux job namespace $id) \
		trap-ready &&
	flux cancel $id &&
	(flux job attach -vEX $id >kill.output 2>&1 || true) &&
	test_debug "cat kill.output" &&
	grep "trap-sigterm got SIGTERM" kill.output
'
test_expect_success 'job-exec: job shell eventually killed by SIGKILL' '
	id=$(flux submit --wait-event=start -n1 \
	     sh -c "trap \"\" SIGTERM;
                    flux kvs put ready=1;
	            while true; do sleep 1; done") &&
	flux kvs get --waitcreate \
		--namespace=$(flux job namespace $id) \
		ready &&
	flux cancel $id &&
	flux job wait-event -vt 15 $id clean &&
	flux dmesg | grep $(flux job id --to=f58 $id) &&
	test_expect_code 137 flux job status $id &&
	flux module reload job-exec
'
test_expect_success 'job-exec: invalid job shell generates exception' '
	id=$(flux run --dry-run /bin/true \
		| $jq ".attributes.system.exec.job_shell = \"/notthere\"" \
		| flux job submit) &&
	flux job wait-event -vt 5 $id clean
'
test_expect_success 'job-exec: invalid bulkexec key in jobspec raises error' '
	flux dmesg -C &&
	test_must_fail flux run --setattr=exec.bulkexec.foo=bar hostname &&
	flux dmesg -H | grep "failed to unpack system.exec.bulkexec"
'
test_expect_success 'job-exec: exception during init terminates job' '
	id=$(flux run --dry-run -n2 -N2 sleep 30 \
		| $jq ".attributes.system.exec.bulkexec.mock_exception = \"init\"" \
		| flux job submit) &&
	flux job wait-event -vt 5 $id clean
'
test_expect_success 'job-exec: exception while starting terminates job' '
	id=$(flux run --dry-run -n2 -N2 sleep 30 \
		| $jq ".attributes.system.exec.bulkexec.mock_exception = \"starting\"" \
		| flux job submit) &&
	flux job wait-event -vt 5 $id clean
'
test_expect_success 'job-exec: failure after first barrier terminates job' '
	for id in $(flux bulksubmit --env FAIL_MODE={} -N2 -n2 sleep 300 \
		    ::: before_barrier_entry after_barrier_entry); do
		echo checking on job $id &&
		flux job wait-event -vt 600 $id clean &&
		test_must_fail flux job status -v $id
	done
'
test_done
