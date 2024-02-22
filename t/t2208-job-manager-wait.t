#!/bin/sh

test_description='Test flux job manager wait handling'

. $(dirname $0)/sharness.sh

test_under_flux 1

SUBMIT_WAIT="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-wait.py"
SUBMIT_WAITANY="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-waitany.py"
SUBMIT_SW="flux python ${FLUX_SOURCE_DIR}/t/job-manager/submit-sliding-window.py"
SUBMIT_INTER="flux python ${FLUX_SOURCE_DIR}/t/job-manager/wait-interrupted.py"
JOB_CONV="flux python ${FLUX_SOURCE_DIR}/t/job-manager/job-conv.py"
PRINT_CONSTANTS="${FLUX_BUILD_DIR}/t/job-manager/print-constants"

flux setattr log-stderr-level 1

print_jobid_any_py() {
	flux python -c 'import flux.constants; print(f"{flux.constants.FLUX_JOBID_ANY:x}")'
}

test_expect_success "python FLUX_JOBID_ANY matches job.h" '
	py_id=$(print_jobid_any_py) &&
	c_id=$($PRINT_CONSTANTS FLUX_JOBID_ANY) &&
	test $py_id = $c_id
'

test_expect_success "wait works on job run with flux submit" '
	JOBID=$(flux submit /bin/true) &&
	flux job wait ${JOBID}
'

test_expect_success "wait works on job run with flux-job" '
	flux submit --dry-run /bin/true >true.jobspec &&
	JOBID=$(flux job submit true.jobspec) &&
	flux job wait ${JOBID}
'

test_expect_success "wait works on inactive job" '
	JOBID=$(flux submit /bin/true) &&
	flux job wait-event ${JOBID} clean &&
	flux job wait ${JOBID}
'

test_expect_success "wait works on three jobs in reverse order" '
	JOB1=$(flux submit /bin/true) &&
	JOB2=$(flux submit /bin/true) &&
	JOB3=$(flux submit /bin/true) &&
	flux job wait ${JOB3} &&
	flux job wait ${JOB2} &&
	flux job wait ${JOB1}
'

test_expect_success "wait FLUX_JOBID_ANY works on three jobs" '
	flux submit /bin/true >jobs.out &&
	flux submit /bin/true >>jobs.out &&
	flux submit /bin/true >>jobs.out &&
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
	JOBID=$(flux submit sleep 120) &&
	flux job raise --severity=0 ${JOBID} my-exception-message &&
	test_expect_code 1 flux job wait ${JOBID} 2>exception.out &&
	grep my-exception-message exception.out
'

test_expect_success "wait works when job tasks exit 1" '
	JOBID=$(flux submit /bin/false) &&
	test_expect_code 1 flux job wait ${JOBID} 2>false.out &&
	grep exit false.out
'

test_expect_success "wait --all fails with jobid" '
	test_must_fail flux job wait --all 42
'

test_expect_success "wait --all works with no waitable jobs" '
	flux job wait --all
'

test_expect_success "wait --all works with one job" '
	flux submit /bin/true &&
	flux job wait --all
'

test_expect_success "wait --all works with two jobs" '
	flux submit /bin/true &&
	flux submit /bin/true &&
	flux job wait --all
'

test_expect_success "wait --all fails when first job fails" '
	flux submit /bin/false &&
	flux submit /bin/true &&
	test_must_fail flux job wait --all
'

test_expect_success "wait --all fails when second job fails" '
	flux submit /bin/true &&
	flux submit /bin/false &&
	test_must_fail flux job wait --all
'


test_expect_success "wait --all --verbose emits one line per successful job" '
	flux submit /bin/true &&
	flux submit /bin/true &&
	flux submit /bin/false &&
	test_must_fail flux job wait --all --verbose 2>verbose.err &&
	test $(wc -l <verbose.err) -eq 3
'

test_expect_success "wait fails on bad jobid, " '
	test_expect_code 2 flux job wait 1
'

test_expect_success "a second wait fails on active job" '
	JOBID=$(flux submit sleep 0.5) &&
	flux job wait ${JOBID} &&
	test_expect_code 2 flux job wait ${JOBID}
'

test_expect_success "a second wait fails on inactive job" '
	JOBID=$(flux submit /bin/true) &&
	flux job wait-event ${JOBID} clean &&
	flux job wait ${JOBID} &&
	test_expect_code 2 flux job wait ${JOBID}
'
test_expect_success "guest cannot wait on a job" '
	flux jobs -a
'

test_expect_success "a user cannot wait on another user's job" '
	JOBID=$(flux submit /bin/true) &&
	export FLUX_HANDLE_USERID=$(($(id -u)+1)) &&
	test_expect_code 1 flux job wait ${JOBID} &&
	unset FLUX_HANDLE_USERID
'
test_expect_success "wait works with deprecated waitable flag" '
	JOBID=$(flux submit --flags waitable /bin/true) &&
	flux job wait ${JOBID}
'

test_done
