#!/bin/sh
#

test_description='Verify runlevels work properly
'

. `dirname $0`/sharness.sh
test_under_flux 1 minimal

test_expect_success 'sharness minimal init.run-level=2 init.mode=normal' '
	runlevel=$(flux getattr init.run-level) &&
	test $runlevel -eq 2 &&
	runmode=$(flux getattr init.mode) &&
	test $runmode = "normal"
'

test_expect_success 'sharness minimal has transitioned normally thus far' '
	flux dmesg >default.log &&
	grep -q "Run level 1 starting" default.log &&
	grep -q "Run level 1 Exited" default.log &&
	grep -q "Run level 2 starting" default.log &&
	! grep -q "Run level 2 Exited" default.log &&
	! grep -q "Run level 3" default.log
'

test_expect_success 'new init.mode=normal instance transitions appropriately' '
	flux start -o,-Slog-stderr-level=6,-Sinit.mode=normal \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		/bin/true 2>normal.log &&
	grep -q "Run level 1 starting" normal.log &&
	grep -q "Run level 1 Exited" normal.log &&
	grep -q "Run level 2 starting" normal.log &&
	grep -q "Run level 2 Exited" normal.log &&
	grep -q "Run level 3 starting" normal.log &&
	grep -q "Run level 3 Exited" normal.log
'

test_expect_success 'new init.mode=none instance transitions appropriately' '
	flux start -o,-Slog-stderr-level=6,-Sinit.mode=none \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		/bin/true 2>none.log &&
	grep -q "Run level 1 starting" none.log &&
	grep -q "Run level 1 Skipped mode=none" none.log &&
	grep -q "Run level 2 starting" none.log &&
	grep -q "Run level 2 Exited" none.log &&
	grep -q "Run level 3 starting" none.log &&
	grep -q "Run level 3 Skipped mode=none" none.log
'

test_expect_success 'rc1 failure transitions to rc3, fails instance' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=/bin/false,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6,-Sinit.mode=normal \
		/bin/true 2>false.log &&
	grep -q "Run level 1 starting" false.log &&
	grep -q "Run level 1 Exited with non-zero status" false.log &&
	! grep -q "Run level 2 starting" false.log &&
	! grep -q "Run level 2 Exited" false.log &&
	grep -q "Run level 3 starting" false.log &&
	grep -q "Run level 3 Exited" false.log
'

test_expect_success 'rc1 bad path handled same as failure' '
	(
	  SHELL=/bin/sh &&
	  test_expect_code 127 flux start \
		-o,-Sbroker.rc1_path=rc1-nonexist,-Sbroker.rc3_path= \
		-o,-Slog-stderr-level=6,-Sinit.mode=normal \
		/bin/true 2>bad1.log
	) &&
	grep -q "Run level 1 starting" bad1.log &&
	grep -q "Run level 1 Exited with non-zero status" bad1.log &&
	! grep -q "Run level 2 starting" bad1.log &&
	! grep -q "Run level 2 Exited" bad1.log &&
	grep -q "Run level 3 starting" bad1.log &&
	grep -q "Run level 3 Exited" bad1.log
'

test_expect_success 'rc3 failure causes instance failure' '
	! flux start \
		-o,-Sbroker.rc3_path=/bin/false \
		-o,-Slog-stderr-level=6,-Sinit.mode=normal \
		/bin/true 2>false3.log &&
	grep -q "Run level 1 starting" false3.log &&
	grep -q "Run level 1 Exited" false3.log &&
	grep -q "Run level 2 starting" false3.log &&
	grep -q "Run level 2 Exited" false3.log &&
	grep -q "Run level 3 starting" false3.log &&
	grep -q "Run level 3 Exited with non-zero status" false3.log
'

test_expect_success 'rc1 environment is as expected' '
	flux start \
		-o,-Sbroker.rc1_path=${FLUX_SOURCE_DIR}/t/rc/rc1-testenv \
		-o,-Slog-stderr-level=6,-Sinit.mode=normal \
		/bin/true 2>&1 | tee rc1-test.log &&
	grep "stderr-" rc1-test.log | egrep -q broker.*err &&
	grep "stdout-" rc1-test.log | egrep -q broker.*info
'

test_done
