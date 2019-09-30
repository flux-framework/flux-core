#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess
LPTEST=${SHARNESS_TEST_DIRECTORY}/shell/lptest

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
'

test_expect_success 'flux-shell: generate input for stdin input tests' '
       echo "foo" > input_stdin_file &&
       echo "doh" >> input_stdin_file &&
       ${LPTEST} 79 10000 > lptestXXL_input
'

#
# input file tests
#

test_expect_success 'flux-shell: run 1-task input file as stdin job' '
        flux mini run -n1 --input=input_stdin_file \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file0.out &&
        test_cmp input_stdin_file file0.out
'

test_expect_success 'flux-shell: run 2-task input file as stdin job' '
        flux mini run -n2 --input=input_stdin_file --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file1.out &&
        grep "0: foo" file1.out &&
        grep "0: doh" file1.out &&
        grep "1: foo" file1.out &&
        grep "1: doh" file1.out
'

test_expect_success NO_CHAIN_LINT 'flux-shell: multiple jobs, each want stdin via file' '
        id1=$(flux mini submit -n1 --input=input_stdin_file \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id2=$(flux mini submit -n1 --input=input_stdin_file \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id3=$(flux mini submit -n1 --input=input_stdin_file \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id4=$(flux mini submit -n1 --input=input_stdin_file \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        flux job attach $id1 > file2_1.out 2> file2_1.err &
        pid1=$!
        flux job attach $id2 > file2_2.out 2> file2_2.err &
        pid2=$!
        flux job attach $id3 > file2_3.out 2> file2_3.err &
        pid3=$!
        flux job attach $id4 > file2_4.out 2> file2_4.err &
        pid4=$!
        wait $pid1 &&
        wait $pid2 &&
        wait $pid3 &&
        wait $pid4 &&
        test_cmp input_stdin_file file2_1.out &&
        test_cmp input_stdin_file file2_2.out &&
        test_cmp input_stdin_file file2_3.out &&
        test_cmp input_stdin_file file2_4.out
'

test_expect_success LONGTEST 'flux-shell: 10K line lptest input works' '
        flux mini run -n1 --input=lptestXXL_input \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file3.out &&
	test_cmp lptestXXL_input file3.out
'

test_expect_success 'flux-shell: input file invalid' '
        test_must_fail flux mini run -n1 --input=/foo/bar/baz \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > file4.out
'

test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
