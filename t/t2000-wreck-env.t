#!/bin/sh
#

test_description='Test basic wreck functionality

Test basic functionality of wreckrun facility.
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE} wreck

#  Return the previous jobid
last_job_id() {
	flux wreck last-jobid
}
#  Return previous job path in kvs
last_job_path() {
	flux wreck last-jobid -p
}

test_expect_success 'flux-wreck: setenv/getenv works' '
	flux wreck setenv FOO=BAR &&
	flux wreck getenv FOO
'
test_expect_success 'flux-wreck: unsetenv works' '
	flux wreck unsetenv FOO &&
	test "$(flux wreck getenv FOO)" = "FOO="
'
test_expect_success 'flux-wreck: setenv all' '
	flux wreck setenv all &&
	flux env /usr/bin/env | sort | grep -ve FLUX_URI -e HOSTNAME -e ENVIRONMENT > env.expected &&
	flux wreck getenv | sort > env.output &&
	test_cmp env.expected env.output
'
test_expect_success 'wreck: global lwj.environ exported to jobs' '
	flux wreck setenv FOO=bar &&
	test "$(flux wreckrun -n1 printenv FOO)" = "bar"
'
test_expect_success 'wreck: wreckrun exports environment vars not in global env' '
	BAR=baz flux wreckrun -n1 printenv BAR > printenv.out &&
	test "$(cat printenv.out)" = "baz"
'
test_expect_success 'wreck: wreckrun --skip-env works' '
	( export BAR=baz &&
          test_must_fail  flux wreckrun --skip-env -n1 printenv BAR > printenv2.out
	) &&
	test "$(cat printenv2.out)" = ""
'

test_done
