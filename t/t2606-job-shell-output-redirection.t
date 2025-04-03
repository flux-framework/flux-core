#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess

#
# 1 task output file tests
#

test_expect_success 'job-shell: run 1-task echo job (stdout file)' '
        flux run -n1 \
             --output=out0 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep stdout:foo out0
'

test_expect_success 'job-shell: run 1-task echo job (stderr file)' '
        flux run -n1 \
             --error=err1 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep stderr:bar err1
'

test_expect_success 'flux-shell: run 1-task echo job (stderr to stdout file)' '
        flux run -n1 \
             --output=out2 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep stderr:bar out2
'

test_expect_success 'job-shell: run 1-task echo job (stdout file/stderr file)' '
        flux run -n1 \
             --output=out3 --error=err3 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out3 &&
        grep stderr:baz err3
'

test_expect_success 'flux-shell: run 1-task echo job (stdout & stderr to stdout file)' '
        flux run -n1 \
             --output=out4 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out4 &&
        grep stderr:baz out4
'

test_expect_success 'job-shell: run 1-task echo job (stdout file/stderr kvs)' '
        id=$(flux submit -n1 \
             --output=out5 --setopt "output.stderr.type=\"kvs\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> err5 &&
        grep stdout:baz out5 &&
        grep stderr:baz err5
'

test_expect_success 'job-shell: run 1-task echo job (stdout kvs/stderr file)' '
        id=$(flux submit -n1 \
             --error=err6 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id > out6 &&
        grep stdout:baz out6 &&
        grep stderr:baz err6
'

#
# 2 task output file tests
#

test_expect_success 'job-shell: run 2-task echo job (stdout file)' '
        flux run -n2 \
             --output=out7 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep "0: stdout:foo" out7 &&
        grep "1: stdout:foo" out7
'

test_expect_success 'job-shell: run 2-task echo job (stderr file)' '
        flux run -n2 \
             --error=err8 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "0: stderr:bar" err8 &&
        grep "1: stderr:bar" err8
'

test_expect_success 'job-shell: run 2-task echo job (stderr to stdout file)' '
        flux run -n2 \
             --output=out9 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "0: stderr:bar" out9 &&
        grep "1: stderr:bar" out9
'

test_expect_success 'job-shell: run 2-task echo job (stdout file/stderr file)' '
        flux run -n2 \
             --output=out10 --error=err10 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "0: stdout:baz" out10 &&
        grep "1: stdout:baz" out10 &&
        grep "0: stderr:baz" err10 &&
        grep "1: stderr:baz" err10
'

test_expect_success 'job-shell: run 2-task echo job (stdout & stderr to stdout file)' '
        flux run -n2 \
             --output=out11 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "0: stdout:baz" out11 &&
        grep "1: stdout:baz" out11 &&
        grep "0: stderr:baz" out11 &&
        grep "1: stderr:baz" out11
'

test_expect_success 'job-shell: run 2-task echo job (stdout file/stderr kvs)' '
        id=$(flux submit -n2 \
             --output=out12 --label-io --setopt "output.stderr.type=\"kvs\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -l $id 2> err12 &&
        grep "0: stdout:baz" out12 &&
        grep "1: stdout:baz" out12 &&
        grep "0: stderr:baz" err12 &&
        grep "1: stderr:baz" err12
'

test_expect_success 'job-shell: run 2-task echo job (stdout kvs/stderr file)' '
        id=$(flux submit -n2 \
             --error=err13 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -l $id > out13 &&
        grep "0: stdout:baz" out13 &&
        grep "1: stdout:baz" out13 &&
        grep "0: stderr:baz" err13 &&
        grep "1: stderr:baz" err13
'

#
# output file mustache tests
#

test_expect_success 'job-shell: run 1-task echo job (mustache id stdout file/stderr file)' '
        id=$(flux submit -n1 \
             --output="out{{id}}" --error="err{{id}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id} &&
        grep stderr:baz err${id}
'

test_expect_success 'flux-shell: run 1-task echo job (mustache id stdout & stderr to stdout file)' '
        id=$(flux submit -n1 \
             --output="out{{id}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id} &&
        grep stderr:baz out${id}
'
test_expect_success 'flux-shell: run 1-task echo job (mustache job name)' '
	id=$(flux submit --wait -n1 --job-name=thisname \
	     --output="out.{{name}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
	grep stdout:baz out.thisname &&
	grep stderr:baz out.thisname
'
test_expect_success 'flux-shell: run 1-task echo job (mustache job name)' '
	id=$(flux submit --wait -n1 \
	     --output="out.{{name}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
	grep stdout:baz out.test_echo &&
	grep stderr:baz out.test_echo
'

test_expect_success 'flux-shell: bad mustache template still writes output' '
        id=$(flux submit -n1 \
             --output="out{{invalid" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out{{invalid &&
        grep stderr:baz out{{invalid
'

#
# output file outputs correct information to guest.output
#

test_expect_success 'job-shell: redirect events appear in guest.output (1-task)' '
        id=$(flux submit -n1 \
             --output=out16 --error=err16 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog16.out &&
        grep "stdout" eventlog16.out | grep redirect | grep "rank=\"0\"" | grep out16 &&
        grep "stderr" eventlog16.out | grep redirect | grep "rank=\"0\"" | grep err16
'

test_expect_success 'job-shell: redirect events appear in guest.output (1-task stderr to stdout)' '
        id=$(flux submit -n1 \
             --output=out17 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog17.out &&
        grep "stdout" eventlog17.out | grep redirect | grep "rank=\"0\"" | grep out17 &&
        grep "stderr" eventlog17.out | grep redirect | grep "rank=\"0\"" | grep out17
'

test_expect_success 'job-shell: redirect events appear in guest.output (2-task)' '
        id=$(flux submit -n2 \
             --output=out18 --error=err18 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog18.out &&
        grep "stdout" eventlog18.out | grep redirect | grep "0-1" | grep out18 &&
        grep "stderr" eventlog18.out | grep redirect | grep "0-1" | grep err18
'

test_expect_success 'job-shell: redirect events appear in guest.output (2-task stderr to stdout)' '
        id=$(flux submit -n2 \
             --output=out19 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog19.out &&
        grep "stdout" eventlog19.out | grep redirect | grep "0-1" | grep out19 &&
        grep "stderr" eventlog19.out | grep redirect | grep "0-1" | grep out19
'

test_expect_success 'job-shell: attach shows redirected file (1-task)' '
        id=$(flux submit -n1 \
             --output=out20 --error=err20 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach20.err &&
        grep "0: stdout redirected to out20" attach20.err &&
        grep "0: stderr redirected to err20" attach20.err
'

test_expect_success 'job-shell: attach shows redirected file (1-task stderr to stdout)' '
        id=$(flux submit -n1 \
             --output=out21 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach21.err &&
        grep "0: stdout redirected to out21" attach21.err &&
        grep "0: stderr redirected to out21" attach21.err
'

test_expect_success 'job-shell: attach shows redirected file (2-task)' '
        id=$(flux submit -n2 \
             --output=out22 --error=err22 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach22.err &&
        grep "[[0-1]]: stdout redirected to out22" attach22.err &&
        grep "[[0-1]]: stderr redirected to err22" attach22.err
'

test_expect_success 'job-shell: attach shows redirected file (2-task stderr to stdout)' '
        id=$(flux submit -n2 \
             --output=out23 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach23.err &&
        grep "[[0-1]]: stdout redirected to out23" attach23.err &&
        grep "[[0-1]]: stderr redirected to out23" attach23.err
'

test_expect_success 'job-shell: attach --quiet suppresses redirected file (1-task)' '
        id=$(flux submit -n1 \
             --output=out24 --error=err24 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q $id 2> attach24.err &&
        ! grep "redirected" attach24.err
'

#
# output corner case tests
#

#
# sharness will redirect /dev/null to stdin by default, leading to the
# possibility of seeing an EOF warning on stdin.  We'll check for that
# manually in the next two tests and filter it out from the stderr
# output.
#

test_expect_success 'job-shell: job attach exits cleanly if no kvs output (1-task)' '
        id=$(flux submit -n1 \
             --output=out25 --error=err25 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q ${id} > out25.attach 2> err25.attach &&
        grep stdout:baz out25 &&
        grep stderr:baz err25 &&
        ! test -s out25.attach &&
        sed -i -e "/stdin EOF could not be sent/d" err25.attach &&
        ! test -s err25.attach
'

test_expect_success 'job-shell: job attach exits cleanly if no kvs output (2-task)' '
        id=$(flux submit -n2 \
             --output=out26 --error=err26 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q ${id} > out26.attach 2> err26.attach &&
        grep stdout:baz out26 &&
        grep stderr:baz err26 &&
        ! test -s out26.attach &&
        sed -i -e "/stdin EOF could not be sent/d" err26.attach &&
        sed -i -e "/affinity/d" err26.attach &&
        ! test -s err26.attach
'

test_expect_success NO_CHAIN_LINT 'job-shell: job attach waits if no kvs output (1-task, live job)' '
        id=$(flux submit -n1 \
             --output=out27 --error=err27 \
             sleep 60)
        flux job wait-event $id start
        flux job attach -E -X ${id} 2> attach27.err &
        pid=$! &&
        flux cancel $id &&
        ! wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-shell: job attach waits if no kvs output (2-task, live job)' '
        id=$(flux submit -n2 \
             --output=out28 --error=err28 \
             sleep 60)
        flux job wait-event $id start
        flux job attach -E -X ${id} 2> attach28.err &
        pid=$! &&
        flux cancel $id &&
        ! wait $pid
'
test_expect_success 'job-shell: shell errors are captured in output file' '
	test_expect_code 127 flux run --output=test.out nosuchcommand &&
	grep "nosuchcommand: No such file or directory" test.out
'
test_expect_success 'job-shell: shell errors are captured in error file' '
	test_expect_code 127  flux run --error=test.err nosuchcommand &&
	grep "nosuchcommand: No such file or directory" test.err
'
test_expect_success 'job-shell: kvs output truncation works' '
	flux run -o output.limit=5 echo 0123456789 2>trunc.err &&
	test_debug "cat trunc.err" &&
	grep "stdout.*truncated" trunc.err
'
test_expect_success 'job-shell: job shell only logs once about truncation' '
	flux run -N4 -o output.limit=20 echo 0123456789 2>trunc4.err &&
	test_debug "cat trunc4.err"  &&
	test $(grep -c "bytes truncated" trunc4.err) -eq 1
'
test_expect_success 'job-shell: stderr truncation works' '
	flux run -o output.limit=5 \
		sh -c "echo 0123456789 >&2" >trunc2.error 2>&1 &&
	grep "stderr.*truncated" trunc2.error
'
test_expect_success LONGTEST 'job-shell: no truncation at 10MB for single-user job' '
	dd if=/dev/urandom bs=10240 count=800 | base64 --wrap 79 >10M+ &&
	flux run cat 10M+ >10M+.output &&
	test_cmp 10M+ 10M+.output
'
test_expect_success 'job-shell: invalid output.limit string is rejected (text)' '
	test_must_fail flux run -o output.limit=foo hostname
'
test_expect_success 'job-shell: invalid output.limit string is rejected (zero)' '
	test_must_fail flux run -o output.limit=0 hostname
'
test_expect_success 'job-shell: invalid output.limit string is rejected (big num)' '
	test_must_fail flux run -o output.limit=4000000000 hostname
'
test_expect_success 'job-shell: invalid output.limit string is rejected (big num suffix)' '
	test_must_fail flux run -o output.limit=4G hostname
'
test_expect_success 'job-shell: output.mode=append works' '
	flux bulksubmit --watch --output=append.out \
		-o output.mode=append echo {} \
		::: one two three &&
	test_debug "cat append.out" &&
	test $(wc -l < append.out) -eq 3 &&
	grep one append.out &&
	grep two append.out &&
	grep three append.out
'
test_expect_success 'job-shell: output.mode=truncate works' '
	cat <<-EOF >trunc.out &&
	test text
	EOF
	flux run --output=trunc.out hostname &&
	test $(wc -l <trunc.out) -eq 1 &&
	test_must_fail grep "test text" trunc.out
'
test_expect_success 'job-shell: invalid output.mode emits warning' '
	flux run --output=inval.out -o output.mode=foo hostname 2>inval.err &&
	test_debug "cat inval.out" &&
	grep "ignoring invalid output.mode=foo" inval.err
'
test_expect_success 'job-shell: per-node output works' '
	flux run -N4 -n4 --output=per-node.{{node.id}} \
		sh -c "flux getattr rank" &&
	ls -l per-node.* &&
	for rank in $(seq 0 3); do test $(cat per-node.${rank}) -eq $rank; done
'
test_expect_success 'job-shell: per-task output works' '
	flux run -N4 -n8 --output=per-task.{{task.id}} \
		printenv FLUX_TASK_RANK &&
	ls -l per-task.* &&
	for rank in $(seq 0 7); do test $(cat per-task.${rank}) -eq $rank; done
'
test_expect_success 'job-shell: log messages are sent to correct output files' '
	flux run -N4 -o verbose=2 --output=per-log.{{node.id}} hostname &&
	grep flux-shell per-log.0 &&
	grep flux-shell per-log.1 &&
	grep flux-shell per-log.2 &&
	grep flux-shell per-log.3
'
test_expect_success 'job-shell: pty output is saved in correct output files' '
	flux run -N4 -n8 -o pty -o cpu-affinity=off \
		--output=pty-output.{{task.id}} echo foo &&
	grep . pty-output.* &&
	for rank in $(seq 0 7); do grep foo pty-output.${rank}; done
'
test_expect_success 'job-shell: open of invalid output files fails' '
	test_must_fail flux run -N4 -n8 \
		--output=/nosuchdir/output.{{task.id}} hostname
'
test_done
