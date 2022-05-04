#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess
LPTEST=${SHARNESS_TEST_DIRECTORY}/shell/lptest

test_expect_success 'flux-shell: generate input for stdin input tests' '
       echo "foo" > input_stdin_file &&
       echo "doh" >> input_stdin_file &&
       ${LPTEST} 79 10000 > lptestXXL_input
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

test_expect_success 'flux-shell: multiple jobs, each want stdin' '
	flux mini submit --cc=1-4 -n1 -t 30 \
	    ${TEST_SUBPROCESS_DIR}/test_echo -O -n >pipe5.jobids &&
	test_debug "cat pipe5.jobids" &&
	i=1 &&
	for id in $(cat pipe5.jobids); do
	    flux job attach $id \
	        <input_stdin_file >pipe5_${i}.out 2>pipe5_${i}.err &&
            test_cmp input_stdin_file pipe5_${i}.out &&
	    i=$((i+1))
	done
'

test_expect_success NO_CHAIN_LINT 'flux-shell: no stdin desired in job' '
        id=$(flux mini submit -n1 sleep 60)
        flux job attach $id < input_stdin_file 2> pipe6A.err &
        pid=$! &&
        flux job wait-event -p guest.exec.eventlog $id shell.init 2> pipe6B.err &&
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

test_expect_success 'flux-shell: task completed, try to pipe into stdin, no error if read only' '
        ${LPTEST} 79 500 > big_dataset &&
        id=$(flux mini submit -n1 cat big_dataset) &&
        flux job wait-event $id clean 2> pipe8A.err &&
        flux job attach --read-only $id < input_stdin_file 2> pipe8B.err
'

test_expect_success NO_CHAIN_LINT 'flux-shell: pipe to stdin twice, second fails' '
        id=$(flux mini submit -n1 sleep 60)
        flux job attach $id < input_stdin_file 2> pipe9A.err &
        pid=$!
        flux job wait-event -p guest.exec.eventlog $id shell.init 2> pipe9B.err &&
        flux job wait-event -p guest.input -m eof=true $id data 2> pipe9C.err &&
        test_must_fail flux job attach $id < input_stdin_file 2> pipe9D.err &&
        flux job cancel $id 2> pipe9E.err &&
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

test_expect_success 'flux-shell: multiple jobs, each want stdin via file' '
	flux mini submit --cc=1-4 -n1 -t 30 --input=input_stdin_file \
	    ${TEST_SUBPROCESS_DIR}/test_echo -O -n >file2.jobids &&
	test_debug "cat file2.jobids" &&
	i=1 &&
	for id in $(cat file2.jobids); do
	    flux job attach $id >file2_${i}.out 2>file2_${i}.err &&
            test_cmp input_stdin_file file2_${i}.out &&
	    i=$((i+1))
	done
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

test_expect_success 'flux-shell: no fatal exception after stdin sent to exited task' '
	name="rankzeroexit" &&
	cat <<-EOF >${name}.sh &&
	#!/bin/sh
	unset FLUX_KVS_NAMESPACE
	flux kvs put ${name}.\${FLUX_TASK_RANK}.started=1
	if test \$FLUX_TASK_RANK -ne 0; then cat; fi
	EOF
	chmod +x ${name}.sh &&
	id=$(flux mini submit -n2 -o verbose ./${name}.sh) &&
	flux job wait-event -v -p guest.exec.eventlog ${id} shell.start &&
	flux kvs get --watch --waitcreate --count=1 ${name}.0.started &&
	flux kvs get --watch --waitcreate --count=1 ${name}.1.started &&
	echo | flux job attach -XE ${id} &&
	flux job wait-event -t 5 -v ${id} clean
'
test_done
