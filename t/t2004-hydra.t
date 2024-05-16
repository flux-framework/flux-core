#!/bin/sh
#

test_description='Test that MPICH Hydra can launch Flux'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh
PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
ARGS="-o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Sbroker.shutdown_path="

if ! which mpiexec.hydra 2>/dev/null; then
    skip_all='skipping hydra-launching-flux tests, mpiexec.hydra unavailable'
    test_done
fi

test_expect_success 'Flux libpmi-client wire protocol works with Hydra' '
	mpiexec.hydra -n 4 ${PMI_INFO}
'

test_expect_success 'Hydra can launch Flux' '
	mpiexec.hydra -n 4 flux start ${ARGS} flux getattr size
'

test_done
