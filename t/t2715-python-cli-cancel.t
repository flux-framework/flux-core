#!/bin/sh

test_description='Test flux cancel command'

. $(dirname $0)/sharness.sh

test_under_flux 4 full -Slog-stderr-level=1

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

runas() {
	userid=$1 && shift
	FLUX_HANDLE_USERID=$userid FLUX_HANDLE_ROLEMASK=0x2 "$@"
}

test_expect_success 'flux cancel fails with bad FLUX_URI' '
	validjob=$(flux submit sleep 30) &&
	(FLUX_URI=/wrong test_must_fail flux cancel ${validjob}) &&
	flux cancel $validjob
'
test_expect_success 'flux cancel fails with unknown job id' '
	test_must_fail flux cancel 0
'
test_expect_success 'flux cancel fails with unknown job ids' '
	test_must_fail flux cancel 0 f123
'
test_expect_success 'flux cancel fails with no args' '
	test_must_fail flux cancel
'
test_expect_success 'flux cancel fails with invalid jobid' '
	test_must_fail flux cancel foo
'
test_expect_success 'flux cancel fails with invalid user' '
	test_must_fail flux cancel --user=1badusername12345
'
test_expect_success 'flux cancel fails with invalid option' '
	test_must_fail flux cancel --meep foo
'
test_expect_success 'flux cancel JOBID works' '
	id=$(flux submit sleep 100) &&
	flux cancel ${id} &&
	flux job wait-event -t 30 ${id} exception >cancel1.out &&
	grep "cancel" cancel1.out &&
	grep "severity\=0" cancel1.out
'
test_expect_success 'flux cancel --message works' '
	id=$(flux submit sleep 100) &&
	flux cancel --message=meepmessage ${id} &&
	flux job wait-event -t 30 ${id} exception >cancel2.out &&
	grep "meepmessage" cancel2.out
'
test_expect_success 'flux cancel --all --dry-run works' '
	count=4 &&
	flux submit --cc=1-$count sleep 60 &&
	flux cancel --all --dry-run 2>cancelall_n.err &&
	cat <<-EOT >cancelall_n.exp &&
	flux-cancel: Would cancel ${count} jobs
	EOT
	test_cmp cancelall_n.exp cancelall_n.err
'
test_expect_success 'flux cancel --all works' '
	count=$(flux job list | wc -l) &&
	flux cancel --all 2>cancelall.err &&
	cat <<-EOT >cancelall.exp &&
	flux-cancel: Canceled ${count} jobs (0 errors)
	EOT
	test_cmp cancelall.exp cancelall.err
'
test_expect_success 'flux cancel --all works with message' '
	count=4 &&
	flux submit --cc=1-$count sleep 60 &&
	flux cancel --all --message="cancel all" 2>cancelall.err &&
	cat <<-EOT >cancelall.exp &&
	flux-cancel: Canceled ${count} jobs (0 errors)
	EOT
	test_cmp cancelall.exp cancelall.err &&
	flux job wait-event -t 5 $(flux job last) exception >exception.out &&
	grep "cancel all" exception.out
'
test_expect_success 'the queue is empty' '
	run_timeout 180 flux queue drain
'
test_expect_success 'flux cancel --all --user all fails for guest' '
	id=$(($(id -u)+1)) &&
	test_must_fail runas ${id} \
	        flux cancel --all --user=all 2> cancelall_all_guest.err &&
	grep "guests can only raise exceptions on their own jobs" \
		cancelall_all_guest.err
'
test_expect_success 'flux- cancel --all --user <guest uid> works for guest' '
	id=$(($(id -u)+1)) &&
	runas ${id} flux cancel --all --user=${id}
'
test_expect_success 'flux cancel --state with unknown state fails' '
        test_must_fail flux cancel --states=FOO 2>cancelall_bs.err &&
	grep "Invalid state FOO specified" cancelall_bs.err
'
test_expect_success 'flux cancel --state=pending works' '
	runid=$(flux submit -n $(flux resource list -no {ncores}) sleep 60) &&
	flux submit --cc=1-4 sleep 30 &&
	flux cancel --states=pending --dry-run 2>pending.err &&
	grep "Would cancel 4 jobs" pending.err &&
	flux cancel --states=pending &&
	test $(flux jobs -f pending -no {id} | wc -l) -eq 0 &&
	test $(flux jobs -f run -no {id} | wc -l) -eq 1
'
test_expect_success 'flux cancel --state=run works' '
	flux cancel --states=run --dry-run 2>run.err &&
	grep "Would cancel 1 job" run.err &&
	flux cancel --states=run
'
test_expect_success 'flux cancel reports 0 jobs with no match' '
	flux cancel --states=run --dry-run 2>nomatch.err &&
	test_debug "cat nomatch.err" &&
	grep "Matched 0 jobs" nomatch.err
'
test_expect_success 'flux cancel can operate on multiple jobs' '
	ids=$(flux submit --cc=1-3 sleep 600) &&
        flux cancel -m "cancel multiple jobids" ${ids} &&
        for id in ${ids}; do
                flux job wait-event -t 30 ${id} exception >exception.out &&
                grep multiple exception.out
        done
'
test_expect_success 'flux cancel does nothing with --dry-run and multiple jobs' '
	flux cancel -n f1 f2 f3 2>multiple_n.out &&
	grep "Would cancel 3 jobs" multiple_n.out
'
test_expect_success 'flux cancel records errors with multiple jobids' '
	ids2=$(flux submit --cc=1-3 sleep 600) &&
        test_must_fail \
		flux cancel -m "cancel multiple jobids" ${ids2} ${ids} \
		2>multiple-errors.out &&
	grep "inactive" multiple-errors.out
'
test_done
