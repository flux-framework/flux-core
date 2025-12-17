#!/bin/sh

test_description='Test perilog jobtap plugin with per-rank=true'

. $(dirname $0)/sharness.sh

test_under_flux 4 full \
	--test-exit-mode=leader

OFFLINE_PLUGIN=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs/offline.so

flux setattr log-stderr-level 1

# In case the testsuite is running as a Flux job
unset FLUX_JOB_ID

drained_ranks() { flux resource status -no {ranks} -s drain; }

no_drained_ranks() { test "$(drained_ranks)" = ""; }

undrain_all() {
	ranks="$(drained_ranks)"
	if test -n "$ranks"; then
	    flux resource undrain $ranks
	fi
}

test_expect_success 'perilog: load plugin with no config' '
	flux jobtap load perilog.so
'
test_expect_success 'perilog: query shows no prolog/epilog config' '
	test_debug "flux jobtap query perilog.so | jq .conf" &&
	flux jobtap query perilog.so \
		| jq -e ".conf.prolog == {} and .conf.epilog == {}"
'
test_expect_success 'perilog: default timeouts are expected values' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = ["prolog"]
	per-rank = true
	[job-manager.epilog]
	command = ["epilog"]
	per-rank = true
	EOF
	flux jobtap query perilog.so | jq .conf &&
	flux jobtap query perilog.so \
		| jq -e ".conf.prolog.timeout == 1800.0" &&
	flux jobtap query perilog.so \
		| jq -e ".conf.epilog.timeout == 0.0"
'
test_expect_success 'perilog: timeout can be set to 0' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	timeout = "0"
	command = [ "test" ]
	EOF
	flux jobtap query perilog.so | jq .conf &&
	flux jobtap query perilog.so \
		| jq -e ".conf.prolog.timeout == 0.0"
'
test_expect_success 'perilog: timeout can be set to infinity (equiv to 0)' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	timeout = "infinity"
	command = [ "test" ]
	EOF
	flux jobtap query perilog.so | jq .conf &&
	flux jobtap query perilog.so \
		| jq -e ".conf.prolog.timeout == 0"
'
test_expect_success 'perilog: invalid config is rejected' '
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.prolog]
	command = "foo"
	EOF
	test_debug "cat config.err.$i" &&
	grep "command must be an array" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.prolog]
	command = [ "foo" ]
	timeout = "5x"
	EOF
	test_debug "cat config.err.$i" &&
	grep "invalid prolog timeout" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.epilog]
	command = [ "foo" ]
	timeout = "5p"
	EOF
	test_debug "cat config.err.$i" &&
	grep "invalid epilog timeout" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.prolog]
	EOF
	test_debug "cat config.err.$i" &&
	grep "no command specified" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.prolog]
	command = [ 1, 2, 3 ]
	EOF
	test_debug "cat config.err.$i" &&
	grep "malformed prolog command" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.prolog]
	command = [ "foo" ]
	per-rank = true
	foo = 1
	EOF
	test_debug "cat config.err.$i" &&
	grep "1 object.*left unpacked: foo" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.perilog]
	log-ignore = "foo"
	EOF
	test_debug "cat config.err.$i" &&
	grep "not an array" config.err.$i &&
	test_must_fail flux config load <<-EOF 2>config.err.$((i+=1)) &&
	[job-manager.perilog]
	log-ignore = [ "[" ]
	EOF
	test_debug "cat config.err.$i" &&
	grep "[fF]ailed to compile" config.err.$i
'
test_expect_success 'perilog: config uses IMP with exec.imp and no command' '
	flux config load <<-EOF &&
	exec.imp = "imp"
	[job-manager.prolog]
	per-rank = true
	[job-manager.epilog]
	per-rank = true
	EOF
	flux jobtap query perilog.so \
		| jq ".conf.prolog.command == [\"imp\", \"run\", \"prolog\"]" &&
	flux jobtap query perilog.so \
		| jq ".conf.epilog.command == [\"imp\", \"run\", \"prolog\"]"
'
test_expect_success 'perilog: prolog default cancel-on-exception is true' '
	flux jobtap query perilog.so \
		| jq ".conf.prolog.cancel_on_exception == true"
'
test_expect_success 'perilog: epilog default cancel-on-exception is false' '
	flux jobtap query perilog.so \
		| jq ".conf.epilog.cancel_on_exception == false"
'
test_expect_success 'perilog: load a basic per-rank prolog config' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "flux", "getattr", "rank" ]
	EOF
	flux jobtap query perilog.so | jq .conf.prolog
'
test_expect_success 'perilog: 4 node job works with per-rank prolog' '
	jobid=$(flux submit -vvv -N4 hostname) &&
	flux job wait-event -vt 30 $jobid prolog-start &&
	flux job wait-event -t 30 $jobid prolog-finish &&
	flux job wait-event -vt 30 $jobid clean &&
	no_drained_ranks
'
test_expect_success 'perilog: stdout was copied to dmesg log' '
	flux dmesg -H &&
	flux dmesg -H | grep "$jobid: prolog:.*rank 0.*stdout: 0" &&
	flux dmesg -H | grep "$jobid: prolog:.*rank 1.*stdout: 1" &&
	flux dmesg -H | grep "$jobid: prolog:.*rank 2.*stdout: 2" &&
	flux dmesg -H | grep "$jobid: prolog:.*rank 3.*stdout: 3"
'
test_expect_success 'perilog: load a basic per-rank prolog config' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sleep", "30" ]
	[job-manager.epilog]
	per-rank = true
	command = [ "sleep", "30" ]
	cancel-on-exception = true
	EOF
	flux jobtap query perilog.so | jq .conf.prolog
'
test_expect_success 'perilog: prolog runs on all 4 ranks of a 4 node job' '
	flux dmesg -c &&
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vt 30 $jobid prolog-start &&
	flux jobtap query perilog.so | jq .procs &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active_ranks == \"0-3\"" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.total == 4" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active == 4" &&
	flux cancel $jobid &&
	flux jobtap query perilog.so &&
	flux job wait-event $jobid prolog-finish
'
test_expect_success 'perilog: canceled prolog does not drain ranks' '
	no_drained_ranks
'
test_expect_success 'perilog: epilog runs even if prolog is canceled' '
	flux dmesg -H &&
	flux job wait-event -vHt 30 $jobid epilog-start &&
	flux jobtap query perilog.so | jq .procs &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.name == \"epilog\"" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active_ranks == \"0-3\"" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.total == 4" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active == 4" &&
	flux cancel $jobid &&
	flux jobtap query perilog.so | jq &&
	flux job wait-event -Hvt 30 $jobid clean
'
test_expect_success 'perilog: epilog triggered by prolog has correct userid' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sleep", "5" ]
	[job-manager.epilog]
	per-rank = true
	command = [ "sh", "-c", "echo FLUX_JOB_USERID=\$FLUX_JOB_USERID" ]
	EOF
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vHt 30 $jobid prolog-start &&
	flux cancel $jobid &&
	flux job wait-event -vHt 30 $jobid clean &&
	flux dmesg -H  | grep FLUX_JOB_USERID=$(id -u)
'
test_expect_success 'perilog: canceled prolog drains active ranks after kill_timeout' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh", "-c", "trap \"\" 15; if test \$(flux getattr rank) -eq 3; then sleep 10; fi" ]
	kill-timeout = 0.5
	EOF
	flux jobtap query perilog.so | jq .conf.prolog &&
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event $jobid prolog-start &&
	flux cancel $jobid &&
	flux job wait-event $jobid prolog-finish &&
	test_debug "echo drained_ranks=$(drained_ranks)" &&
	test "$(drained_ranks)" = "3" &&
	flux resource drain -no {reason} | grep "canceled then timed out" &&
	flux job wait-event -vt 15 $jobid clean &&
	flux resource undrain 3
'
test_expect_success 'perilog: signaled prolog is reported' '
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh", "-c", "kill \$\$" ]
	EOF
	jobid=$(flux submit hostname) &&
	flux job wait-event -vHt 30 $jobid exception &&
	flux job wait-event -t 30 $jobid exception >exception.out &&
	grep "prolog killed by signal 15" exception.out
'
test_expect_success 'perilog: prolog failure drains affected ranks' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh",
	            "-c",
	            "if test \$(flux getattr rank) -eq 3; then exit 1; fi" ]
	EOF
	flux dmesg -C &&
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vHt 30 $jobid prolog-start &&
	flux jobtap query perilog.so | jq .procs.$jobid &&
	test_must_fail flux job attach $jobid &&
	flux dmesg -H &&
	flux resource drain -no "{ranks} {reason}" &&
	test "$(drained_ranks)" = "3" &&
	test "$(flux resource drain -no {reason})" = "prolog failed for job $jobid" &&
	flux resource drain
'
test_expect_success 'perilog: prolog timeout works and drains ranks' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	timeout = "1s"
	per-rank = true
	command = [ "sh",
	            "-c",
	            "if test \$(flux getattr rank) -eq 3; then sleep 30; fi" ]
	EOF
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -Hvt 30 $jobid prolog-start &&
	flux jobtap query perilog.so | jq .procs.$jobid &&
	test_must_fail flux job attach $jobid &&
	test "$(drained_ranks)" = "3" &&
	test "$(flux resource drain -no {reason})" = "prolog timed out for job $jobid" &&
	undrain_all
'
test_expect_success 'perilog: prolog can drain multiple ranks' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "false" ]
	EOF
	test_must_fail flux run -N4 hostname &&
	flux resource drain -o long &&
	test "$(drained_ranks)" = "0-3" &&
	undrain_all
'
test_expect_success 'perilog: nonfatal exception does not cancel prolog' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh",
                    "-c",
		    """flux job raise -s2 --type=test \$FLUX_JOB_ID
                    flux job wait-event \$FLUX_JOB_ID exception""" ]
	EOF
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux job wait-event -t 15 $jobid clean &&
	flux jobs $jobid &&
	flux job status -vvv $jobid &&
	no_drained_ranks
'
test_expect_success 'perilog: job can time out after prolog' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sleep", "1" ]
	EOF
	jobid=$(flux submit -t0.5s sleep 10) &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux job wait-event -vt 15 $jobid exception &&
	flux job wait-event -vt 15 $jobid clean &&
	flux job status -v $jobid 2>&1 | grep type=timeout
'
test_expect_success 'perilog: job can be canceled after prolog is complete' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sleep", "0" ]
	EOF
	jobid=$(flux submit sleep 300) &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux cancel $jobid &&
	flux job wait-event -t 15 $jobid exception &&
	test_must_fail_or_be_terminated flux job status $jobid
'
test_expect_success 'perilog: epilog runs on all ranks with per-rank' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.epilog]
	per-rank = true
	cancel-on-exception = true
	command = [ "sleep", "15" ]
	EOF
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vHt 30 $jobid epilog-start &&
	flux jobtap query perilog.so | jq .procs.$jobid &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active_ranks == \"0-3\"" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.total == 4" &&
	flux jobtap query perilog.so \
		| jq -e ".procs.$jobid.active == 4" &&
	flux jobtap query perilog.so \
		| jq -e ".conf.epilog.cancel_on_exception == true" &&
	flux cancel $jobid &&
	flux jobtap query perilog.so | jq &&
	flux job wait-event -vHt 20 $jobid clean
'
test_expect_success 'perilog: canceled epilog does not drain ranks' '
	no_drained_ranks
'
test_expect_success 'perilog: epilog failure drains ranks' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.epilog]
	per-rank = true
	command = [ "sh",
	            "-c",
                    "if test \$(flux getattr rank) -eq 1; then exit 1; fi" ]
	EOF
	flux jobtap query perilog.so | jq &&
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vHt 30 $jobid epilog-finish &&
	flux resource drain -o long &&
	test "$(drained_ranks)" = "1" &&
	test "$(flux resource drain -no {reason})" = "epilog failed for job $jobid" &&
	undrain_all
'
test_expect_success 'perilog: epilog failure raises non-fatal job exception' '
	flux job wait-event -vHt 30 $jobid exception
'
test_expect_success 'perilog: job does not start when prolog cancel times out' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh",
	            "-c",
	            "trap \"\" 15; sleep 2" ]
	kill-timeout = 1
	timeout = ".25s"
	[job-manager.epilog]
	per-rank = true
	command = [ "true" ]
	EOF
	flux jobtap query perilog.so | jq &&
	jobid=$(flux submit hostname) &&
	flux job wait-event -vHt 30 $jobid clean &&
	flux job eventlog -H $jobid >prolog-cancel-eventlog.out &&
	cat prolog-cancel-eventlog.out &&
	test_must_fail grep "start$" prolog-cancel-eventlog.out &&
	grep epilog-start prolog-cancel-eventlog.out
'
test_expect_success 'perilog: prolog has FLUX_JOB_RANKS environment variable' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	per-rank = true
	command = [ "sh", "-c", "test \$FLUX_JOB_RANKS\" = \"0-3\"" ]
	EOF
	jobid=$(flux submit -N4 hostname) &&
	flux job wait-event -vHt 30 $jobid prolog-start &&
	flux job wait-event -vHt 30 $jobid clean
'

test_expect_success 'perilog: log-ignore works' '
	undrain_all &&
	flux config load <<-EOF &&
	[job-manager.prolog]
	command = [ "printf", "foo: whee!\nbar: woo!\nbaz: important!\n" ]
	[job-manager.perilog]
	log-ignore = [ "^foo:.*", "^bar:" ]
	EOF
	flux dmesg -c >/dev/null &&
	flux run hostname &&
	flux dmesg -H > dmesg.out &&
	test_debug "cat dmesg.out" &&
	test_must_fail grep foo: dmesg.out &&
	test_must_fail grep bar: dmesg.out &&
	grep baz: dmesg.out
'
test_expect_success 'perilog: load offline plugin before perilog.so' '
	flux jobtap remove perilog.so &&
	flux jobtap load $OFFLINE_PLUGIN &&
	flux jobtap load perilog.so
'
test_expect_success 'perilog: load simple prolog for offline rank testing' '
	undrain_all &&
	flux config load <<-EOF
	[job-manager.prolog]
	per-rank = true
	command = [ "flux", "getattr", "rank" ]
	EOF
'
# Note: bulk-exec does not return an error code on EHOSTUNREACH since
# it is assumed a job exception will be raised by the instance.
# Here we are just checking that the prolog finishes without blocking the
# job from moving to CLEANUP/INACTIVE.
test_expect_success 'perilog: prolog is successful with offline rank' '
	undrain_all &&
	jobid=$(flux submit -N4 -n4 true) &&
	flux job wait-event -t 30 $jobid prolog-start &&
	flux job wait-event -vHt 30 $jobid prolog-finish &&
	flux jobtap remove offline.so
'
test_done
