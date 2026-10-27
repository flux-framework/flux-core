#!/bin/sh
#

test_description='Test MPI-4 Sessions over Flux PMI

An MPI Session program builds a communicator from the WORLD process set
and from an even-rank subset.  MPICH >= 5.0 implements true Sessions
and bootstraps the subset communicator with a PMI-1.2 group barrier when
the process manager offers PMI-1.2, falling back to whole-job barriers
otherwise.  MPICH 4.x builds an internal MPI_COMM_WORLD behind the
Sessions API and always uses whole-job barriers.

The Flux PMI server offers PMI-1.1, so this test checks that a Sessions
program bootstraps over the fallback path and that no group barrier
appears on the wire.

Skipped unless FLUX_TEST_MPI is set, the MPI is MPICH, and MPI-4
Sessions are available.

N.B. Two MPICH details affect whether MPICH >= 5 would use the group
barrier at all, and the wire assertion below is only meaningful when
both are accounted for:
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
# communicator's business card exchange actually sent.
test_expect_success 'MPI Sessions job runs with PMI tracing' '
	run_timeout 60 flux run -n4 -N2 -ocpu-affinity=off -overbose=2 \
	    ${SESSIONS} 2>trace.err
'
test_expect_success 'server replied pmi_subversion=1' '
	grep "cmd=response_to_init.*pmi_subversion=1" trace.err
'
test_expect_success 'client used only whole-job barriers' '
	grep "cmd=barrier_in" trace.err &&
	test_must_fail grep "cmd=barrier_in group=" trace.err
'

test_done
