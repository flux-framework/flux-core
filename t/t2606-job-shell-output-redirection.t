#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
'

#
# 1 task output file tests
#

test_expect_success 'job-shell: run 1-task echo job (stdout file)' '
        flux mini run -n1 \
             --output=out0 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep stdout:foo out0
'

test_expect_success 'job-shell: run 1-task echo job (stderr file)' '
        flux mini run -n1 \
             --error=err1 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep stderr:bar err1
'

test_expect_success 'flux-shell: run 1-task echo job (stderr to stdout file)' '
        flux mini run -n1 \
             --output=out2 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep stderr:bar out2
'

test_expect_success 'job-shell: run 1-task echo job (stdout file/stderr file)' '
        flux mini run -n1 \
             --output=out3 --error=err3 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out3 &&
        grep stderr:baz err3
'

test_expect_success 'flux-shell: run 1-task echo job (stdout & stderr to stdout file)' '
        flux mini run -n1 \
             --output=out4 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out4 &&
        grep stderr:baz out4
'

test_expect_success 'job-shell: run 1-task echo job (stdout file/stderr kvs)' '
        id=$(flux mini submit -n1 \
             --output=out5 --setopt "output.stderr.type=\"kvs\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> err5 &&
        grep stdout:baz out5 &&
        grep stderr:baz err5
'

test_expect_success 'job-shell: run 1-task echo job (stdout kvs/stderr file)' '
        id=$(flux mini submit -n1 \
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
        flux mini run -n2 \
             --output=out7 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep "0: stdout:foo" out7 &&
        grep "1: stdout:foo" out7
'

test_expect_success 'job-shell: run 2-task echo job (stderr file)' '
        flux mini run -n2 \
             --error=err8 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "0: stderr:bar" err8 &&
        grep "1: stderr:bar" err8
'

test_expect_success 'job-shell: run 2-task echo job (stderr to stdout file)' '
        flux mini run -n2 \
             --output=out9 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "0: stderr:bar" out9 &&
        grep "1: stderr:bar" out9
'

test_expect_success 'job-shell: run 2-task echo job (stdout file/stderr file)' '
        flux mini run -n2 \
             --output=out10 --error=err10 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "0: stdout:baz" out10 &&
        grep "1: stdout:baz" out10 &&
        grep "0: stderr:baz" err10 &&
        grep "1: stderr:baz" err10
'

test_expect_success 'job-shell: run 2-task echo job (stdout & stderr to stdout file)' '
        flux mini run -n2 \
             --output=out11 --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "0: stdout:baz" out11 &&
        grep "1: stdout:baz" out11 &&
        grep "0: stderr:baz" out11 &&
        grep "1: stderr:baz" out11
'

test_expect_success 'job-shell: run 2-task echo job (stdout file/stderr kvs)' '
        id=$(flux mini submit -n2 \
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
        id=$(flux mini submit -n2 \
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
# mustache {{id}} template tests
#

test_expect_success 'job-shell: run 1-task echo job (mustache id stdout file/stderr file)' '
        id=$(flux mini submit -n1 \
             --output="out{{id}}" --error="err{{id}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id} &&
        grep stderr:baz err${id}
'

test_expect_success 'flux-shell: run 1-task echo job (mustache id stdout & stderr to stdout file)' '
        id=$(flux mini submit -n1 \
             --output="out{{id}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id} &&
        grep stderr:baz out${id}
'

#
# 1 task output mustache {{taskid}} tests
#

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stdout)' '
        flux mini run -n1 \
             --output="out16-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep stdout:foo out16-0
'

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stderr)' '
        flux mini run -n1 \
             --error="err17-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep stderr:bar err17-0
'

test_expect_success 'job-shell: run 1-task echo job (stderr to stdout {{taskid}})' '
        flux mini run -n1 \
             --output="out18-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "stderr:bar" out18-0
'

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stdout & stderr)' '
        flux mini run -n1 \
             --output="out19-{{taskid}}" --error="err19-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out19-0 &&
        grep stderr:baz err19-0
'

test_expect_success 'flux-shell: run 1-task echo job (stdout & stderr to stdout {{taskid}})' '
        flux mini run -n1 \
             --output="out20-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out20-0 &&
        grep stderr:baz out20-0
'

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stdout/stderr kvs)' '
        id=$(flux mini submit -n1 \
             --output="out21-{{taskid}}" --setopt "output.stderr.type=\"kvs\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> err21 &&
        grep stdout:baz out21-0 &&
        grep stderr:baz err21
'

test_expect_success 'flux-shell: run 1-task echo job (stdout kvs/{{taskid}} stderr)' '
        id=$(flux mini submit -n1 \
             --error="err22-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id > out22 &&
        grep stdout:baz out22 &&
        grep stderr:baz err22-0
'

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stdout/stderr file)' '
        flux mini run -n1 \
             --output="out23-{{taskid}}" --error="err23" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out23-0 &&
        grep stderr:baz err23
'

test_expect_success 'flux-shell: run 1-task echo job (stdout file/{{taskid}} stderr)' '
        flux mini run -n1 \
             --output="out24" --error="err24-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep stdout:baz out24 &&
        grep stderr:baz err24-0
'

#
# 2 task output mustache {{taskid}} tests
#

test_expect_success 'flux-shell: run 2-task echo job ({{taskid}} stdout)' '
        flux mini run -n2 \
             --output="out25-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo &&
        grep "stdout:foo" out25-0 &&
        grep "stdout:foo" out25-1
'

test_expect_success 'flux-shell: run 2-task echo job ({{taskid}} stderr)' '
        flux mini run -n2 \
             --error="err26-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "stderr:bar" err26-0 &&
        grep "stderr:bar" err26-1
'

test_expect_success 'job-shell: run 2-task echo job (stderr to stdout {{taskid}})' '
        flux mini run -n2 \
             --output="out27-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar &&
        grep "stderr:bar" out27-0 &&
        grep "stderr:bar" out27-1
'

test_expect_success 'flux-shell: run 2-task echo job ({{taskid}} stdout & stderr)' '
        flux mini run -n2 \
             --output="out28-{{taskid}}" --error="err28-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "stdout:baz" out28-0 &&
        grep "stdout:baz" out28-1 &&
        grep "stderr:baz" err28-0 &&
        grep "stderr:baz" err28-1
'

test_expect_success 'job-shell: run 2-task echo job (stdout & stderr to stdout {{taskid}})' '
        flux mini run -n2 \
             --output="out29-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "stdout:baz" out29-0 &&
        grep "stdout:baz" out29-1 &&
        grep "stderr:baz" out29-0 &&
        grep "stderr:baz" out29-1
'

test_expect_success 'flux-shell: run 2-task echo job ({{taskid}} stdout/stderr kvs)' '
        id=$(flux mini submit -n2 \
             --output="out30-{{taskid}}" --setopt "output.stderr.type=\"kvs\"" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -l $id 2> err30 &&
        grep "stdout:baz" out30-0 &&
        grep "stdout:baz" out30-1 &&
        grep "0: stderr:baz" err30 &&
        grep "1: stderr:baz" err30
'

test_expect_success 'flux-shell: run 2-task echo job (stdout kvs/{{taskid}} stderr)' '
        id=$(flux mini submit -n2 \
             --error="err31-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -l $id > out31 &&
        grep "0: stdout:baz" out31 &&
        grep "1: stdout:baz" out31 &&
        grep "stderr:baz" err31-0 &&
        grep "stderr:baz" err31-1
'

test_expect_success 'flux-shell: run 2-task echo job ({{taskid}} stdout/stderr file)' '
        flux mini run -n2 \
             --output="out32-{{taskid}}" --error="err32" --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "stdout:baz" out32-0 &&
        grep "stdout:baz" out32-1 &&
        grep "0: stderr:baz" err32 &&
        grep "1: stderr:baz" err32
'

test_expect_success 'flux-shell: run 2-task echo job (stdout file/{{taskid}} stderr)' '
        flux mini run -n2 \
             --output="out33" --label-io --error="err33-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz &&
        grep "0: stdout:baz" out33 &&
        grep "1: stdout:baz" out33 &&
        grep "stderr:baz" err33-0 &&
        grep "stderr:baz" err33-1
'

#
# test both {{id}} and {{taskid}}
#

test_expect_success 'flux-shell: run 1-task echo job ({{taskid}} stdout & stderr)' '
        id=$(flux mini submit -n1 \
             --output="out{{id}}-{{taskid}}" --error="err{{id}}-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id}-0 &&
        grep stderr:baz err${id}-0
'

test_expect_success 'flux-shell: run 1-task echo job (stdout & stderr to stdout {{taskid}})' '
        id=$(flux mini submit -n1 \
             --output="out{{id}}-{{taskid}}" \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        grep stdout:baz out${id}-0 &&
        grep stderr:baz out${id}-0
'

#
# output file outputs correct information to guest.output
#

test_expect_success 'job-shell: redirect events appear in guest.output (1-task)' '
        id=$(flux mini submit -n1 \
             --output=out36 --error=err36 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog36.out &&
        grep "stdout" eventlog36.out | grep redirect | grep "rank=\"0\"" | grep out36 &&
        grep "stderr" eventlog36.out | grep redirect | grep "rank=\"0\"" | grep err36
'

test_expect_success 'job-shell: redirect events appear in guest.output (1-task stderr to stdout)' '
        id=$(flux mini submit -n1 \
             --output=out37 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog37.out &&
        grep "stdout" eventlog37.out | grep redirect | grep "rank=\"0\"" | grep out37 &&
        grep "stderr" eventlog37.out | grep redirect | grep "rank=\"0\"" | grep out37
'

test_expect_success 'job-shell: redirect events appear in guest.output (2-task)' '
        id=$(flux mini submit -n2 \
             --output=out38 --error=err38 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog38.out &&
        grep "stdout" eventlog38.out | grep redirect | grep "0-1" | grep out38 &&
        grep "stderr" eventlog38.out | grep redirect | grep "0-1" | grep err38
'

test_expect_success 'job-shell: redirect events appear in guest.output (2-task stderr to stdout)' '
        id=$(flux mini submit -n2 \
             --output=out39 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job eventlog -p guest.output $id > eventlog39.out &&
        grep "stdout" eventlog39.out | grep redirect | grep "0-1" | grep out39 &&
        grep "stderr" eventlog39.out | grep redirect | grep "0-1" | grep out39
'

test_expect_success 'job-shell: attach shows redirected file (1-task)' '
        id=$(flux mini submit -n1 \
             --output=out40 --error=err40 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach40.err &&
        grep "0: stdout redirected to out40" attach40.err &&
        grep "0: stderr redirected to err40" attach40.err
'

test_expect_success 'job-shell: attach shows redirected file (1-task stderr to stdout)' '
        id=$(flux mini submit -n1 \
             --output=out41 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach41.err &&
        grep "0: stdout redirected to out41" attach41.err &&
        grep "0: stderr redirected to out41" attach41.err
'

test_expect_success 'job-shell: attach shows redirected file (2-task)' '
        id=$(flux mini submit -n2 \
             --output=out42 --error=err42 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach42.err &&
        grep "[[0-1]]: stdout redirected to out42" attach42.err &&
        grep "[[0-1]]: stderr redirected to err42" attach42.err
'

test_expect_success 'job-shell: attach shows redirected file (2-task stderr to stdout)' '
        id=$(flux mini submit -n2 \
             --output=out43 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach $id 2> attach43.err &&
        grep "[[0-1]]: stdout redirected to out43" attach43.err &&
        grep "[[0-1]]: stderr redirected to out43" attach43.err
'

test_expect_success 'job-shell: attach --quiet suppresses redirected file (1-task)' '
        id=$(flux mini submit -n1 \
             --output=out44 --error=err44 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q $id 2> attach44.err &&
        ! grep "redirected" attach44.err
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
        id=$(flux mini submit -n1 \
             --output=out45 --error=err45 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q ${id} > out45.attach 2> err45.attach &&
        grep stdout:baz out45 &&
        grep stderr:baz err45 &&
        ! test -s out45.attach &&
        sed -i -e "/stdin EOF could not be sent/d" err45.attach &&
        ! test -s err45.attach
'

test_expect_success 'job-shell: job attach exits cleanly if no kvs output (2-task)' '
        id=$(flux mini submit -n2 \
             --output=out46 --error=err46 \
             ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz) &&
        flux job wait-event $id clean &&
        flux job attach -q ${id} > out46.attach 2> err46.attach &&
        grep stdout:baz out46 &&
        grep stderr:baz err46 &&
        ! test -s out46.attach &&
        sed -i -e "/stdin EOF could not be sent/d" err46.attach &&
        ! test -s err46.attach
'

test_expect_success NO_CHAIN_LINT 'job-shell: job attach waits if no kvs output (1-task, live job)' '
        id=$(flux mini submit -n1 \
             --output=out47 --error=err47 \
             sleep 60)
        flux job wait-event $id start &&
        flux job attach -E -X ${id} 2> attach47.err &
        pid=$! &&
        flux job cancel $id &&
        ! wait $pid
'

test_expect_success NO_CHAIN_LINT 'job-shell: job attach waits if no kvs output (2-task, live job)' '
        id=$(flux mini submit -n2 \
             --output=out48 --error=err48 \
             sleep 60)
        flux job wait-event $id start &&
        flux job attach -E -X ${id} 2> attach48.err &
        pid=$! &&
        flux job cancel $id &&
        ! wait $pid
'

test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
