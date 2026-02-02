#!/bin/sh

test_description='Test job dependency singleton support'

. $(dirname $0)/sharness.sh


flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

test_under_flux 2 job -Slog-stderr-level=1

submit_as_alternate_user()
{
	FAKE_USERID=42
	flux run --dry-run "$@" | \
		flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
		>job.signed
	FLUX_HANDLE_USERID=$FAKE_USERID \
		flux job submit --flags=signed job.signed
}

test_expect_success 'dependency=singleton rejects job with no job name' '
	test_expect_code 1 flux submit --dependency=singleton true 2>s1.err &&
	grep "require a job name" s1.err
'
test_expect_success 'jobs without a dependency run' '
	flux run true
'
test_expect_success 'submit 5 singleton jobs at once' '
	flux submit -n1 --watch --quiet --cc=1-5 --job-name=test \
		--dependency=singleton echo {cc} > 5jobs.out &&
	test_debug "cat 5jobs.out" &&
	test_seq 1 5 >5jobs.expected &&
	test_cmp 5jobs.expected 5jobs.out
'
test_expect_success 'submit a held job' '
	heldid=$(flux submit -n1 --urgency=hold --job-name=foo hostname)
'
test_expect_success 'singleton job is stuck in DEPEND state' '
	id=$(flux submit -n1 --job-name=foo --dependency=singleton true) &&
	flux job wait-event -vt 30 $id dependency-add &&
	test $(flux jobs -no {state} $id) = DEPEND
'
test_expect_success 'jobtap plugin query works' '
	flux jobtap query .dependency-singleton | jq &&
	flux jobtap query .dependency-singleton \
		| jq -e ".[\"$(id -u):foo\"].count == 2"
'
test_expect_success 'singleton job with a different name runs' '
	id2=$(flux submit -n1 --job-name=bar --dependency=singleton true) &&
	flux job wait-event -vt 30 $id2 clean
'
test_expect_success FLUX_SECURITY 'allow guest access to testexec' '
	echo exec.testexec.allow-guests=true | flux config load
'
test_expect_success FLUX_SECURITY 'singleton job as another user with same name runs' '
	id3=$(submit_as_alternate_user -n1 --job-name=foo \
	      --dependency=singleton -Sexec.test.run_duration=0.1s true) &&
	echo "submitted $id3" &&
	flux job wait-event -vt 30 $id3 clean
'
test_expect_success 'release held job, singleton should now run' '
	test $(flux jobs -no {state} $id) = DEPEND &&
	flux job urgency $heldid default &&
	flux job wait-event -vt30 $id clean
'
test_done
