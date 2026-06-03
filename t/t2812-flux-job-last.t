#!/bin/sh
#
# ci=asan

test_description='Test the flux job last command'

. $(dirname $0)/sharness.sh

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

test_under_flux 1

test_expect_success 'flux-job last fails on invalid arguments' '
	test_must_fail flux job last --badarg 2>badarg.err &&
	grep "unrecognized option" badarg.err
'
test_expect_success 'flux-job last fails when no jobs have been submitted' '
	test_must_fail flux job last 2>nojob.err &&
	grep "job history is empty" nojob.err

'
test_expect_success 'submit some jobs' '
	flux submit --cc=0-9 true >jobids
'
test_expect_success 'flux job last lists the most recently submitted job' '
	id=$(flux job last) &&
	test "$id" = "$(tail -1 jobids)"
'
test_expect_success 'flux job last [::-1] lists jobs in submit order' '
	flux job last "[::-1]" >last-reverse-all.out &&
	test_cmp jobids last-reverse-all.out
'
test_expect_success 'flux job last [:] lists all the jobs' '
	flux job last "[:]" >last.out &&
	test $(wc -l <last.out) -eq $(wc -l <jobids)
'
test_expect_success 'flux job last N lists the last N jobs' '
	head -4 last.out >last4.exp &&
	flux job last 4 >last4.out &&
	test_cmp last4.exp last4.out
'
# issue #4930
test_expect_success 'flux-job last lists inactive jobs after instance restart' '
	flux job last "[:]" >lastdump.exp &&
	flux queue idle &&
	flux dump dump.tgz &&
	flux start -Scontent.restore=dump.tgz \
		flux job last "[:]" >lastdump.out &&
	test_cmp lastdump.exp lastdump.out
'
# Rewrite the submit event timestamp (== t_submit) of a job's eventlog in
# the KVS so that multiple jobs can be forced to share an identical t_submit.
force_t_submit() {
	local id=$1
	local ts=$2
	local kd=$(flux job id --to=kvs $id)
	flux kvs get -r ${kd}.eventlog | flux python -c "
import sys, json
lines = sys.stdin.read().splitlines()
e = json.loads(lines[0])
e['timestamp'] = $ts
lines[0] = json.dumps(e)
sys.stdout.write('\n'.join(lines) + '\n')
" | flux kvs put -r ${kd}.eventlog=-
}

# The history plugin keys jobs by id, not t_submit, so jobs sharing a
# t_submit must all survive a restart (replay).  Before this was fixed,
# the t_submit-based duplicate check dropped all but one such job.
test_expect_success 'flux-job last keeps jobs sharing a t_submit after restart' '
	ids=$(flux submit --cc=1-3 --wait true | sort) &&
	flux queue idle &&
	for id in $ids; do force_t_submit $id 1000000000.0; done &&
	flux module reload job-manager &&
	flux job last "[:]" >shared_tsubmit.out &&
	for id in $ids; do
		grep -q $id shared_tsubmit.out || return 1
	done
'

# issue #4931
test_expect_success 'flux-job last does not list purged jobs' '
	flux job purge --force --num-limit=0 &&
	test_must_fail flux job last 2>nojob2.err &&
	grep "job history is empty" nojob2.err
'

submit_as_root()
{
        FAKE_USERID=0
        test_debug "echo running flux run $@ as userid $FAKE_USERID"
        flux run --dry-run "$@" | \
          flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
            >job.signed
        FLUX_HANDLE_USERID=$FAKE_USERID \
          flux job submit --flags=signed job.signed
}

# issue #5475
# Execution may fail but submission should work - enough for this test
test_expect_success FLUX_SECURITY 'reload job-ingest with allow-root-jobs' '
	flux module reload job-ingest allow-root-jobs
'
test_expect_success FLUX_SECURITY 'run a job as fake root' '
	submit_as_root true &&
	FLUX_HANDLE_USERID=0 flux job last
'

test_done
