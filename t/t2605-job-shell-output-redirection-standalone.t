#!/bin/sh
#
test_description='Test flux-shell in --standalone mode'

. `dirname $0`/sharness.sh

jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ

#  Run flux-shell under flux command to get correct paths
FLUX_SHELL="flux ${FLUX_BUILD_DIR}/src/shell/flux-shell"

unset FLUX_URI

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess

test_expect_success 'flux-shell: generate 1-task echo jobspecs and matching R' '
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j1echostdout &&
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j1echostderr &&
	flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j1echoboth &&
	cat >R1 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0" }, "rank": "0" }
        ]}}
	EOT
'

test_expect_success 'flux-shell: generate 2-task echo jobspecs and matching R' '
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j2echostdout &&
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j2echostderr &&
	flux jobspec srun -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j2echoboth &&
	cat >R2 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0-1" }, "rank": "0" }
        ]}}
	EOT
'

#
# 1 task output file tests
#

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout file)' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out0\"" \
            > j1echostdout-0 &&
        ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-0 -R R1 0 &&
	grep stdout:foo out0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stderr file)' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err1\"" \
            > j1echostderr-1 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-1 -R R1 1 &&
	grep stderr:bar err1
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stderr to stdout file)' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out2\"" \
            > j1echostderr-2 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-2 -R R1 2 &&
	grep stderr:bar out2
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout file/stderr file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out3\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err3\"" \
            > j1echoboth-3 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-3 -R R1 3 &&
	grep stdout:baz out3 &&
	grep stderr:baz err3
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout & stderr to stdout file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out4\"" \
            > j1echoboth-4 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-4 -R R1 4 &&
	grep stdout:baz out4 &&
	grep stderr:baz out4
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout file/stderr term)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out5\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"term\"" \
            > j1echoboth-5 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-5 -R R1 2> err5 5 &&
	grep stdout:baz out5 &&
	grep stderr:baz err5
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout term/stderr file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err6\"" \
            > j1echoboth-6 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-6 -R R1 > out6 6 &&
	grep stdout:baz out6 &&
	grep stderr:baz err6
'

#
# 2 task output file tests
#

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout file)' '
        cat j2echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out7\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            > j2echostdout-7 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostdout-7 -R R2 7 &&
	grep "0: stdout:foo" out7 &&
	grep "1: stdout:foo" out7
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stderr file)' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err8\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echostderr-8 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-8 -R R2 8 &&
	grep "0: stderr:bar" err8 &&
	grep "1: stderr:bar" err8
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stderr to stdout file)' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out9\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            > j2echostderr-9 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-9 -R R2 9 &&
	grep "0: stderr:bar" out9 &&
	grep "1: stderr:bar" out9
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout file/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out10\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err10\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-10 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-10 -R R2 10 &&
	grep "0: stdout:baz" out10 &&
	grep "1: stdout:baz" out10 &&
	grep "0: stderr:baz" err10 &&
	grep "1: stderr:baz" err10
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout & stderr to stdout file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out11\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            > j2echoboth-11 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-11 -R R2 11 &&
	grep "0: stdout:baz" out11 &&
	grep "1: stdout:baz" out11 &&
	grep "0: stderr:baz" out11 &&
	grep "1: stderr:baz" out11
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout file/stderr term)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out12\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"term\"" \
            > j2echoboth-12 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-12 -R R2 2> err12 12 &&
	grep "0: stdout:baz" out12 &&
	grep "1: stdout:baz" out12 &&
	grep "0: stderr:baz" err12 &&
	grep "1: stderr:baz" err12
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout term/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err13\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-13 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-13 -R R2 > out13 13 &&
	grep "0: stdout:baz" out13 &&
	grep "1: stdout:baz" out13 &&
	grep "0: stderr:baz" err13 &&
	grep "1: stderr:baz" err13
'

#
# mustache {{id}} template tests
#

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id stdout file/stderr file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err{{id}}\"" \
            > j1echoboth-14 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-14 -R R1 14 &&
	grep stdout:baz out14 &&
	grep stderr:baz err14
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id stdout & stderr to stdout file)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id}}\"" \
            > j1echoboth-15 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-15 -R R1 15 &&
	grep stdout:baz out15 &&
	grep stderr:baz out15
'

#
# 1 task output mustache {{taskid}} tests
#

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job ({{taskid}} stdout)' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out16-{{taskid}}\"" \
            > j1echostdout-16 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostdout-16 -R R1 16 &&
	grep stdout:foo out16-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job ({{taskid}} stderr)' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err17-{{taskid}}\"" \
            > j1echostderr-17 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-17 -R R1 17 &&
	grep stderr:bar err17-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stderr to stdout {{taskid}})' '
        cat j1echostderr \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out18-{{taskid}}\"" \
            > j1echostderr-18 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echostderr-18 -R R1 18 &&
	grep stderr:bar out18-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job ({{taskid}} stdout & stderr)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out19-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err19-{{taskid}}\"" \
            > j1echoboth-19 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-19 -R R1 19 &&
	grep stdout:baz out19-0 &&
	grep stderr:baz err19-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout & stderr to stdout {{taskid}})' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out20-{{taskid}}\"" \
            > j1echoboth-20 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-20 -R R1 20 &&
	grep stdout:baz out20-0 &&
	grep stderr:baz out20-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job ({{taskid}} stdout/stderr term)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out21-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"term\"" \
            > j1echoboth-21 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-21 -R R1 2> err21 21 &&
	grep stdout:baz out21-0 &&
	grep stderr:baz err21
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (stdout term/{{taskid}} stderr)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err22-{{taskid}}\"" \
            > j1echoboth-22 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-22 -R R1 > out22 22 &&
	grep stdout:baz out22 &&
	grep stderr:baz err22-0
'

#
# 2 task output mustache {{taskid}} tests
#

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job ({{taskid}} stdout)' '
        cat j2echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out23-{{taskid}}\"" \
            > j2echostdout-23 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostdout-23 -R R2 23 &&
	grep "stdout:foo" out23-0 &&
	grep "stdout:foo" out23-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job ({{taskid}} stderr)' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err24-{{taskid}}\"" \
            > j2echostderr-24 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-24 -R R2 24 &&
	grep "stderr:bar" err24-0 &&
	grep "stderr:bar" err24-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stderr to stdout {{taskid}})' '
        cat j2echostderr \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out25-{{taskid}}\"" \
            > j2echostderr-25 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echostderr-25 -R R2 25 &&
	grep "stderr:bar" out25-0 &&
	grep "stderr:bar" out25-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job ({{taskid}} stdout & stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out26-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err26-{{taskid}}\"" \
            > j2echoboth-26 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-26 -R R2 26 &&
	grep "stdout:baz" out26-0 &&
	grep "stdout:baz" out26-1 &&
	grep "stderr:baz" err26-0 &&
	grep "stderr:baz" err26-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout & stderr to stdout {{taskid}})' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out27-{{taskid}}\"" \
            > j2echoboth-27 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-27 -R R2 27 &&
	grep stdout:baz out27-0 &&
	grep stdout:baz out27-1 &&
	grep stderr:baz out27-0 &&
	grep stderr:baz out27-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job ({{taskid}} stdout/stderr term)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out28-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"term\"" \
            > j2echoboth-28 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-28 -R R2 2> err28 28 &&
	grep "stdout:baz" out28-0 &&
	grep "stdout:baz" out28-1 &&
	grep "0: stderr:baz" err28 &&
	grep "1: stderr:baz" err28
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout term/{{taskid}} stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err29-{{taskid}}\"" \
            > j2echoboth-29 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-29 -R R2 > out29 29 &&
	grep "0: stdout:baz" out29 &&
	grep "1: stdout:baz" out29 &&
	grep "stderr:baz" err29-0 &&
	grep "stderr:baz" err29-1
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job ({{taskid}} stdout/stderr file)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out30-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err30\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.label = true" \
            > j2echoboth-30 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-30 -R R2 30 &&
	grep "stdout:baz" out30-0 &&
	grep "stdout:baz" out30-1 &&
	grep "0: stderr:baz" err30 &&
	grep "1: stderr:baz" err30
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task echo job (stdout file/{{taskid}} stderr)' '
        cat j2echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out31\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.label = true" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err31-{{taskid}}\"" \
            > j2echoboth-31 &&
	${FLUX_SHELL} -v -s -r 0 -j j2echoboth-31 -R R2 31 &&
	grep "0: stdout:baz" out31 &&
	grep "1: stdout:baz" out31 &&
	grep "stderr:baz" err31-0 &&
	grep "stderr:baz" err31-1
'

#
# test both {{id}} and {{taskid}}
#

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id per-task stdout & stderr)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id}}-{{taskid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err{{id}}-{{taskid}}\"" \
            > j1echoboth-32 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-32 -R R1 32 &&
	grep stdout:baz out32-0 &&
	grep stderr:baz err32-0
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id stdout & stderr to stdout per-task)' '
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id}}-{{taskid}}\"" \
            > j1echoboth-33 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-33 -R R1 33 &&
	grep stdout:baz out33-0 &&
	grep stderr:baz out33-0
'

#
# output corner case tests
#

test_expect_success HAVE_JQ 'flux-shell: error on bad output type' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"foobar\"" \
            > j1echostdout-34 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-34 -R R1 34
'

test_expect_success HAVE_JQ 'flux-shell: error on no path with file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            > j1echostdout-35 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-35 -R R1 35
'

test_expect_success HAVE_JQ 'flux-shell: error invalid path to file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"/foo/bar/baz\"" \
            > j1echostdout-36 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-36 -R R1 36
'

test_expect_success HAVE_JQ 'flux-shell: error invalid path to file output ({{taskid}})' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"/foo/bar/baz{{taskid}}\"" \
            > j1echostdout-37 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-37 -R R1 37
'

test_done
