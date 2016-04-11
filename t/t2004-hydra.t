#!/bin/sh
#

test_description='Test that MICH Hydra can launch Flux'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if ! which mpiexec.hydra 2>/dev/null; then
    skip_all='skipping hydra-launching-flux tests, mpiexec.hydra unavailable'
    test_done
fi

test_expect_success 'Hydra runs hello world' '
	mpiexec.hydra -n 4 echo "Hello World"
'

test_expect_success 'Hydra sets PMI_FD to unique value' '
	test `mpiexec.hydra -n 4 printenv PMI_FD | sort | uniq | wc -l` -eq 4
'

test_expect_success 'Hydra sets PMI_RANK to unique value' '
	test `mpiexec.hydra -n 4 printenv PMI_RANK | sort | uniq | wc -l` -eq 4
'

test_expect_success 'Hydra sets PMI_SIZE to uniform value' '
	test `mpiexec.hydra -n 4 printenv PMI_SIZE | sort | uniq | wc -l` -eq 1
'

test_expect_success 'Flux libpmi-client wire protocol works with Hydra' '
	mpiexec.hydra -n 4 ${FLUX_BUILD_DIR}/t/pmi/pminfo
'

test_expect_success 'Hydra can launch Flux' '
	mpiexec.hydra -n 4 flux broker \
		flux comms info >flux_out &&
	grep size=4 flux_out
'

test_done
