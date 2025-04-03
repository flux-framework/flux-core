#!/bin/sh
#
test_description='Test flux-shell behavior when plugin calls fork(2)'

. `dirname $0`/sharness.sh


test_under_flux 2 job

FORK_PLUGIN="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs/fork.so"
test_expect_success 'create shell initrc to load fork.so plugin' '
	cat <<-EOF >fork.lua
	plugin.load { file = "$FORK_PLUGIN" }
	EOF
'
test_expect_success 'submit sleep job with fork plugin' '
	id=$(flux submit -o userrc=fork.lua -N2 sleep 120)
'
test_expect_success 'cancel sleep job' '
	flux cancel $id
'
test_expect_success 'job should exit without hanging due to shell child' '
	flux job wait-event -vt 60 $id clean
'
test_expect_success 'submit hostname job with fork plugin and wait it to exit' '
	id=$(flux submit -o userrc=fork.lua -n1 hostname) &&
	flux job wait-event -Hp exec $id shell.task-exit
'
test_expect_success 'cancel hostname job' '
	flux cancel $id
'
test_expect_success 'job should complete without hanging due to shell child' '
	flux job wait-event -vt 60 $id clean
'

test_done
