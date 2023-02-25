#!/bin/sh
#
test_description='Test parallel debugger support in emulation mode'

. `dirname $0`/sharness.sh

stall="${SHARNESS_TEST_DIRECTORY}/debug/stall"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

stop_tasks_test()     {
	jq '.attributes.system.shell.options."stop-tasks-in-exec" = 1';
}

test_under_flux 2

TIMEOUT=10

parse_jobid() {
	outfile=$1 &&
	jobid=$(cat ${outfile} | grep ^jobid: | awk '{ print $2 }') &&
	echo ${jobid}
}

parse_totalview_jobid() {
	outfile=$1 &&
	jobid=$(cat ${outfile} | grep totalview_jobid | \
	awk '{ print $2 }' | awk -F= '{ print $2 }') &&
	echo ${jobid}
}

test_expect_success 'debugger: launching under debugger via flux-mini works' '
	flux run -v --debug-emulate -N 2 -n 2 sleep 0 2> jobid.out &&
	jobid=$(parse_jobid jobid.out) &&
	echo ${jobid} > jobid &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish
'

test_expect_success 'debugger: submitting under debugger via flux-mini works' '
	jobid=$(flux submit -N 2 -n 2 -o stop-tasks-in-exec hostname) &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} start &&
	flux job attach --debug-emulate ${jobid} &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} finish
'

test_expect_success HAVE_JQ 'debugger: submitting under debugger via flux-job works' '
	flux run --dry-run -N2 -n 2 hostname \
		| stop_tasks_test > stop_tasks.json &&
	jobid=$(flux job submit stop_tasks.json) &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} start &&
	flux job attach --debug-emulate ${jobid} &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} finish
'

test_expect_success 'debugger: SIGCONT can unlock a job in stop-tasks-in-exec' '
	jobid=$(flux submit -N 2 -n 2 -o stop-tasks-in-exec hostname) &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} start &&
	flux job kill --signal=SIGCONT ${jobid} &&
	flux job wait-event -vt ${TIMEOUT}  ${jobid} finish
'

test_expect_success 'debugger: attaching to a running job works' '
	jobid=$(flux submit ${stall} done 10) &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} start &&
	${waitfile} -v -t ${TIMEOUT} done &&
	flux job attach -v --debug-emulate ${jobid} &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish
'

test_expect_success 'debugger: attaching to a finished job must fail' '
	jobid=$(flux submit -n 2 hostname) &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish &&
	test_must_fail flux job attach --debug-emulate ${jobid}
'

test_expect_success 'debug-emulate: attaching to a failed job must fail' '
	jobid=$(flux submit -n 2 ./bad_cmd) &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish &&
	test_must_fail flux job attach --debug-emulate ${jobid}
'

test_expect_success 'debugger: totalview_jobid is set for attach mode' '
	jobid=$(flux submit ${stall} done2 10) &&
	jobid=$(flux job id ${jobid}) &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} start &&
	${waitfile} -v -t ${TIMEOUT} done2 &&
	flux job attach -vv --debug-emulate ${jobid} 2> jobid.out2 &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish &&
	tv_jobid=$(parse_totalview_jobid jobid.out2) &&
	test ${tv_jobid} = "${jobid}"
'

flux_job_attach() {
	flux job attach -vv --debug ${1} 2> ${2} &
	${waitfile} -v -t ${TIMEOUT} --pattern="totalview_jobid" ${2}
}

# flux job attach --debug JOBID must not continue target processes
test_expect_success 'debugger: job attach --debug must not continue target' '
	jobid=$(flux submit ${stall} done3 100) &&
	jobid=$(flux job id ${jobid}) &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} start &&
	${waitfile} -v -t ${TIMEOUT} done3 &&
	flux_job_attach ${jobid} jobid.out3 &&
	tv_jobid=$(parse_totalview_jobid jobid.out3) &&
	test ${tv_jobid} = "${jobid}" &&
	test_must_fail flux job wait-event -vt ${TIMEOUT} ${jobid} finish &&
	flux job cancel ${jobid} &&
	flux job wait-event -vt ${TIMEOUT} ${jobid} finish
'

test_done
