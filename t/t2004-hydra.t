#!/bin/sh
#

test_description='Test that MPICH Hydra can launch Flux'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh
PMINFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pminfo

if ! which mpiexec.hydra 2>/dev/null; then
    skip_all='skipping hydra-launching-flux tests, mpiexec.hydra unavailable'
    test_done
fi

test_expect_success 'Hydra runs hello world' '
	mpiexec.hydra -n 4 echo "Hello World"
'

count_uniq_lines() { sort $1 | uniq | wc -l; }

test_expect_success 'Hydra sets PMI_FD to unique value' '
	mpiexec.hydra -n 4 printenv PMI_FD > out &&
	test_debug "cat out" &&
	test $(count_uniq_lines out) -eq 4
'

test_expect_success 'Hydra sets PMI_RANK to unique value' '
	mpiexec.hydra -n 4 printenv PMI_RANK > out2 &&
	test_debug "cat out2" &&
	test $(count_uniq_lines out2) -eq 4
'

test_expect_success 'Hydra sets PMI_SIZE to uniform value' '
	mpiexec.hydra -n 4 printenv PMI_SIZE > out3 &&
	test_debug "cat out3" &&
	test $(count_uniq_lines out3) -eq 1
'

test_expect_success 'Flux libpmi-client wire protocol works with Hydra' '
	mpiexec.hydra -n 4 ${PMINFO}
'

test_expect_success 'Hydra can launch Flux' '
	mpiexec.hydra -n 4 flux start \
		flux comms info >flux_out &&
	test_debug "cat flux_out" &&
	grep size=4 flux_out
'

test_done
