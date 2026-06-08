#!/bin/sh

test_description='Sanity checks for job-exec multiuser exec'

. $(dirname $0)/sharness.sh

flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

if ! test_have_prereq FLUX_SECURITY; then
	skip_all='skipping multiuser exec tests, libflux-security or IMP not found'
	test_done
fi

IMP=${SHARNESS_TEST_SRCDIR}/job-exec/imp.sh
IMP_FAIL=${SHARNESS_TEST_SRCDIR}/job-exec/imp-fail.sh
DUMMY_SHELL=${SHARNESS_TEST_SRCDIR}/job-exec/dummy.sh

#  Configure dummy IMP
if ! test -d conf.d; then
	mkdir conf.d
	cat <<-EOF >conf.d/exec.toml
	[exec]
	imp = "${IMP}"
	EOF
fi

export FLUX_CONF_DIR=$(pwd)/conf.d
test_under_flux 4

test_expect_success 'job-exec: module configured to use IMP' '
	flux module stats -p bulk-exec.config.flux_imp_path job-exec | grep ${IMP}
'
test_expect_success 'job-exec: job as instance owner works' '
	test "$(id -u)" = "$(flux run id -u)"
'

SIGN_AS=${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py
#  Use flux_sign_wrap_as(3) to create a fake sign-wrapped jobspec
#  which will pass ingest signature check when submitted with
#  FLUX_HANDLE_USERID.
#
#  This will trigger the job-exec module to run job under our fake IMP.
#
test_expect_success 'job-exec: job as guest tries to run IMP' '
	FAKE_USERID=42 &&
	flux run --dry-run -n1 id -u | \
		flux python ${SIGN_AS} ${FAKE_USERID} > job.signed &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed job.signed) &&
	test_might_fail flux job attach ${id} &&
	flux job list-ids ${id} > ${id}.json &&
	jq -e ".userid == 42" < ${id}.json &&
	flux job attach ${id} 2>&1 | grep "test-imp: Running.*$(flux job id ${id})"
'
test_expect_success 'job-exec: large jobspec does not get truncated' '
	(FAKE_USERID=42 &&
		for i in `seq 0 2048`; \
			do export ENV${i}=xxxxxyyyyyyyyyzzzzzzz; \
		done &&
		flux run --dry-run -n1 id -u | \
			flux python ${SIGN_AS} ${FAKE_USERID} > job.signed &&
		id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed job.signed) &&
		test_might_fail flux job attach ${id} &&
		actual=imp-$(flux job id $id).input &&
		test_debug "echo expecting J of size $(wc -c < job.signed)B" &&
		test_debug "echo input to IMP was $(wc -c < $actual)B" &&
		jq -r .J ${actual} > J.input &&
		test_cmp job.signed J.input
	)
'
#  Configure dummy job shell so that we can ignore invalid signature on J
#   for this test, otherwise real shell would exit immediately.
#
test_expect_success 'job-exec: reconfig with failing dummy IMP' '
	cat <<-EOF >>conf.d/exec.toml
	job-shell = "${DUMMY_SHELL}"
	EOF
'
test_expect_success 'job-exec: reconfig and reload module' '
	flux config reload &&
	flux module reload -f job-exec
'
test_expect_success NO_ASAN 'job-exec: kill multiuser job works' '
	FAKE_USERID=42 &&
	flux run --dry-run -n2 -N2 sleep 1000 | \
		flux python ${SIGN_AS} ${FAKE_USERID} > sleep-job.signed &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed sleep-job.signed) &&
	flux job list-ids ${id} > ${id}.json &&
	jq -e ".userid == 42" < ${id}.json &&
	flux job wait-event -p exec -vt 30 ${id} shell.start &&
	flux cancel ${id} &&
	test_expect_code 143 run_timeout 30 flux job status -v ${id}
'
# Ensure barrier-timeout drains only affected rank on multiuser flux (#7663)
test_expect_success 'job-exec: barrier timeout raises job exception' '
	flux run --dry-run -N4 \
		--env=FAIL_MODE=hang-on-rank3 \
		--setattr=exec.bulkexec.barrier-timeout=0.5 \
		sleep 30 \
		| flux python ${SIGN_AS} 42 >signed.json &&
	jobid=$(FLUX_HANDLE_USERID=42 \
		flux job submit --flags=signed signed.json) &&
	flux job wait-event -vHt 60 $jobid exception
'
test_expect_success 'job-exec: only the affected rank is drained' '
	test_debug "flux resource drain" &&
	test "$(flux resource drain -no {ranks})" = "3" &&
	flux resource drain -no {reason} | grep "start timeout"
'
test_expect_success 'job-exec: undrain all ranks' '
	flux resource list &&
	flux resource undrain $(flux resource drain -no {ranks})
'
# Ensure exit-before-first barrier drains only affected rank
test_expect_success 'job-exec: barrier timeout raises job exception' '
	flux run --dry-run -N4 \
		--env=FAIL_MODE=exit-on-rank3 \
		sleep 30 \
		| flux python ${SIGN_AS} 42 >signed.json &&
	jobid=$(FLUX_HANDLE_USERID=42 \
		flux job submit --flags=signed signed.json) &&
	flux job wait-event -vHt 60 $jobid exception
'
test_expect_success 'job-exec: only the affected rank is drained' '
	test_debug "flux resource drain -o long" &&
	test "$(flux resource drain -no {ranks})" = "3" &&
	flux resource drain -no {reason} | grep "terminated before first"
'
test_expect_success 'job-exec: undrain all ranks' '
	flux resource list &&
	flux resource undrain $(flux resource drain -no {ranks})
'
# Ensure "exited before first barrier" doesn't drain ranks when exception
# occurs before first barrier
test_expect_success 'job-exec: barrier timeout raises job exception' '
	flux run --dry-run -N4 \
		--env=FAIL_MODE=hang-on-all \
		--setattr=exec.bulkexec.barrier-timeout=0.5 \
		sleep 30 \
		| flux python ${SIGN_AS} 42 >signed.json &&
	jobid=$(FLUX_HANDLE_USERID=42 \
		flux job submit --flags=signed signed.json) &&
	flux job wait-event -vHt 60 $jobid start &&
	flux cancel $jobid &&
	flux job wait-event -vHt 60 $jobid clean
'
test_expect_success 'job-exec: no ranks were drained' '
	test "$(flux resource drain -no {ranks})" = ""
'
#  Configure failing IMP
test_expect_success 'job-exec: reconfig with failing dummy IMP' '
	cat <<-EOF >conf.d/exec.toml
	[exec]
	imp = "${IMP_FAIL}"
	EOF
'

test_expect_success 'job-exec: reconfig and reload module' '
	flux config reload &&
	flux module reload -f job-exec
'
test_expect_success 'job-exec: IMP failure on one rank terminates job' '
	FAKE_USERID=42 &&
	id=$(FLUX_HANDLE_USERID=${FAKE_USERID} \
		flux job submit --flags=signed sleep-job.signed) &&
	flux job wait-event -vt 20 ${id}  clean &&
	test_must_fail_or_be_terminated flux job attach -vEX ${id}
'
test_expect_success 'job-exec: failed rank is drained' '
	flux resource drain -no "{ranks} {reason}" >drain.out &&
	test_debug "cat drain.out" &&
	cat <<-EOF >drain.expected &&
	0-1 $id terminated before first barrier
	EOF
	test_cmp drain.expected drain.out
'
test_expect_success 'undrain ranks' '
	flux resource undrain 0-1
'

test_done
