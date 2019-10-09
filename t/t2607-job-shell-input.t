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
       echo "foo" > input-0 &&
       echo "bar" > input-1 &&
       ${LPTEST} 79 10000 > lptestXXL_input &&
       ${LPTEST} 79 10000 > lptestXXL-0
'

#
# pipe in stdin tests
#

test_expect_success 'flux-shell: run 1-task no pipe in stdin' '
        id=$(flux mini submit -n1 echo foo) &&
        flux job attach $id > pipe0.out &&
        grep foo pipe0.out
'

test_expect_success 'flux-shell: run 1-task input file as stdin job' '
        id=$(flux mini submit -n1 \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n) &&
        flux job attach $id < input_stdin_file > pipe1.out &&
        test_cmp input_stdin_file pipe1.out
'

test_expect_success 'flux-shell: run 2-task input file as stdin job' '
        id=$(flux mini submit -n2 \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n) &&
        flux job attach -l $id < input_stdin_file > pipe2.out &&
        grep "0: foo" pipe2.out &&
        grep "0: doh" pipe2.out &&
        grep "1: foo" pipe2.out &&
        grep "1: doh" pipe2.out
'

test_expect_success LONGTEST 'flux-shell: 10K line lptest piped input works' '
        id=$(flux mini submit -n1 \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n) &&
        flux job attach $id < lptestXXL_input > pipe3.out &&
	test_cmp lptestXXL_input pipe3.out
'

#
# sharness will redirect /dev/null to stdin by default, so we create a named pipe
# and pipe that in for tests in which we need "no stdin".
#

test_expect_success NO_CHAIN_LINT 'flux-shell: attach twice, one with data' '
        mkfifo stdin4.pipe
        id=$(flux mini submit -n1 \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        flux job attach $id < stdin4.pipe > pipe4A.out 2> pipe4A.err &
        pid1=$!
        flux job attach $id < input_stdin_file > pipe4B.out 2> pipe4B.err &
        pid2=$!
        exec 9> stdin4.pipe &&
        wait $pid1 &&
        wait $pid2 &&
        exec 9>&- &&
        test_cmp input_stdin_file pipe4A.out &&
        test_cmp input_stdin_file pipe4B.out
'

test_expect_success NO_CHAIN_LINT 'flux-shell: multiple jobs, each want stdin' '
        id1=$(flux mini submit -n1 \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id2=$(flux mini submit -n1 \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id3=$(flux mini submit -n1 \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id4=$(flux mini submit -n1 \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        flux job attach $id1 < input_stdin_file > pipe5_1.out 2> pipe5_1.err &
        pid1=$!
        flux job attach $id2 < input_stdin_file > pipe5_2.out 2> pipe5_2.err &
        pid2=$!
        flux job attach $id3 < input_stdin_file > pipe5_3.out 2> pipe5_3.err &
        pid3=$!
        flux job attach $id4 < input_stdin_file > pipe5_4.out 2> pipe5_4.err &
        pid4=$!
        wait $pid1 &&
        wait $pid2 &&
        wait $pid3 &&
        wait $pid4 &&
        test_cmp input_stdin_file pipe5_1.out &&
        test_cmp input_stdin_file pipe5_2.out &&
        test_cmp input_stdin_file pipe5_3.out &&
        test_cmp input_stdin_file pipe5_4.out
'

test_expect_success NO_CHAIN_LINT 'flux-shell: no stdin desired in job' '
        id=$(flux mini submit -n1 sleep 60)
        flux job attach $id < input_stdin_file 2> pipe6A.err &
        pid=$! &&
        flux job wait-event -p guest.exec.eventlog $id input-ready 2> pipe6B.err &&
        flux job wait-event -p guest.input -m eof=true $id data 2> pipe6C.err &&
        flux job cancel $id 2> pipe6D.err &&
        test_expect_code 143 wait $pid
'

# There is a small racy possibility that `flux job attach` could
# notice the job is completed BEFORE it is informed the shell is not
# receiving stdin, thus the `flux job attach` call does not fail.
#
# To greatly decrease the odds of that happening, make sure the test
# job outputs a very nice chunk of data.
test_expect_success 'flux-shell: task completed, try to pipe into stdin' '
        ${LPTEST} 79 500 > big_dataset &&
        id=$(flux mini submit -n1 cat big_dataset) &&
        flux job wait-event $id clean 2> pipe7A.err &&
        test_must_fail flux job attach $id < input_stdin_file 2> pipe7B.err
'

test_expect_success NO_CHAIN_LINT 'flux-shell: pipe to stdin twice, second fails' '
        id=$(flux mini submit -n1 sleep 60)
        flux job attach $id < input_stdin_file 2> pipe8A.err &
        pid=$!
        flux job wait-event -p guest.exec.eventlog $id input-ready 2> pipe8B.err &&
        flux job wait-event -p guest.input -m eof=true $id data 2> pipe8C.err &&
        test_must_fail flux job attach $id < input_stdin_file 2> pipe8D.err &&
        flux job cancel $id 2> pipe8E.err &&
        test_expect_code 143 wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux-shell: pipe in zero data' '
        flux mini run -n1 echo < /dev/null &&
        cat /dev/null | flux mini run -n1 cat
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
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n
'

test_expect_success 'flux-shell: task stdin via file, try to pipe into stdin fails' '
        id=$(flux mini submit -n1 --input=input_stdin_file sleep 60) &&
        flux job wait-event $id start &&
        test_must_fail flux job attach $id < input_stdin_file &&
        flux job cancel $id
'

#
# input file per-task tests
#

test_expect_success 'flux-shell: run 1-task per-task input file' '
        flux mini run -n1 --input="input-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > pertask0.out &&
        test_cmp input-0 pertask0.out
'

test_expect_success 'flux-shell: run 2-task per-task input file' '
        flux mini run -n2 --input="input-{{taskid}}" --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > pertask1.out &&
        grep "0: foo" pertask1.out &&
        grep "1: bar" pertask1.out
'

test_expect_success NO_CHAIN_LINT 'flux-shell: multiple jobs, each want stdin via file' '
        id1=$(flux mini submit -n1 --input="input-{{taskid}}" \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id2=$(flux mini submit -n1 --input="input-{{taskid}}" \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id3=$(flux mini submit -n1 --input="input-{{taskid}}" \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        id4=$(flux mini submit -n1 --input="input-{{taskid}}" \
              ${TEST_SUBPROCESS_DIR}/test_echo -O -n)
        flux job attach $id1 > pertask2_1.out 2> pertask2_1.err &
        pid1=$!
        flux job attach $id2 > pertask2_2.out 2> pertask2_2.err &
        pid2=$!
        flux job attach $id3 > pertask2_3.out 2> pertask2_3.err &
        pid3=$!
        flux job attach $id4 > pertask2_4.out 2> pertask2_4.err &
        pid4=$!
        wait $pid1 &&
        wait $pid2 &&
        wait $pid3 &&
        wait $pid4 &&
        test_cmp input-0 pertask2_1.out &&
        test_cmp input-0 pertask2_2.out &&
        test_cmp input-0 pertask2_3.out &&
        test_cmp input-0 pertask2_4.out
'

test_expect_success LONGTEST 'flux-shell: 10K line lptest input works' '
        flux mini run -n1 --input=lptestXXL-0 \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > pertask3.out &&
	test_cmp lptestXXL_input pertask3.out
'

test_expect_success 'flux-shell: per-task input file invalid' '
        test_must_fail flux mini run -n1 --input="foobar-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > pertask4.out
'

test_expect_success 'flux-shell: task stdin via per-task file, try to pipe into stdin fails' '
        id=$(flux mini submit -n1 --input="input-{{taskid}}" sleep 60) &&
        flux job wait-event $id start &&
        test_must_fail flux job attach $id < input_stdin_file &&
        flux job cancel $id
'

#
# corner case tests
#

test_expect_success 'flux-shell: run 1-task input file with service type' '
        id=$(flux mini submit -n1 \
             --setopt "input.stdin.type=\"service\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n) &&
        flux job attach $id < input_stdin_file > cc1.out &&
        test_cmp input_stdin_file cc1.out
'

test_expect_success 'flux-shell: error on bad input type' '
        id=$(flux mini submit -n1 \
             --setopt "input.stdin.type=\"foobar\"" \
             echo foo) &&
        flux job wait-event $id clean &&
        test_must_fail flux job attach $id
'

test_expect_success 'flux-shell: error on no path with file output' '
        id=$(flux mini submit -n1 \
             --label-io --setopt "input.stdin.type=\"file\"" \
             echo foo) &&
        flux job wait-event $id clean &&
        test_must_fail flux job attach $id
'

test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
