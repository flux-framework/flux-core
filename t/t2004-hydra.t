#!/bin/sh
#

test_description='Test that MPICH Hydra can launch Flux'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh
PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

if ! which mpiexec.hydra 2>/dev/null; then
    skip_all='skipping hydra-launching-flux tests, mpiexec.hydra unavailable'
    test_done
fi

test_expect_success 'Flux libpmi-client wire protocol works with Hydra' '
	mpiexec.hydra -n 4 ${PMI_INFO}
'

# Hydra offered PMI-1.2 group barriers starting in MPICH 5.0.  Probe
# for it so the group barrier checks cover whichever Hydra is installed.
test_expect_success 'get pmi library path' '
	flux config builtin pmi_library_path >libpmi
'
if mpiexec.hydra -n 1 flux pmi --method=libpmi:$(flux config builtin \
    pmi_library_path) barrier --group=self >/dev/null 2>&1; then
	test_set_prereq HYDRA_PMI12
fi
# N.B. Hydra's exit status is unreliable when ranks exit unevenly, so
# check the client's error message rather than the status.
test_expect_success !HYDRA_PMI12 'libpmi group barrier fails on 1.1 Hydra' '
	(mpiexec.hydra -n 2 \
	    flux pmi --method=libpmi:$(cat libpmi) barrier --group=world \
	    2>group.err || true) &&
	grep "operation failed" group.err &&
	mpiexec.hydra -n 2 flux pmi --method=libpmi:$(cat libpmi) barrier
'
test_expect_success HYDRA_PMI12 'libpmi group barrier works on 1.2 Hydra' '
	mpiexec.hydra -n 2 \
	    flux pmi --method=libpmi:$(cat libpmi) barrier --group=world &&
	mpiexec.hydra -n 2 \
	    flux pmi --method=libpmi:$(cat libpmi) barrier --group=0,1
'

test_expect_success 'Hydra can launch Flux' '
	mpiexec.hydra -n 4 flux start ${ARGS} flux getattr size
'

test_done
