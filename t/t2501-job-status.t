#!/bin/sh

test_description='Test flux job status'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

test_expect_success 'status: submit a series of jobs' '
	zero=$(flux mini submit /bin/true) &&
	one=$(flux mini submit /bin/false) &&
	sigint=$(flux mini submit sh -c "kill -INT \$$") &&
	shell_sigquit=$(flux mini submit sh -c "kill -QUIT \$PPID")
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
test_expect_success 'status: returns highest status for multiple jobs' '
	test_expect_code 130 flux job status -vv ${zero} ${one} ${sigint}
'
test_expect_success 'status: fails expectedly on non-existent job' '
	test_expect_code 1 flux job status 123
'
test_done
