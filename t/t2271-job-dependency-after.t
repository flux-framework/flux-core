#!/bin/sh

test_description='Test job dependency after* support'

. $(dirname $0)/sharness.sh

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

test_under_flux 1 job

flux setattr log-stderr-level 1

submit_as_alternate_user()
{
	FAKE_USERID=42
	test_debug "echo running flux run $@ as userid $FAKE_USERID"
	flux run --dry-run "$@" | \
		flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
		>job.signed
	FLUX_HANDLE_USERID=$FAKE_USERID \
		flux job submit --flags=signed job.signed
}

test_expect_success 'dependency=after:invalid rejects invalid target jobids' '
	test_expect_code 1 flux bulksubmit \
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
	jobid=$(flux submit sleep 300) &&
	test_debug "echo submitted job $jobid" &&
	test_expect_code 1 submit_as_alternate_user \
		--dependency=afterany:$jobid hostname \
		>baduser.log 2>&1 &&
	test_debug "cat baduser.log" &&
	grep "Permission denied" baduser.log &&
	flux cancel $jobid
'
test_expect_success 'disable ingest validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'dependency=after rejects invalid dependency' '
	flux run --dry-run hostname | \
	  jq ".attributes.system.dependencies[0] = \
		{\"scheme\":\"after\", \"value\": 1}" >job.json &&
	test_expect_code 1 flux job submit job.json
'
test_expect_success 'reenable ingest validator' '
	flux module reload -f job-ingest
'
test_expect_success 'dependency=after works' '
	jobid=$(flux submit --urgency=hold hostname) &&
	depid=$(flux submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid dependency-add &&
	test_debug "echo checking that job ${depid} is in DEPEND state" &&
	test "$(flux jobs -no {state} $depid)" = "DEPEND" &&
	flux job urgency $jobid default &&
	flux job wait-event -vt 15 $depid clean
'
test_expect_success 'dependency=after does not release job until start event' '
	jobid=$(flux submit \
		--setattr=system.exec.test.override=1 \
		--setattr=system.exec.test.run_duration=0.001s true) &&
	depid=$(flux submit --dependency=after:$jobid hostname) &&
	flux job wait-event -t 15 $depid dependency-add &&
	flux job wait-event -t 15 $jobid alloc &&
	test_debug "echo antecedent in RUN state, but no start event" &&
	test_must_fail flux job wait-event -t 0.25 $depid dependency-remove &&
	test_debug "echo dependency not yet removed" &&
	flux job-exec-override start $jobid &&
	flux job wait-event -t 15 $jobid start &&
	test_debug "echo antecedent now started" &&
	flux job wait-event -t 15 $depid dependency-remove &&
	flux job wait-event -t 15 $depid clean &&
	flux job wait-event -t 15 $jobid clean
'
test_expect_success 'dependency=after works when antecedent is running' '
	jobid=$(flux submit sleep 300) &&
	flux job wait-event -vt 15 $jobid start &&
	depip=$(flux submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid clean &&
	flux cancel $jobid
'
test_expect_success 'dependency=after generates exception for failed job' '
	jobid=$(flux submit --urgency=hold hostname) &&
	depid=$(flux submit --dependency=after:$jobid hostname) &&
	flux job wait-event -vt 15 $depid dependency-add &&
	test_debug "echo checking that job ${depid} is in DEPEND state" &&
	test "$(flux jobs -no {state} $depid)" = "DEPEND" &&
	flux cancel $jobid &&
	flux job wait-event -m type=dependency -vt 15 $depid exception
'
test_expect_success 'dependency=afterany works' '
	flux bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afterany {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afterany) &&
	job2=$(sed "2q;d" jobids.afterany) &&
	job3=$(sed "3q;d" jobids.afterany) &&
	jobid=$(flux submit \
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
	flux cancel $job3 &&
	flux job wait-event -vt 15 $jobid clean
'
test_expect_success 'dependency=afterok works' '
	flux bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afterok {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afterok) &&
	job2=$(sed "2q;d" jobids.afterok) &&
	job3=$(sed "3q;d" jobids.afterok) &&
	ok1=$(flux submit \
		--dependency=afterok:$job1 \
		hostname) &&
	ok2=$(flux submit \
		--dependency=afterok:$job2 \
		hostname) &&
	ok3=$(flux submit \
		--dependency=afterok:$job3 \
		hostname) &&
	for id in $(cat jobids.afterok);
		do flux job urgency $id default;
	done &&
	flux job wait-event -vt 15 $job3 start &&
	flux cancel $job3 &&
	flux job wait-event -vt 15 \
		-m description=after-success=$job1 \
		$ok1 dependency-remove &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok2 exception &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok2 exception
'
test_expect_success 'dependency=afternotok works' '
	flux bulksubmit \
		--urgency=hold \
		--job-name={} \
		--log=jobids.afternotok {} 300 \
		::: true false sleep &&
	job1=$(sed "1q;d" jobids.afternotok) &&
	job2=$(sed "2q;d" jobids.afternotok) &&
	job3=$(sed "3q;d" jobids.afternotok) &&
	ok1=$(flux submit \
		--dependency=afternotok:$job1 \
		hostname) &&
	ok2=$(flux submit \
		--dependency=afternotok:$job2 \
		hostname) &&
	ok3=$(flux submit \
		--dependency=afternotok:$job3 \
		hostname) &&
	for id in $(cat jobids.afternotok);
		do flux job urgency $id default;
	done &&
	flux job wait-event -vt 15 $job3 start &&
	flux cancel $job3 &&
	flux job wait-event -vt 15 \
		-m description=after-failure=$job2 \
		$ok2 dependency-remove &&
	flux job wait-event -vt 15 \
		-m description=after-failure=$job3 \
		$ok3 dependency-remove &&
	flux job wait-event -vt 15 \
		-m type=dependency $ok1 exception
'
test_expect_success 'dependency=after works for INACTIVE jobs' '
	run_timeout 15 \
		flux bulksubmit --wait --watch \
		--job-name=after:{} \
		--dependency=after:{} \
		echo after:{} ::: ${job1} ${job2} ${job3}
'
test_expect_success 'dependency=afterany works for INACTIVE jobs' '
	run_timeout 15 \
		flux bulksubmit --wait --watch \
		--job-name=afterany:{} \
		--dependency=afterany:{} \
		echo afterany:{} ::: ${job1} ${job2} ${job3}
'
test_expect_success 'dependency=afterok works for INACTIVE job' '
	run_timeout 15 \
		flux run --dependency=afterok:${job1} \
		echo afterok:${job1} &&
	test_must_fail flux run --dependency=afterok:${job2} hostname &&
	test_must_fail flux run --dependency=afterok:${job3} hostname
'
test_expect_success 'dependency=afternotok works for INACTIVE job' '
	run_timeout 15 \
		flux bulksubmit --wait --watch \
		--job-name=afternotok:{} \
		--dependency=afternotok:{} \
		echo afterany:{} ::: ${job2} ${job3} &&
	test_must_fail flux run --dependency=afternotok:${job1} hostname
'
test_expect_success 'dependency=after fails for INACTIVE canceled job' '
	job4=$(flux submit --urgency=hold hostname) &&
	flux cancel ${job4} &&
	test_must_fail flux run --dependency=after:${job4} hostname
'
test_expect_success 'jobs with dependencies can be safely canceled' '
	jobid=$(flux submit --urgency=hold hostname) &&
	depid=$(flux submit --dependency=after:$jobid hostname) &&
	flux cancel $depid &&
	flux job urgency $jobid default &&
	flux job wait-event -vt 15 $jobid clean
'
test_expect_success 'flux jobtap query dependency-after works' '
	flux jobtap query .dependency-after > query-none.json &&
	test_debug "jq -S . query-none.json" &&
	jq -e ".dependencies | length == 0" query-none.json &&
	jobid=$(flux submit --urgency=hold hostname) &&
	depid=$(flux submit --dependency=after:$jobid hostname) &&
	flux jobtap query .dependency-after > query.json &&
	test_debug "jq -S . query.json" &&
	jq -e ".dependencies | length == 1" query.json &&
	flux job urgency $jobid default &&
	flux job wait-event -vt 15 $depid clean
'
test_done
