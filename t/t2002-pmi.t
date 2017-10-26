#!/bin/sh
#

test_description='Test that PMI works in a Flux-launched program 

Test that PMI works in a Flux-launched program 
'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} wreck
echo "# $0: flux session size will be ${SIZE}"
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest
PMINFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pminfo
LIBPMI=${FLUX_BUILD_DIR}/src/common/.libs/libpmi.so

# Usage: run_program timeout ntasks nnodes
run_program() {
	local timeout=$1
	local ntasks=$2
	local nnodes=$3
	shift 3
	run_timeout $timeout flux wreckrun -l -o stdio-delay-commit \
		    -n${ntasks} -N${nnodes} $*
}

# Requires lwj == 1
test_expect_success 'pmi: wreck sets FLUX_JOB_ID' '
	run_program 5 ${SIZE} ${SIZE} printenv FLUX_JOB_ID >output_appnum &&
        test `wc -l < output_appnum` = ${SIZE} &&
	test `cut -d: -f2 output_appnum | uniq` -eq 1
'

test_expect_success 'pmi: wreck sets FLUX_JOB_SIZE' '
	run_program 5 ${SIZE} ${SIZE} printenv FLUX_JOB_SIZE >output_size &&
        test `wc -l < output_size` -eq ${SIZE} &&
	test `cut -d: -f2 output_size | uniq` -eq ${SIZE}
'

test_expect_success 'pmi: wreck sets FLUX_TASK_RANK' '
	run_program 5 4 4 printenv FLUX_TASK_RANK | sort >output_rank &&
	cat >expected_rank <<-EOF  &&
	0: 0
	1: 1
	2: 2
	3: 3
	EOF
	test_cmp expected_rank output_rank
'

test_expect_success 'pmi: wreck sets FLUX_LOCAL_RANKS multiple tasks per node' '
	run_program 5 ${SIZE} 1 printenv FLUX_LOCAL_RANKS >output_clique &&
	test `cut -d: -f2 output_clique | uniq` = `seq -s, 0 $((${SIZE}-1))`
'

test_expect_success 'pmi: wreck sets FLUX_LOCAL_RANKS single task per node' '
	run_timeout 5 flux wreckrun -l -N4 printenv FLUX_LOCAL_RANKS \
						| sort >output_clique2 &&
	cat >expected_clique2 <<-EOF  &&
	0: 0
	1: 1
	2: 2
	3: 3
	EOF
	test_cmp expected_clique2 output_clique2
'

test_expect_success 'pmi: wreck sets PMI_SIZE' '
	run_program 5 ${SIZE} ${SIZE} printenv PMI_SIZE >output_size &&
        test `wc -l < output_size` -eq ${SIZE} &&
	test `cut -d: -f2 output_size | uniq` -eq ${SIZE}
'

test_expect_success 'pmi: wreck sets PMI_RANK' '
	run_program 5 4 4 printenv PMI_RANK | sort >output_rank &&
	cat >expected_rank <<-EOF  &&
	0: 0
	1: 1
	2: 2
	3: 3
	EOF
	test_cmp expected_rank output_rank
'

test_expect_success 'pmi: wreck sets PMI_FD' '
	run_program 1 1 1 printenv PMI_FD >/dev/null
'

test_expect_success 'pmi: wreck preputs PMI_process_mapping into kvs' '
	cat <<-EOF >print-pmi-map.sh &&
	#!/bin/sh
        if test \${FLUX_TASK_RANK} -eq 0; then
          KVS_PATH=\$(flux wreck kvs-path \${FLUX_JOB_ID})
          flux kvs get --json \${KVS_PATH}.pmi.PMI_process_mapping
        fi
	EOF
	chmod +x print-pmi-map.sh &&
	run_timeout 5 flux wreckrun -l -N4 -n4 ./print-pmi-map.sh >output_map &&
	cat >expected_map <<-EOF &&
	0: (vector,(0,4,1))
	EOF
	test_cmp expected_map output_map
'

test_expect_success 'pmi: PMI reports correct rank, size' '
	run_program 5 4 4 ${PMINFO} | sed -e"s/ appnum.*$//" \
				    | sort >output_pminfo &&
	cat >expected_pminfo <<-EOF &&
	0: 0: size=4
	1: 1: size=4
	2: 2: size=4
	3: 3: size=4
	EOF
	test_cmp expected_pminfo output_pminfo
'

test_expect_success 'pmi: dlopen failsafe works' '
	test_must_fail run_program 5 1 1 ${PMINFO} --library ${LIBPMI} \
					2>failsafe_output &&
	grep -q "PMI_Init: operation failed" failsafe_output
'

test_expect_success 'pmi: wreck sets clique for multiple tasks per node' '
	run_timeout 5 flux wreckrun -N1 -n4 ${PMINFO} --clique \
						| sort >output_pmclique &&
	cat >expected_pmclique <<-EOT &&
	0: clique=0,1,2,3
	1: clique=0,1,2,3
	2: clique=0,1,2,3
	3: clique=0,1,2,3
	EOT
	test_cmp expected_pmclique output_pmclique
'

test_expect_success 'pmi: wreck sets clique for single task per node' '
	run_timeout 5 flux wreckrun -N4 ${PMINFO} --clique \
						| sort >output_pmclique2 &&
	cat >expected_pmclique2 <<-EOF  &&
	0: clique=0
	1: clique=1
	2: clique=2
	3: clique=3
	EOF
	test_cmp expected_pmclique2 output_pmclique2
'

test_expect_success 'pmi: (put*1) / barrier / (get*1) pattern works' '
	run_program 10 ${SIZE} ${SIZE} ${KVSTEST} >output_kvstest &&
	grep -q "put phase" output_kvstest &&
	grep -q "get phase" output_kvstest
'

test_expect_success 'pmi: (put*1) / barrier / (get*size) pattern works' '
	run_program 30 ${SIZE} ${SIZE} ${KVSTEST} -n >output_kvstest2 &&
	grep -q "put phase" output_kvstest2 &&
	grep -q "get phase" output_kvstest2
'

test_expect_success 'pmi: (put*16) / barrier / (get*16) pattern works' '
	run_program 30 ${SIZE} ${SIZE} ${KVSTEST} -N 16 >output_kvstest3 &&
	grep -q "put phase" output_kvstest3 &&
	grep -q "get phase" output_kvstest3
'

test_expect_success 'pmi: (put*16) / barrier / (get*16*size) pattern works' '
	run_program 60 ${SIZE} ${SIZE} ${KVSTEST} -n -N 16 >output_kvstest4 &&
	grep -q "put phase" output_kvstest4 &&
	grep -q "get phase" output_kvstest4
'

test_done
