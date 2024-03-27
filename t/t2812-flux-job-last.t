#!/bin/sh

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
	flux submit --cc=0-9 /bin/true >jobids
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
	flux start -o,-Scontent.restore=dump.tgz \
		flux job last "[:]" >lastdump.out &&
	test_cmp lastdump.exp lastdump.out
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
test_expect_success FLUX_SECURITY 'run a job as fake root' '
	submit_as_root true &&
	FLUX_HANDLE_USERID=0 flux job last
'

test_done
