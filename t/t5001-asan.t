#!/bin/sh

test_description='Run broker under asan with a small workload'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if test_have_prereq NO_ASAN; then
    skip_all='skipping asan tests since AddressSanitizer not active'
    test_done
fi

ASAN_WORKLOAD=${SHARNESS_TEST_SRCDIR}/asan/asan-workload.sh

ASAN_NBROKERS=${ASAN_NBROKERS:-2}

# Allow jobs to run under sdexec in test, if available
cat >asan.toml <<-EOT
[exec]
service-override = true
EOT

test_expect_success \
  "reports no new errors on $ASAN_NBROKERS broker run" '
	run_timeout 300 \
	flux start -s ${ASAN_NBROKERS} \
		--test-exit-timeout=120 \
		--config-path=asan.toml \
		 ${ASAN_WORKLOAD}
'
test_done
