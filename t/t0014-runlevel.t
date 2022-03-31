#!/bin/sh
#

test_description='Verify rc scripts excute with proper semantics
'
# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug

. `dirname $0`/sharness.sh

test_expect_success 'initial program is run when rc1/rc3 are nullified' '
	flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		/bin/true 2>normal.log
'

test_expect_success 'rc1 failure causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=/bin/false,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		sleep 3600 2>rc1_failure.log
'

test_expect_success 'rc1 bad path handled same as failure' '
	(
	  SHELL=/bin/sh &&
	  test_expect_code 127 flux start \
		-o,-Sbroker.rc1_path=rc1-nonexist,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>bad1.log
	)
'

test_expect_success 'default initial program is $SHELL' '
	run_timeout --env=SHELL=/bin/sh 15 \
		flux $SHARNESS_TEST_SRCDIR/scripts/runpty.py -i none \
		flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		>shell.log &&
	grep "rc2.0: /bin/sh Exited" shell.log
'

test_expect_success 'rc3 failure causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc3_path=/bin/false \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>rc3_failure.log
'

test_expect_success 'broker.rc2_none terminates by signal without error' '
	for timeout in 0.5 1 2 4; do
	    run_timeout -s ALRM $timeout flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Sbroker.rc2_none &&
	    break
	done
'

test_expect_success 'flux admin cleanup-push /bin/true works' '
	flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		flux admin cleanup-push /bin/true
'

test_expect_success 'flux admin cleanup-push /bin/false causes instance failure' '
	test_expect_code 1 flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		flux admin cleanup-push /bin/false
'

test_expect_success 'cleanup does not run if rc1 fails' '
	test_expect_code 1 flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=/bin/false,-Sbroker.rc3_path= \
		flux admin cleanup-push memorable-string 2>nocleanup.err && \
	test_must_fail grep memorable-string nocleanup.err
'

test_expect_success 'flux admin cleanup-push (empty) fails' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		flux admin cleanup-push "" 2>push.err &&
	grep "cannot push an empty command line" push.err
'

test_expect_success 'flux admin cleanup-push with no commands fails' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		flux admin cleanup-push </dev/null 2>push2.err &&
	grep "commands array is empty" push2.err
'

test_expect_success 'flux admin cleanup-push (stdin) works' '
	echo /bin/true | flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		flux admin cleanup-push 2>push-stdin.err &&
	grep cleanup.0 push-stdin.err
'

test_expect_success 'flux admin cleanup-push (stdin) retains cmd block order' '
	flux start -o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		flux admin cleanup-push <<-EOT 2>hello.err &&
	echo Hello world
	echo Hello solar system
	EOT
	grep "cleanup.0: Hello world" hello.err &&
	grep "cleanup.1: Hello solar system" hello.err
'

test_expect_success 'rc1 environment is as expected' '
	flux start \
		-o,-Sbroker.rc1_path=${FLUX_SOURCE_DIR}/t/rc/rc1-testenv \
		-o,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>&1 | tee rc1-test.log &&
	grep "stderr-" rc1-test.log | egrep -q broker.*err &&
	grep "stdout-" rc1-test.log | egrep -q broker.*info
'

test_done
