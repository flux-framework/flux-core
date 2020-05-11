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

parse_jobid() {
        outfile=$1
        jobid=$(cat ${outfile} | grep jobid | awk '{ print $2 }')
        echo ${jobid}
}

test_expect_success 'debugger: launching under debugger via flux-mini works' '
        flux mini run -v --debug-emulate -N 2 -n 2 sleep 0 2> jobid.out &&
        jobid=$(parse_jobid jobid.out) &&
        echo ${jobid} > jobid &&
        flux job wait-event -vt 2.5 ${jobid} finish
'

test_expect_success 'debugger: submitting under debugger via flux-mini works' '
        jobid=$(flux mini submit -N 2 -n 2 -o stop-tasks-in-exec hostname) &&
        flux job wait-event -vt 2.5  ${jobid} start &&
        flux job attach --debug-emulate ${jobid} &&
        flux job wait-event -vt 2.5  ${jobid} finish
'

test_expect_success HAVE_JQ 'debugger: submitting under debugger via flux-job works' '
        flux jobspec srun -N2 -n 2 hostname | stop_tasks_test > stop_tasks.json &&
        jobid=$(flux job submit stop_tasks.json) &&
        flux job wait-event -vt 2.5  ${jobid} start &&
        flux job attach --debug-emulate ${jobid} &&
        flux job wait-event -vt 2.5  ${jobid} finish
'

test_expect_success 'debugger: SIGCONT can unlock a job in stop-tasks-in-exec' '
        jobid=$(flux mini submit -N 2 -n 2 -o stop-tasks-in-exec hostname) &&
        flux job wait-event -vt 2.5  ${jobid} start &&
        flux job kill --signal=SIGCONT ${jobid} &&
        flux job wait-event -vt 2.5  ${jobid} finish
'

test_expect_success 'debugger: attaching to a running job works' '
        jobid=$(flux jobspec srun -n 1 ${stall} done 10 | flux job submit) &&
        flux job wait-event -vt 2.5 ${jobid} start &&
        ${waitfile} -v -t 2.5 done &&
        flux job attach -v --debug-emulate ${jobid} &&
        flux job wait-event -vt 2.5 ${jobid} finish
'

test_expect_success 'debugger: attaching to a finished job must fail' '
        jobid=$(flux jobspec srun -n 2 hostname | flux job submit) &&
        flux job wait-event -vt 2.5 ${jobid} finish &&
        test_must_fail flux job attach --debug-emulate ${jobid}
'

test_expect_success 'debug-emulate: attaching to a failed job must fail' '
        jobid=$(flux jobspec srun -n 2 ./bad_cmd | flux job submit) &&
        flux job wait-event -vt 2.5 ${jobid} finish &&
        test_must_fail flux job attach --debug-emulate ${jobid}
'

test_done
