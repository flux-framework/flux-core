#!/bin/sh
#

test_description="Test that Flux's MPI personalities work"

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux ${SIZE}

run_program() {
        local timeout=$1
	local nnodes=$2
	local ntasks=$3
	shift 3
        run_timeout $timeout flux mini run $OPTS -n${ntasks} -N${nnodes} $*
}

CONF_PMI_LIBRARY_PATH=$(flux getattr conf.pmi_library_path | sed 's|/libpmi.so||')

test_expect_success 'mvapich mpi prepends to LD_LIBRARY_PATH for tasks' '
  run_program 5 ${SIZE} ${SIZE} printenv LD_LIBRARY_PATH > mvapich.ld.out &&
  result="$(uniq mvapich.ld.out | cut -d: -f1)" &&
  echo $result &&
  echo $CONF_PMI_LIBRARY_PATH &&
  test "$result" = $CONF_PMI_LIBRARY_PATH
'

test_expect_success 'mvapich mpi sets MPIRUN_RANK for tasks' '
  run_program 5 ${SIZE} ${SIZE} printenv MPIRUN_RANK \
    | sort > mvapich.rank.out &&
  test_debug "cat mvapich.rank.out" &&
  printf "0\n1\n2\n3\n" > mvapich.rank.expected &&
  test_cmp mvapich.rank.expected mvapich.rank.out
'

test_expect_success "intel mpi rewrites I_MPI_MPI_LIBRARY" '
  export I_MPI_PMI_LIBRARY="foobar" &&
  test_when_finished "unset I_MPI_PMI_LIBRARY" &&
  run_program 5 ${SIZE} ${SIZE} printenv I_MPI_PMI_LIBRARY > intel-mpi.set &&
  test "$(uniq intel-mpi.set)" = "$(flux getattr conf.pmi_library_path)"
'

test_expect_success "intel mpi only rewrites when necessary" '
   echo $I_MPI_PMI_LIBRARY &&
   run_program 5 ${SIZE} ${SIZE} printenv | grep I_MPI_PMI_LIBRARY \
       | tee intel-mpi.unset &&
   test "$(wc -l intel-mpi.unset | cut -f 1 -d " ")" = "0"
'

test_expect_success NO_ASAN,HAVE_JQ "spectrum mpi only enabled with option" '
  LD_PRELOAD_saved=${LD_PRELOAD} &&
  unset LD_PRELOAD &&
  test_when_finished "export LD_PRELOAD=${LD_PRELOAD_saved}" &&
  flux jobspec srun -n${SIZE} -N${SIZE} printenv LD_PRELOAD \
    | jq ".attributes.system.shell.options.mpi = \"spectrum\"" > j.spectrum &&
  test_expect_code 1 run_program 5 ${SIZE} ${SIZE} printenv LD_PRELOAD &&
  jobid=$(flux job submit j.spectrum) &&
  flux job attach ${jobid} > spectrum.out &&
  grep /opt/ibm/spectrum spectrum.out
'

test_done
