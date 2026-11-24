#!/bin/sh

test_description='test flux debug alloc-check with a double-booking scheduler'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

SCHED_DUPALLOC=${SHARNESS_TEST_SRCDIR}/scheduler/sched-dupalloc.py

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

# ISO 8601 timestamp with microseconds, as emitted by flux dmesg and
# flux job eventlog -T iso
ISO='[0-9]\{4\}-[0-9][0-9]-[0-9][0-9]T[0-9:]*\.[0-9]\{6\}\(Z\|[-+][0-9:]*\)'

test_under_flux 2 full -Slog-stderr-level=1

# Usage: f58 jobid
#  Normalize jobid to the f58 form used in flux debug alloc-check output
f58() {
	flux job id --to=f58 $1
}

# Usage: submit_as_alternate_user [flux run args...]
#  Submit a job as userid 42 and print its jobid
submit_as_alternate_user() {
	flux run --dry-run "$@" | \
		flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py 42 \
		>job.signed &&
	FLUX_HANDLE_USERID=42 flux job submit --flags=signed job.signed
}

# Usage: hk_count
hk_count() {
	flux housekeeping list -no {id} | wc -l
}

# Usage: wait_for_hk count
wait_for_hk() {
	test_wait_until -i 300 -s 0.1 "test \$(hk_count) -eq $1"
}

test_expect_success 'flux debug alloc-check --help works' '
	flux debug alloc-check --help >help.out &&
	grep -- --no-collect help.out
'
test_expect_success 'unload sched-simple, load sched-dupalloc' '
	flux module unload sched-simple &&
	flux module load ${SCHED_DUPALLOC}
'
test_expect_success 'dup-alloc job is denied with no previous allocation' '
	test_must_fail flux run --setattr=system.dup-alloc=1 true 2>deny.err &&
	grep "no previous allocation" deny.err
'
test_expect_success 'flux debug alloc-check succeeds on idle instance' '
	flux debug alloc-check -o idle 2>idle.err &&
	grep "no overlapping allocations" idle.err &&
	grep "job-manager and scheduler agree" idle.err &&
	test_must_fail test -e idle.tar.gz
'
test_expect_success 'start a normal job' '
	jobA=$(f58 $(flux submit --wait-event=start -n1 sleep inf))
'
test_expect_success 'flux debug alloc-check succeeds with one running job' '
	flux debug alloc-check -o ok 2>ok.err &&
	grep "checked 1 allocations" ok.err &&
	test_must_fail grep "conflict:" ok.err &&
	test_must_fail test -e ok.tar.gz
'
test_expect_success 'flux debug alloc-check fails without owner credentials' '
	test_expect_code 1 env FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux debug alloc-check -n 2>guest.err &&
	test_debug "cat guest.err" &&
	grep "ERROR: [a-z.-]*: .*owner" guest.err &&
	test_must_fail grep "checked" guest.err
'
test_expect_success 'start a job that duplicates the first allocation' '
	jobB=$(f58 $(flux submit --wait-event=start -n1 \
		--setattr=system.dup-alloc=1 sleep inf)) &&
	test "$(flux job info $jobA R | jq -S .execution.R_lite)" = \
	     "$(flux job info $jobB R | jq -S .execution.R_lite)"
'
test_expect_success 'flux resource list reports duplicate allocation' '
	test_must_fail flux resource list 2>rlist.err &&
	grep "duplicate allocation" rlist.err
'
test_expect_success 'flux debug alloc-check detects the duplicate' '
	test_expect_code 1 flux debug alloc-check -o dup 2>dup.err &&
	test_debug "cat dup.err" &&
	grep "WARNING: job-manager.resource-status: .*duplicate allocation" \
		dup.err &&
	grep "ERROR: conflict:" dup.err >conflict.out &&
	test $(wc -l <conflict.out) -eq 1 &&
	grep "job $jobA" conflict.out &&
	grep "job $jobB" conflict.out &&
	grep "ERROR:    overlap: rank" dup.err &&
	grep "ERROR:    job $jobA: alloc " dup.err &&
	grep "ERROR:    job $jobB: alloc ${ISO}\$" dup.err
'
test_expect_success 'scheduler and job-manager views still agree' '
	grep "job-manager and scheduler agree" dup.err &&
	test_must_fail grep "scheduler mismatch" dup.err
'
test_expect_success 'debug bundle was collected' '
	test -f dup.tar.gz &&
	tar tzf dup.tar.gz >dup.list &&
	grep dup/summary.json dup.list &&
	grep dup/sched.resource-status.json dup.list &&
	grep "dup/jobs/$jobA/R.txt" dup.list &&
	grep "dup/jobs/$jobB/eventlog.txt" dup.list &&
	jq -e ".conflicts | length == 1" dup/summary.json &&
	jq -e ".sched_mismatch == null" dup/summary.json
'
test_expect_success 'debug bundle has dmesg and eventlogs with ISO timestamps' '
	head -1 dup/dmesg.txt | grep "flux dmesg$" &&
	sed -n 2p dup/dmesg.txt | grep "^$ISO " &&
	sed -n 2p dup/jobs/$jobA/eventlog.txt | grep "^$ISO submit" &&
	sed -n 2p dup/jobs/$jobA/exec-eventlog.txt | grep "^$ISO init"
'
# With -d, the testsuite sets FLUX_PYCLI_LOGLEVEL=10 (debug), so reset it
# to INFO here so that only -v enables debug output
test_expect_success 'no debug messages without -v' '
	test_expect_code 1 env FLUX_PYCLI_LOGLEVEL=20 \
		flux debug alloc-check -n 2>quiet.err &&
	test_must_fail grep DEBUG quiet.err
'
test_expect_success 'flux debug alloc-check -v logs what it is doing' '
	test_expect_code 1 env FLUX_PYCLI_LOGLEVEL=20 \
		flux debug alloc-check -v -o verbose 2>verbose.err &&
	test_debug "cat verbose.err" &&
	grep "DEBUG: sched.resource-status: 1 ranks allocated" verbose.err &&
	grep "DEBUG: job-list reports 2 running jobs" verbose.err &&
	grep "DEBUG: checking 1 pairs" verbose.err &&
	grep "DEBUG: running flux dmesg" verbose.err &&
	grep "DEBUG: writing summary.json" verbose.err
'
test_expect_success 'flux debug alloc-check --no-collect skips debug bundle' '
	test_expect_code 1 flux debug alloc-check --no-collect -o nocollect \
		2>nocollect.err &&
	grep "conflict:" nocollect.err &&
	test_must_fail grep "debug bundle" nocollect.err &&
	test_must_fail test -e nocollect &&
	test_must_fail test -e nocollect.tar.gz
'
test_expect_success 'all output goes to stderr with logger prefix' '
	test_expect_code 1 flux debug alloc-check -n >stdout.out 2>prefix.err &&
	test_must_be_empty stdout.out &&
	test_must_fail grep -v "^flux-debug: " prefix.err
'
test_expect_success 'no color or symbols by default when not a tty' '
	test_must_fail grep "$(printf "\\033")" prefix.err &&
	test_must_fail grep "$(printf "\\342\\234\\230")" prefix.err
'
test_expect_success '--color=always adds color and symbols' '
	test_expect_code 1 flux debug alloc-check -n --color=always 2>color.err &&
	grep "$(printf "\\033\\[1;31m")" color.err &&
	grep "$(printf "\\342\\234\\230") conflict:" color.err
'
test_expect_success '--color=always emits symbols if locale is not UTF-8' '
	test_expect_code 1 env PYTHONIOENCODING=ascii \
		flux debug alloc-check -n --color=always 2>ascii.err &&
	test_debug "cat ascii.err" &&
	grep "$(printf "\\342\\234\\230") conflict:" ascii.err &&
	test_must_fail grep "\\\\u" ascii.err
'
test_expect_success 'NO_COLOR disables color' '
	test_expect_code 1 env NO_COLOR=1 flux debug alloc-check -n 2>nocolor.err &&
	test_must_fail grep "$(printf "\\033")" nocolor.err
'
# Freeing the duplicate job causes sched-dupalloc to free the original
# job's resources from its pool while the job-manager still has them
# allocated to jobA.
test_expect_success 'cancel duplicate job' '
	flux cancel $jobB &&
	flux job wait-event $jobB clean
'
test_expect_success 'flux debug alloc-check detects scheduler mismatch' '
	test_expect_code 1 flux debug alloc-check -v -o mismatch 2>mismatch.err &&
	test_debug "cat mismatch.err" &&
	grep "no overlapping allocations" mismatch.err &&
	grep "ERROR: scheduler mismatch" mismatch.err &&
	grep "ERROR:    allocated in job-manager only: rank" mismatch.err &&
	grep "ERROR:      held by job $jobA: rank0/core" mismatch.err &&
	grep "ERROR:    allocated in scheduler only: none" mismatch.err &&
	grep "DEBUG: sched.resource-status (resample 5):" mismatch.err &&
	test_must_fail test -e mismatch.tar.gz
'
test_expect_success 'cancel original job, flux debug alloc-check succeeds' '
	flux cancel $jobA &&
	flux job wait-event $jobA clean &&
	flux debug alloc-check -o clean 2>clean.err &&
	grep "job-manager and scheduler agree" clean.err
'
# A free delayed in the scheduler looks like a free in flight: the
# job-manager has released the resources but the scheduler has not yet.
test_expect_success 'difference from a free in flight is not reported' '
	jobS=$(flux submit --setattr=system.slow-free=4 -n1 true) &&
	flux job wait-event $jobS clean &&
	flux debug alloc-check -n -v 2>slowfree.err &&
	test_debug "cat slowfree.err" &&
	grep "DEBUG: sched.resource-status: 1 ranks allocated" slowfree.err &&
	grep "DEBUG: sched.resource-status (resample 1):" slowfree.err &&
	grep "job-manager and scheduler agree" slowfree.err
'
test_expect_success FLUX_SECURITY 'flux debug alloc-check checks jobs of all users' '
	echo exec.testexec.allow-guests=true | flux config load &&
	jobG=$(submit_as_alternate_user -n1 \
		--setattr=system.exec.test.run_duration=5m true) &&
	flux job wait-event $jobG start &&
	flux debug alloc-check -n 2>guest-job.err &&
	test_debug "cat guest-job.err" &&
	grep "checked 1 allocations" guest-job.err &&
	flux cancel $jobG &&
	flux job wait-event $jobG clean
'
test_expect_success 'overlapping alloc-bypass job is not a duplicate' '
	flux jobtap load alloc-bypass.so &&
	jobC=$(flux submit --wait-event=start -n1 sleep inf) &&
	jobD=$(flux submit --wait-event=start \
		--setattr=system.alloc-bypass.R="$(flux job info $jobC R)" \
		-o per-resource.type=core sleep inf) &&
	flux debug alloc-check -v -o bypass 2>bypass.err &&
	grep "skipped 1" bypass.err &&
	grep "DEBUG: skipping .*: alloc-bypass" bypass.err &&
	flux cancel $jobC $jobD &&
	flux job wait-event $jobC clean &&
	flux job wait-event $jobD clean &&
	flux jobtap remove alloc-bypass.so
'
# Housekeeping blocks until hkhold is removed.
test_expect_success 'configure blocking housekeeping' '
	cat >housekeeping.sh <<-EOT &&
	#!/bin/sh
	while test -e $(pwd)/hkhold; do sleep 0.1; done
	EOT
	chmod +x housekeeping.sh &&
	flux config load <<-EOT
	[job-manager.housekeeping]
	command = [ "$(pwd)/housekeeping.sh" ]
	EOT
'
test_expect_success 'run a job that leaves resources in housekeeping' '
	touch hkhold &&
	jobE=$(f58 $(flux submit -n1 true)) &&
	flux job wait-event $jobE clean &&
	wait_for_hk 1
'
test_expect_success 'start job duplicating the housekeeping allocation' '
	jobF=$(f58 $(flux submit --wait-event=start -n1 \
		--setattr=system.dup-alloc=1 sleep inf))
'
test_expect_success 'flux debug alloc-check detects job/housekeeping conflict' '
	test_expect_code 1 flux debug alloc-check -n 2>hk.err &&
	test_debug "cat hk.err" &&
	grep "duplicate housekeeping allocation" hk.err &&
	grep "conflict:" hk.err >hkconflict.out &&
	grep "housekeeping $jobE" hkconflict.out &&
	grep "job $jobF" hkconflict.out &&
	grep "ERROR:    housekeeping $jobE: alloc .*free .*housekeeping since .* on ranks 0" \
		hk.err
'
test_expect_success 'release housekeeping, flux debug alloc-check detects mismatch' '
	rm hkhold &&
	wait_for_hk 0 &&
	test_expect_code 1 flux debug alloc-check -n 2>hk2.err &&
	test_debug "cat hk2.err" &&
	grep "no overlapping allocations" hk2.err &&
	grep "scheduler mismatch" hk2.err &&
	grep "held by job $jobF: rank0/core" hk2.err
'
test_expect_success 'cancel duplicate job, flux debug alloc-check succeeds' '
	flux cancel $jobF &&
	flux job wait-event $jobF clean &&
	wait_for_hk 0 &&
	flux debug alloc-check -n
'
test_expect_success 'unload sched-dupalloc, reload sched-simple' '
	flux module remove sched-dupalloc &&
	flux module load sched-simple
'
test_done
