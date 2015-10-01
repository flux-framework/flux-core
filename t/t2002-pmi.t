#!/bin/sh
#

test_description='Test that PMI works in a Flux-launched program 

Test that PMI works in a FLux-launched program 
'

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(($(nproc)+1))
test ${SIZE} -gt 4 || SIZE=4
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

# Usage: run_program timeout ntasks nnodes
run_program() {
	local timeout=$1
	local ntasks=$2
	local nnodes=$3
	shift 3
	export PMI_TRACE=0x38
	run_timeout $timeout flux wreckrun -l -o stdio-delay-commit \
		    -n${ntasks} -N${nnodes} $*
}

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

# FIXME: this test hardwires the expectations that job ID's are
# assigned in a particular sequence.  We don't really care about that,
# just that it's set to an integer that be returned by PMI_Get_appnum().
test_expect_success 'pmi: wreck sets FLUX_JOB_ID' '
	run_program 5 ${SIZE} ${SIZE} printenv FLUX_JOB_ID >output_appnum &&
        test `wc -l < output_appnum` = ${SIZE} &&
	test `cut -d: -f2 output_appnum | uniq` -eq 3
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

test_expect_success 'pmi: (put*1) / barrier / (get*1) pattern works' '
	run_program 10 ${SIZE} ${SIZE} \
	    ${FLUX_BUILD_DIR}/t/pmi/kvstest >output_kvstest &&
	grep -q "put phase" output_kvstest &&
	grep -q "get phase" output_kvstest &&
	test `grep PMI_KVS_Put output_kvstest | wc -l` -eq ${SIZE} &&
	test `grep PMI_Barrier output_kvstest | wc -l` -eq $((${SIZE}*2)) &&
	test `grep PMI_KVS_Get output_kvstest | wc -l` -eq ${SIZE} 
'

test_expect_success 'pmi: (put*1) / barrier / (get*size) pattern works' '
	run_program 30 ${SIZE} ${SIZE} \
	    ${FLUX_BUILD_DIR}/t/pmi/kvstest -n >output_kvstest2 &&
	grep -q "put phase" output_kvstest2 &&
	grep -q "get phase" output_kvstest2 &&
	test `grep PMI_KVS_Put output_kvstest2 | wc -l` -eq ${SIZE} &&
	test `grep PMI_Barrier output_kvstest2 | wc -l` -eq $((${SIZE}*2)) &&
	test `grep PMI_KVS_Get output_kvstest2 | wc -l` -eq $((${SIZE}*${SIZE}))
'

test_expect_success 'pmi: (put*16) / barrier / (get*16) pattern works' '
	run_program 30 ${SIZE} ${SIZE} \
	    ${FLUX_BUILD_DIR}/t/pmi/kvstest -N 16 >output_kvstest3 &&
	grep -q "put phase" output_kvstest3 &&
	grep -q "get phase" output_kvstest3 &&
	test `grep PMI_KVS_Put output_kvstest3 | wc -l` -eq $((${SIZE}*16)) &&
	test `grep PMI_Barrier output_kvstest3 | wc -l` -eq $((${SIZE}*2)) &&
	test `grep PMI_KVS_Get output_kvstest3 | wc -l` -eq $((${SIZE}*16))
'

test_expect_success 'pmi: (put*16) / barrier / (get*16*size) pattern works' '
	run_program 60 ${SIZE} ${SIZE} \
	    ${FLUX_BUILD_DIR}/t/pmi/kvstest -n -N 16 >output_kvstest4 &&
	grep -q "put phase" output_kvstest4 &&
	grep -q "get phase" output_kvstest4 &&
	test `grep PMI_KVS_Put output_kvstest4 | wc -l` -eq $((${SIZE}*16)) &&
	test `grep PMI_Barrier output_kvstest4 | wc -l` -eq $((${SIZE}*2))  &&
	test `grep PMI_KVS_Get output_kvstest4 | wc -l` -eq $((${SIZE}*16*${SIZE}))
'

test_done
