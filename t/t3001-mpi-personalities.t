#!/bin/sh
#

test_description="Test that Flux's MPI personalities work"

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} wreck
echo "# $0: flux session size will be ${SIZE}"

# Usage: run_program timeout ntasks nnodes
run_program() {
        local timeout=$1
        local ntasks=$2
        local nnodes=$3
        shift 3
        run_timeout $timeout flux wreckrun $OPTS \
                    -n${ntasks} -N${nnodes} $*
}

test_expect_success "intel mpi rewrites I_MPI_MPI_LIBRARY" '
  export I_MPI_PMI_LIBRARY="foobar" \
  && run_program 5 ${SIZE} ${SIZE} printenv I_MPI_PMI_LIBRARY \
     | tee intel-mpi.set \
  && test "$(uniq intel-mpi.set)" = "$(flux getattr conf.pmi_library_path)"
'

test_expect_success "intel mpi only rewrites when necessary" '
   run_program 5 ${SIZE} ${SIZE} printenv $I_MPI_PMI_LIBRARY \
	| tee intel-mpi.unset \
    && test "$(wc -l intel-mpi.unset | cut -f 1 -d " ")" = "0"
'

OPTS="-o mpi=spectrum"
test_expect_success "spectrum mpi sets MPI_ROOT" '
  run_program 5 ${SIZE} ${SIZE} printenv MPI_ROOT \
        | tee spectrum-mpi.env \
    && test "$(uniq spectrum-mpi.env)" = "/opt/ibm/spectrum_mpi"
'

OPTS="-o mpi=spectrum"
test_expect_success "spectrum mpi sets the soft stack limit" '
  run_timeout 5 flux wreckrun -n ${SIZE} -N ${SIZE} $OPTS bash -c "ulimit -s" \
        | tee spectrum-mpi.ulimit \
        && test "$(uniq spectrum-mpi.ulimit)" = "10240"
'

test_done
