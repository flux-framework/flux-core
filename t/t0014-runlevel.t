#!/bin/sh
#

test_description='Verify rc scripts excute with proper semantics
'

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

test_expect_success 'rc3 failure causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc3_path=/bin/false \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>rc3_failure.log
'

test_expect_success 'broker.rc2_none terminates by signal without error' '
	run_timeout -s ALRM 0.5 flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Sbroker.rc2_none
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
