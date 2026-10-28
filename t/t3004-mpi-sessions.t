#!/bin/sh
#

test_description='Test MPI-4 Sessions over Flux PMI

An MPI Session program builds a communicator from the WORLD process set
and from an even-rank subset.  MPICH >= 5.0 implements true Sessions
and bootstraps the subset communicator with a PMI-1.2 group barrier when
the process manager offers PMI-1.2, falling back to whole-job barriers
otherwise.  MPICH 4.x builds an internal MPI_COMM_WORLD behind the
Sessions API and always uses whole-job barriers.

This test runs a Sessions program against the server pinned to PMI-1.1
with pmi-simple.version=1.1, then against the default PMI-1.2 server,
and traces the wire to check what the subset communicator used: only
whole-job barriers against 1.1, and on MPICH >= 5 a tagged group
barrier against 1.2.

Skipped unless FLUX_TEST_MPI is set, the MPI is MPICH, and MPI-4
Sessions are available.

N.B. Two MPICH details affect whether MPICH >= 5 uses the group barrier
at all, and the wire assertion guards against both:
 - MPICH 5.0.x configure only enables PMI_Barrier_group when linking a
   third party libpmi, so a build with its own embedded libpmi needs
   CPPFLAGS=-DHAVE_PMI_BARRIER_GROUP=1 (see scripts/fetch-and-build-mpich.sh).
 - MPICH main after v5.0.2 defaults MPIR_CVAR_PMI_DISABLE_GROUP to 1.
   The test sets it to 0 explicitly.
'

. `dirname $0`/sharness.sh

if test -z "$FLUX_TEST_MPI"; then
    skip_all='skipping MPI tests, FLUX_TEST_MPI not set'
    test_done
fi

SESSIONS=${FLUX_BUILD_DIR}/t/mpi/sessions
VERSION=${FLUX_BUILD_DIR}/t/mpi/version
if ! test -x ${SESSIONS} || ! test -x ${VERSION}; then
    skip_all='skipping MPI tests, MPI not available/configured'
    test_done
fi
# The PMI-1.2 group barrier is an MPICH-specific mechanism; other MPIs
# bootstrap Sessions differently (e.g. via PMIx), so restrict to MPICH.
if ! ${VERSION} 2>&1 | grep -q MPICH; then
    skip_all='skipping MPI Sessions tests, MPI is not MPICH'
    test_done
fi
# Skip if this MPI was built without MPI-4 Sessions.
if ! ${SESSIONS} --probe; then
    skip_all='skipping MPI Sessions tests, MPI-4 Sessions not supported'
    test_done
fi

# MPICH >= 5.0 has PMI_Barrier_group and true Sessions (see above).
MPICH_MAJOR=$(${VERSION} | sed -n 's/^MPICH Version:[ \t]*\([0-9]*\).*/\1/p')
if test "${MPICH_MAJOR:-0}" -ge 5; then
    test_set_prereq GROUP_BARRIER
else
    echo "# MPICH ${MPICH_MAJOR}.x: the group barrier is exercised only" \
         "with MPICH >= 5"
fi

# work around https://github.com/open-mpi/ompi/issues/7701 and similar in mpich
export HWLOC_COMPONENTS=-gl
export MPIR_CVAR_PMI_DISABLE_GROUP=0
export TEST_UNDER_FLUX_CORES_PER_RANK=4
SIZE=2
MAX_MPI_SIZE=$(($SIZE*$TEST_UNDER_FLUX_CORES_PER_RANK))
test_under_flux $SIZE job

OPTS="-ocpu-affinity=off -overbose=1"

test_expect_success 'MPI Sessions job runs on one node (n=2)' '
	run_timeout 60 flux run -n2 -N1 $OPTS ${SESSIONS}
'
test_expect_success 'MPI Sessions job runs across nodes (n=4)' '
	run_timeout 60 flux run -n4 -N2 $OPTS ${SESSIONS}
'
test_expect_success 'MPI Sessions job runs at max size' '
	run_timeout 60 flux run -n${MAX_MPI_SIZE} $OPTS ${SESSIONS}
'
# Trace the PMI wire protocol (-o verbose=2) and check what the subset
# communicator's business card exchange actually sent, first against a
# server pinned to PMI-1.1 and then against the default PMI-1.2 server.
test_expect_success 'MPI Sessions job runs with PMI tracing (1.1 server)' '
	run_timeout 60 flux run -n4 -N2 -ocpu-affinity=off -overbose=2 \
	    -o pmi-simple.version=1.1 ${SESSIONS} 2>trace.err
'
test_expect_success 'server replied pmi_subversion=1' '
	grep "cmd=response_to_init.*pmi_subversion=1" trace.err
'
test_expect_success 'client used only whole-job barriers' '
	grep "cmd=barrier_in" trace.err &&
	test_must_fail grep "cmd=barrier_in group=" trace.err
'
test_expect_success 'MPI Sessions job runs with PMI tracing (1.2 server)' '
	run_timeout 60 flux run -n4 -N2 -ocpu-affinity=off -overbose=2 \
	    ${SESSIONS} 2>trace12.err
'
test_expect_success 'server replied pmi_subversion=2' '
	grep "cmd=response_to_init.*pmi_subversion=2" trace12.err
'
test_expect_success GROUP_BARRIER 'subset communicator used a group barrier' '
	grep "cmd=barrier_in group=" trace12.err &&
	grep "cmd=barrier_out tag=" trace12.err
'
# On MPICH 4.x a group barrier would mean the version check is wrong.
test_expect_success !GROUP_BARRIER 'MPICH 4.x client used only 1.1 barriers' '
	grep "cmd=barrier_in" trace12.err &&
	test_must_fail grep "cmd=barrier_in group=" trace12.err
'

test_done
