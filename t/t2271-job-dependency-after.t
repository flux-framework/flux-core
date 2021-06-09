#!/bin/sh

test_description='Test job dependency after* support'

. $(dirname $0)/sharness.sh

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

test_under_flux 1 job

flux setattr log-stderr-level 1

submit_as_alternate_user()
{
	FAKE_USERID=42
	test_debug "echo running flux mini run $@ as userid $FAKE_USERID"
	flux mini run --dry-run "$@" | \
		flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
		>job.signed
	FLUX_HANDLE_USERID=$FAKE_USERID \
		flux job submit --flags=signed job.signed
}

test_expect_success 'dependency=after:invalid rejects invalid target jobids' '
	test_expect_code 1 flux mini bulksubmit \
		--dependency={} \
		--log-stderr={}.err \
		hostname ::: \
		after:-1 \
		after:bar \
		after:1234 \
		after:${jobid} &&
	grep "\"-1\" is not a valid jobid" after:-1.err &&
	grep "\"bar\" is not a valid jobid" after:bar.err &&
	grep "job not found" after:1234.err
'
test_expect_success FLUX_SECURITY 'dependency=after will not work on another user job' '
	jobid=$(flux mini submit sleep 300) &&
	test_debug "echo submitted job $jobid" &&
	test_expect_code 1 submit_as_alternate_user \
		--dependency=afterany:$jobid hostname \
		>baduser.log 2>&1 &&
	test_debug "cat baduser.log" &&
	grep "Permission denied" baduser.log &&
	flux job cancel $jobid
'
test_expect_success HAVE_JQ 'dependency=after rejects invalid dependency' '
	flux mini run --dry-run hostname | \
	  jq ".attributes.system.dependencies[0] = \
		{\"scheme\":\"after\", \"value\": 1}" >job.json &&
	test_expect_code 1 flux job submit job.json
'
test_expect_success 'dependency=after works' '
	jobid=$(flux mini submit --urgency=hold hostname) &&
	depid=$(flux mini submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid dependency-add &&
	test_debug "echo checking that job ${depid} is in DEPEND state" &&
	test "$(flux jobs -no {state} $depid)" = "DEPEND" &&
	flux job urgency $jobid default &&
	flux job wait-event -vt 15 $depid clean 
'
test_expect_success 'dependency=after works when antecedent is running' '
	jobid=$(flux mini submit sleep 300) &&
	flux job wait-event -vt 15 $jobid start &&
	depip=$(flux mini submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid clean &&
	flux job cancel $jobid
'
test_expect_success 'dependency=after generates exception for failed job' '
	jobid=$(flux mini submit --urgency=hold hostname) &&
	depid=$(flux mini submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid dependency-add &&
	test_debug "echo checking that job ${depid} is in DEPEND state" &&
	test "$(flux jobs -no {state} $depid)" = "DEPEND" &&
	flux job cancel $jobid &&
	flux job wait-event -m type=dependency -vt 15 $depid exception 
'
test_expect_success 'dependency=afterany works' '
	flux mini bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afterany {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afterany) &&
	job2=$(sed "2q;d" jobids.afterany) &&
	job3=$(sed "3q;d" jobids.afterany) &&
	jobid=$(flux mini submit \
		--dependency=afterany:$job1 \
		--dependency=afterany:$job2 \
		--dependency=afterany:$job3 \
		hostname) &&
	for id in $(cat jobids.afterany);
		do flux job urgency $id default;
	done &&
	flux jobs &&
	flux job wait-event -vt 15 \
		-m description=after-finish=$job2 \
		$jobid dependency-remove &&
	flux jobs &&
	flux job cancel $job3 &&
	flux job wait-event -vt 15 $jobid clean
'
test_expect_success 'dependency=afterok works' '
	flux mini bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afterok {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afterok) &&
	job2=$(sed "2q;d" jobids.afterok) &&
	job3=$(sed "3q;d" jobids.afterok) &&
	ok1=$(flux mini submit \
		--dependency=afterok:$job1 \
		hostname) &&
	ok2=$(flux mini submit \
		--dependency=afterok:$job2 \
		hostname) &&
	ok3=$(flux mini submit \
		--dependency=afterok:$job3 \
		hostname) &&
	for id in $(cat jobids.afterok);
		do flux job urgency $id default;
	done &&
	flux job wait-event -vt 15 $job3 start &&
	flux job cancel $job3 &&
	flux job wait-event -vt 15 \
		-m description=after-success=$job1 \
		$ok1 dependency-remove &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok2 exception &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok2 exception
'
test_expect_success 'dependency=afternotok works' '
	flux mini bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afternotok {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afternotok) &&
	job2=$(sed "2q;d" jobids.afternotok) &&
	job3=$(sed "3q;d" jobids.afternotok) &&
	ok1=$(flux mini submit \
		--dependency=afternotok:$job1 \
		hostname) &&
	ok2=$(flux mini submit \
		--dependency=afternotok:$job2 \
		hostname) &&
	ok3=$(flux mini submit \
		--dependency=afternotok:$job3 \
		hostname) &&
	for id in $(cat jobids.afternotok);
		do flux job urgency $id default;
	done &&
	flux job wait-event -vt 15 $job3 start &&
	flux job cancel $job3 &&
	flux job wait-event -vt 15 \
		-m description=after-failure=$job2 \
		$ok2 dependency-remove &&
	flux job wait-event -vt 15 \
		-m description=after-failure=$job3 \
		$ok3 dependency-remove &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok1 exception
'
test_expect_success 'jobs with dependencies can be safely canceled' '
	jobid=$(flux mini submit --urgency=hold hostname) &&
	depid=$(flux mini submit --dependency=after:$jobid hostname) &&
	flux job cancel $depid &&
	flux job urgency $jobid default &&
	flux job wait-event -vt 15 $jobid clean
'
test_done
