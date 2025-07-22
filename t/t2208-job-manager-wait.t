#!/bin/sh

test_description='Test flux job manager wait handling'

. $(dirname $0)/sharness.sh

test_under_flux 1

list_jobs=${FLUX_BUILD_DIR}/t/job-manager/list-jobs
SUBMIT_WAIT="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-wait.py"
SUBMIT_WAITANY="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-waitany.py"
SUBMIT_SW="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-sliding-window.py"
SUBMIT_INTER="flux python ${FLUX_SOURCE_DIR}/t/job-manager/wait-interrupted.py"
JOB_CONV="flux python ${FLUX_SOURCE_DIR}/t/job-manager/job-conv.py"
PRINT_CONSTANTS="${FLUX_BUILD_DIR}/t/job-manager/print-constants"

flux setattr log-stderr-level 1

test_job_count() {
    test $(${list_jobs} | wc -l) -eq $1
}

print_jobid_any_py() {
	flux python -c 'import flux.constants; print(f"{flux.constants.FLUX_JOBID_ANY:x}")'
}

test_expect_success "python FLUX_JOBID_ANY matches job.h" '
	py_id=$(print_jobid_any_py) &&
	c_id=$($PRINT_CONSTANTS FLUX_JOBID_ANY) &&
	test $py_id = $c_id
'

test_expect_success "wait works on waitable job run with flux submit" '
	JOBID=$(flux submit --flags waitable true) &&
	flux job wait ${JOBID}
'

test_expect_success "wait works on waitable job run with flux-job" '
	flux submit --dry-run true >true.jobspec &&
	JOBID=$(flux job submit --flags=waitable true.jobspec) &&
	flux job wait ${JOBID}
'

test_expect_success "wait works on inactive,waitable job" '
	JOBID=$(flux submit --flags waitable true) &&
	flux job wait-event ${JOBID} clean &&
	flux job wait ${JOBID}
'

test_expect_success "waitable inactive jobs are listed as zombies" '
	JOBID=$(flux submit --flags waitable true) &&
	echo ${JOBID} >id1.out &&
	flux job wait-event ${JOBID} clean &&
	${list_jobs} >list1.out &&
	test $(wc -l <list1.out) -eq 1 &&
	test "$(jq .state <list1.out | ${JOB_CONV} statetostr)" = "INACTIVE"
'

test_expect_success "zombies go away after they are waited for" '
	flux job wait $(cat id1.out) &&
	${list_jobs} >list2.out &&
	test $(wc -l <list2.out) -eq 0
'

test_expect_success "wait works on three waitable jobs in reverse order" '
	JOB1=$(flux submit --flags waitable true) &&
	JOB2=$(flux submit --flags waitable true) &&
	JOB3=$(flux submit --flags waitable true) &&
	flux job wait ${JOB3} &&
	flux job wait ${JOB2} &&
	flux job wait ${JOB1}
'

test_expect_success "wait FLUX_JOBID_ANY works on three waitable jobs" '
	flux submit --flags waitable true >jobs.out &&
	flux submit --flags waitable true >>jobs.out &&
	flux submit --flags waitable true >>jobs.out &&
	flux job wait >wait.out &&
	flux job wait >>wait.out &&
	flux job wait >>wait.out &&
	sort -n jobs.out >jobs_s.out &&
	sort -n wait.out >wait_s.out &&
	test_cmp jobs_s.out wait_s.out
'

test_expect_success 'python submit-wait example works' '
        ${SUBMIT_WAIT} >submit_wait.out &&
        test $(grep Success submit_wait.out | wc -l) -eq 5
'

test_expect_success 'python submit-waitany example works' '
        ${SUBMIT_WAITANY} >submit_waitany.out &&
        test $(grep Success submit_waitany.out | wc -l) -eq 5
'

test_expect_success 'python submit-slidiing-window example works' '
        ${SUBMIT_SW} >submit_sw.out &&
        test $(grep Success submit_sw.out | wc -l) -eq 10
'

test_expect_success 'disconnect with async waits pending' '
        ${SUBMIT_INTER} &&
	count=0 &&
	echo cleaning up zombies... &&
	while flux job wait; do count=$(($count+1)); done &&
	echo ...reaped $count of 10 zombies
'

test_expect_success "wait FLUX_JOBID_ANY exits 2 with no waitable jobs" '
	test_expect_code 2 flux job wait
'

test_expect_success "wait works when job terminated by exception" '
	JOBID=$(flux submit --flags waitable sleep 120) &&
	flux job raise --severity=0 ${JOBID} my-exception-message &&
	test_expect_code 1 flux job wait ${JOBID} 2>exception.out &&
	grep my-exception-message exception.out
'

test_expect_success "wait works when job tasks exit 1" '
	JOBID=$(flux submit --flags waitable false) &&
	test_expect_code 1 flux job wait ${JOBID} 2>false.out &&
	grep exit false.out
'

test_expect_success "wait --all fails with jobid" '
	test_must_fail flux job wait --all 42
'

test_expect_success "wait --all works with no waitable jobs" '
	test_job_count 0 &&
	flux job wait --all
'

test_expect_success "wait --all works with one job" '
	flux submit --flags waitable true &&
	test_job_count 1 &&
	flux job wait --all &&
	test_job_count 0
'

test_expect_success "wait --all works with two jobs" '
	flux submit --flags waitable true &&
	flux submit --flags waitable true &&
	test_job_count 2 &&
	flux job wait --all &&
	test_job_count 0
'

test_expect_success "wait --all fails when first job fails" '
	flux submit --flags waitable false &&
	flux submit --flags waitable true &&
	test_job_count 2 &&
	test_must_fail flux job wait --all &&
	test_job_count 0
'

test_expect_success "wait --all fails when second job fails" '
	flux submit --flags waitable true &&
	flux submit --flags waitable false &&
	test_job_count 2 &&
	test_must_fail flux job wait --all &&
	test_job_count 0
'


test_expect_success "wait --all --verbose emits one line per successful job" '
	flux submit --flags waitable true &&
	flux submit --flags waitable true &&
	flux submit --flags waitable false &&
	test_must_fail flux job wait --all --verbose 2>verbose.err &&
	test $(wc -l <verbose.err) -eq 3
'

test_expect_success "wait fails on bad jobid, " '
	test_expect_code 2 flux job wait 1
'

test_expect_success "wait fails on non-waitable, active job" '
	JOBID=$(flux submit sleep 0.5) &&
	test_expect_code 2 flux job wait ${JOBID}
'

test_expect_success "wait fails on non-waitable, inactive job" '
	JOBID=$(flux submit true) &&
	flux job wait-event ${JOBID} clean &&
	test_expect_code 2 flux job wait ${JOBID}
'

test_expect_success "a second wait fails on waitable, active job" '
	JOBID=$(flux submit --flags waitable sleep 0.5) &&
	flux job wait ${JOBID} &&
	test_expect_code 2 flux job wait ${JOBID}
'

test_expect_success "a second wait fails on waitable, inactive job" '
	JOBID=$(flux submit --flags waitable true) &&
	flux job wait-event ${JOBID} clean &&
	flux job wait ${JOBID} &&
	test_expect_code 2 flux job wait ${JOBID}
'

test_expect_success "guest cannot submit job with WAITABLE flag" '
	export FLUX_HANDLE_ROLEMASK=0x2 &&
	test_must_fail flux submit --flags waitable true &&
	unset FLUX_HANDLE_ROLEMASK
'

test_expect_success "guest cannot wait on a job" '
	JOBID=$(flux submit --flags waitable true) &&
	export FLUX_HANDLE_ROLEMASK=0x2 &&
	test_expect_code 1 flux job wait ${JOBID} &&
	unset FLUX_HANDLE_ROLEMASK
'

test_done
