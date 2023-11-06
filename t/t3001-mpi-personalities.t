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
	run_timeout $timeout flux run $OPTS -n${ntasks} -N${nnodes} $*
}

test_expect_success 'LD_LIBRARY_PATH is not being set by MPI personalties' '
	test_must_fail flux run --env-remove=* printenv LD_LIBRARY_PATH
'

test_expect_success NO_ASAN "spectrum mpi only enabled with option" '
	LD_PRELOAD_saved=${LD_PRELOAD} &&
	unset LD_PRELOAD &&
	test_when_finished "export LD_PRELOAD=${LD_PRELOAD_saved}" &&
	flux run -n${SIZE} -N${SIZE} \
	  -o mpi=spectrum --dry-run printenv LD_PRELOAD > j.spectrum &&
	test_expect_code 1 run_program 15 ${SIZE} ${SIZE} printenv LD_PRELOAD &&
	jobid=$(flux job submit j.spectrum) &&
	flux job attach ${jobid} > spectrum.out &&
	grep /opt/ibm/spectrum spectrum.out
'

test_expect_success NO_ASAN "spectrum mpi also enabled with spectrum@version" '
	LD_PRELOAD_saved=${LD_PRELOAD} &&
	unset LD_PRELOAD &&
	test_when_finished "export LD_PRELOAD=${LD_PRELOAD_saved}" &&
	flux run -n${SIZE} -N${SIZE} \
	  -o mpi=spectrum@10.4 --dry-run printenv LD_PRELOAD > j.spectrum &&
	test_expect_code 1 run_program 15 ${SIZE} ${SIZE} printenv LD_PRELOAD &&
	jobid=$(flux job submit j.spectrum) &&
	flux job attach ${jobid} > spectrum.out &&
	grep /opt/ibm/spectrum spectrum.out
'

test_expect_success 'spectrum mpi sets OMPI_COMM_WORLD_RANK' '
	flux run -n${SIZE} -N${SIZE} \
	  -o mpi=spectrum --dry-run \
	  printenv OMPI_COMM_WORLD_RANK > j.spectrum &&
	jobid=$(flux job submit j.spectrum) &&
	flux job attach ${jobid} | sort -n > spectrum.rank.out &&
	test_debug "cat spectrum.rank.out" &&
	printf "0\n1\n2\n3\n" > spectrum.rank.expected &&
	test_cmp spectrum.rank.expected spectrum.rank.out
'

export FLUX_SHELL_RC_PATH=$(pwd)/shellrc
test_expect_success 'create test mpi plugins in $FLUX_SHELL_RC_PATH' '
	mkdir -p shellrc/mpi &&
	cat <<-EOF >shellrc/mpi/test.lua &&
	shell.setenv ("MPI_TEST_NOVERSION", "t")
	EOF
	cat <<-EOF >shellrc/mpi/test@1.lua &&
	shell.setenv ("MPI_TEST_VERSION", "1")
	EOF
	cat <<-EOF >shellrc/mpi/test@2.lua
	shell.setenv ("MPI_TEST_VERSION", "2")
	EOF
'
test_expect_success '-ompi=name loads path/mpi/name.lua' '
	name=mpi-test &&
	flux run -o mpi=test env | grep MPI_TEST > ${name}.out &&
	test_debug "cat ${name}.out" &&
	cat <<-EOF >${name}.expected &&
	MPI_TEST_NOVERSION=t
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success '-ompi=name loads path/mpi/name.lua' '
	name=mpi-test1 &&
	flux run -o mpi=test@1 env | grep MPI_TEST > ${name}.out &&
	test_debug "cat ${name}.out" &&
	cat <<-EOF >${name}.expected &&
	MPI_TEST_NOVERSION=t
	MPI_TEST_VERSION=1
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_expect_success '-ompi=name loads path/mpi/name.lua' '
	name=mpi-test2 &&
	flux run -o mpi=test@2 env | grep MPI_TEST > ${name}.out &&
	test_debug "cat ${name}.out" &&
	cat <<-EOF >${name}.expected &&
	MPI_TEST_NOVERSION=t
	MPI_TEST_VERSION=2
	EOF
	test_cmp ${name}.expected ${name}.out
'
test_done
