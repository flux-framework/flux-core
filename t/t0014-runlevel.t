#!/bin/sh
#

test_description='Verify runlevels work properly
'

. `dirname $0`/sharness.sh
test_under_flux 1 minimal

test_expect_success 'sharness minimal init.run-level=2' '
	runlevel=$(flux getattr init.run-level) &&
	test $runlevel -eq 2
'

test_expect_success 'sharness minimal has transitioned normally thus far' '
	flux dmesg >default.log &&
	grep -q "Run level 1 starting" default.log &&
	grep -q "Run level 1 Not configured" default.log &&
	grep -q "Run level 2 starting" default.log &&
	! grep -q "Run level 2 Not configured" default.log &&
	! grep -q "Run level 3" default.log
'

test_expect_success 'new instance transitions appropriately' '
	flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		/bin/true 2>normal.log &&
	grep -q "Run level 1 starting" normal.log &&
	grep -q "Run level 1 Not configured" normal.log &&
	grep -q "Run level 2 starting" normal.log &&
	grep -q "Run level 2 Exited" normal.log &&
	grep -q "Run level 3 starting" normal.log &&
	grep -q "Run level 3 Not configured" normal.log
'

test_expect_success 'rc1 failure transitions to rc3, fails instance' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=/bin/false,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>false.log &&
	grep -q "Run level 1 starting" false.log &&
	grep -q "Run level 1 Exited with non-zero status" false.log &&
	! grep -q "Run level 2 starting" false.log &&
	! grep -q "Run level 2 Exited" false.log &&
	grep -q "Run level 3 starting" false.log &&
	grep -q "Run level 3 Not configured" false.log
'

test_expect_success 'rc1 bad path handled same as failure' '
	(
	  SHELL=/bin/sh &&
	  test_expect_code 127 flux start \
		-o,-Sbroker.rc1_path=rc1-nonexist,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>bad1.log
	) &&
	grep -q "Run level 1 starting" bad1.log &&
	grep -q "Run level 1 Exited with non-zero status" bad1.log &&
	! grep -q "Run level 2 starting" bad1.log &&
	! grep -q "Run level 2 Exited" bad1.log &&
	grep -q "Run level 3 starting" bad1.log &&
	grep -q "Run level 3 Not configured" bad1.log
'

test_expect_success 'rc3 failure causes instance failure' '
	! flux start \
		-o,-Sbroker.rc3_path=/bin/false \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>false3.log &&
	grep -q "Run level 1 starting" false3.log &&
	grep -q "Run level 1 Exited" false3.log &&
	grep -q "Run level 2 starting" false3.log &&
	grep -q "Run level 2 Exited" false3.log &&
	grep -q "Run level 3 starting" false3.log &&
	grep -q "Run level 3 Exited with non-zero status" false3.log
'

test_expect_success 'instance with no rc2 terminated cleanly by timeout' '
	run_timeout 0.5 flux start \
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
