#!/bin/sh

test_description='Test flux job status'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

test_expect_success 'status: submit a series of jobs' '
	zero=$(flux mini submit /bin/true) &&
	one=$(flux mini submit /bin/false) &&
	sigint=$(flux mini submit sh -c "kill -INT \$$") &&
	shell_sigquit=$(flux mini submit sh -c "kill -QUIT \$PPID") &&
	unsatisfiable=$(flux mini submit -n 1024 hostname) &&
	killed=$(flux mini submit sleep 600) &&
	flux queue stop &&
	canceled=$(flux mini submit -n 1024 hostname) &&
	flux job cancel ${canceled} &&
	flux queue start
'
test_expect_success 'status: exits with error with no jobs specified' '
	test_expect_code 1 flux job status
'
test_expect_success 'status: returns status 0 for successful job' '
	run_timeout 10 flux job status ${zero}
'
test_expect_success 'status: returns status 1 for failed job' '
	test_expect_code 1 flux job status ${one}
'
test_expect_success 'status: returns status 130 for SIGINT job' '
	test_expect_code 130 flux job status -v ${sigint}
'
test_expect_success 'status: returns status 130 for SIGINT job' '
	test_expect_code 130 flux job status -v ${sigint}
'
test_expect_success 'status: returns status 131 when job-shell gets SIGQUIT' '
	test_expect_code 131 flux job status -v ${shell_sigquit}
'
test_expect_success 'status: returns 1 for unsatisfiable job' '
	test_expect_code 1 flux job status -v ${unsatisfiable}
'
test_expect_success 'status: returns 1 for canceled pending job' '
	test_expect_code 1 flux job status -v ${canceled}
'
test_expect_success 'status: --exception-exit-code works' '
	test_expect_code 42 flux job status -v --exception-exit-code=42 ${canceled} &&
	test_expect_code 255 flux job status -v --exception-exit-code=255 ${unsatisfiable}
'
test_expect_success 'status: returns 143 (SIGTERM) for canceled running job' '
	flux job wait-event -p guest.exec.eventlog ${killed} shell.start &&
	flux job cancel ${killed} &&
	test_expect_code 143 flux job status -v ${killed}
'
test_expect_success 'status: returns highest status for multiple jobs' '
	test_expect_code 130 flux job status -vv ${zero} ${one} ${sigint}
'
test_expect_success 'status: fails expectedly on non-existent job' '
	test_expect_code 1 flux job status 123
'
test_done
