#!/bin/sh
#
test_description='Test flux-shell task exit support'

. `dirname $0`/sharness.sh

test_under_flux 2 job

test_expect_success 'flux-shell: first task exit posts shell.task-exit event' '
	jobid=$(flux submit true) &&
	run_timeout 10 flux job wait-event -p exec \
		$jobid shell.task-exit
'

test_expect_success 'flux-shell: create 30s sleep script - rank 1 exits early' '
	cat >testscript.sh <<-EOT &&
	#!/bin/bash
	test \$FLUX_TASK_RANK -eq 1 && exit 200
	sleep 30
	EOT
	chmod +x testscript.sh
'

test_expect_success 'flux-shell: run script with 2 tasks and 1s timeout' '
	test_must_fail run_timeout 30 flux run \
		-n2 -o exit-timeout=1s ./testscript.sh 2>tmout.err &&
	grep "exception.*timeout" tmout.err
'

test_expect_success 'flux-shell: run script with 2 nodes and 1s timeout' '
	test_must_fail run_timeout 30 flux run \
		-n2 -N2 -o exit-timeout=1s ./testscript.sh 2>tmout2.err &&
	grep "exception.*timeout" tmout2.err
'

test_expect_success 'flux-shell: run script with 2 tasks and exit-on-error' '
	test_must_fail run_timeout 30 flux run \
		-n2 -o exit-on-error ./testscript.sh
'

test_expect_success 'flux-shell: run script with 2 nodes and exit-on-error' '
	test_must_fail run_timeout 30 flux run \
		-n2 -N2 -o exit-on-error ./testscript.sh
'
test_expect_success 'flux-shell: exit-timeout catches lost shell' '
	cat >test2.sh <<-"EOF" &&
	#!/bin/bash
	if test $FLUX_TASK_RANK -eq 1; then
	    kill -9 $PPID
	    exit
	fi
	sleep 30
	EOF
	chmod +x test2.sh &&
	test_must_fail run_timeout 30 flux run \
		-n2 -N2 -o exit-timeout=1s ./test2.sh
'
test_expect_success 'flux-shell: exit-on-error catches lost shell' '
	test_must_fail run_timeout 30 flux run \
		-n2 -N2 -o exit-on-error ./test2.sh
'
test_expect_success 'flux-shell: exit-timeout=aaa is rejected' '
	test_must_fail flux run -o exit-timeout=aaa true
'
test_expect_success 'flux-shell: exit-timeout=false is rejected' '
	test_must_fail flux run -o exit-timeout=false true
'
test_expect_success 'flux-shell: exit-timeout=none is accepted' '
	flux run -o exit-timeout=none true
'
test_expect_success 'flux-shell: exit-timeout=100 is accepted' '
	flux run -o exit-timeout=100 true
'
test_expect_success 'flux-shell: exit-timeout=42.34 is accepted' '
	flux run -o exit-timeout=42.34 true
'

test_done
