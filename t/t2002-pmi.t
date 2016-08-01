#!/bin/sh
#

test_description='Test that PMI works in a Flux-launched program 

Test that PMI works in a FLux-launched program 
'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} wreck
echo "# $0: flux session size will be ${SIZE}"
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest

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

test_expect_success 'pmi: wreck preputs PMI_process_mapping into kvs' '
	cat <<-EOF >print-pmi-map.sh &&
	#!/bin/sh
        if test \${FLUX_TASK_RANK} -eq 0; then
	  flux kvs get lwj.\${FLUX_JOB_ID}.pmi.PMI_process_mapping
        fi
	EOF
	chmod +x print-pmi-map.sh &&
	run_timeout 5 flux wreckrun -l -N4 -n4 ./print-pmi-map.sh >output_map &&
	cat >expected_map <<-EOF &&
	0: (vector,(0,4,1))
	EOF
	test_cmp expected_map output_map
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
