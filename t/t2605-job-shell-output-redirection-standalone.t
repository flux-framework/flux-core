#!/bin/sh
#
test_description='Test flux-shell in --standalone mode'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

#  Run flux-shell under flux command to get correct paths
FLUX_SHELL="flux ${FLUX_BUILD_DIR}/src/shell/flux-shell"

unset FLUX_URI

TEST_SUBPROCESS_DIR=${FLUX_BUILD_DIR}/src/common/libsubprocess

test_expect_success 'flux-shell: generate 1-task echo jobspecs and matching R' '
	flux mini run --dry-run -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j1echostdout &&
	flux mini run --dry-run -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j1echostderr &&
	flux mini run --dry-run -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j1echoboth &&
	cat >R1 <<-EOT
	{"version": 1, "execution":{ "R_lite":[
		{ "children": { "core": "0" }, "rank": "0" }
        ]}}
	EOT
'

test_expect_success 'flux-shell: generate 2-task echo jobspecs and matching R' '
	flux mini run --dry-run -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O foo > j2echostdout &&
	flux mini run --dry-run -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -E bar > j2echostderr &&
	flux mini run --dry-run -N1 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -P -O -E baz > j2echoboth &&
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
# output file mustache tests
#

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id stdout file/stderr file)' '
    id=$(flux job id --to=f58 14) &&
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{jobid}}\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stderr.path = \"err{{jobid}}\"" \
            > j1echoboth-14 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-14 -R R1 14 &&
	grep stdout:baz out${id} &&
	grep stderr:baz err${id}
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task echo job (mustache id stdout & stderr to stdout file)' '
    id=$(flux job id --to=f58 15) &&
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id}}\"" \
            > j1echoboth-15 &&
	${FLUX_SHELL} -v -s -r 0 -j j1echoboth-15 -R R1 15 &&
	grep stdout:baz out${id} &&
	grep stderr:baz out${id}
'

for type in f58 dec hex dothex words; do
  test_expect_success HAVE_JQ "flux-shell: output template {{id.$type}}" '
    jobid=123456789 &&
    id=$(flux job id --to=${type} ${jobid}) &&
        cat j1echoboth \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"out{{id.${type}}}\"" \
            > j1-mustache-${type} &&
	${FLUX_SHELL} -v -s -r 0 -j j1-mustache-${type} -R R1 ${jobid} &&
	grep stdout:baz out${id} &&
	grep stderr:baz out${id}
 '
done

test_expect_success HAVE_JQ "flux-shell: bad output mustache template is not rendered" '
	cat j1echoboth \
	    |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
	    |  $jq ".attributes.system.shell.options.output.stdout.path = \"{{idx}}.out\"" \
	    > j1-mustache-error1 &&
	${FLUX_SHELL} -v -s -r 0 -j j1-mustache-error1 -R R1 1234 &&
	grep stdout:baz {{idx}}.out &&
	grep stderr:baz {{idx}}.out
'
test_expect_success HAVE_JQ "flux-shell: bad output mustache template is not rendered" '
	cat j1echoboth \
	    |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
	    |  $jq ".attributes.system.shell.options.output.stdout.path = \"{{id.x}}.out\"" \
	    > j1-mustache-error2 &&
	${FLUX_SHELL} -v -s -r 0 -j j1-mustache-error2 -R R1 1234 &&
	grep stdout:baz {{id.x}}.out &&
	grep stderr:baz {{id.x}}.out
'

test_expect_success HAVE_JQ "flux-shell: unknown mustache template is not rendered" '
	cat j1echoboth \
	    |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
	    |  $jq ".attributes.system.shell.options.output.stdout.path = \"{{foo}}.out\"" \
	    > j1-mustache-error3 &&
	${FLUX_SHELL} -v -s -r 0 -j j1-mustache-error3 -R R1 1234 &&
	grep stdout:baz {{foo}}.out &&
	grep stderr:baz {{foo}}.out
'

test_expect_success HAVE_JQ "flux-shell: too large mustache template is not rendered" '
	tmpl=$(printf "%0.sf" $(seq 0 120)) &&
	cat j1echoboth \
	    |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
	    |  $jq ".attributes.system.shell.options.output.stdout.path = \"{{$tmpl}}.out\"" \
	    > j1-mustache-error4 &&
	${FLUX_SHELL} -v -s -r 0 -j j1-mustache-error4 -R R1 1234 &&
	grep stdout:baz {{$tmpl}}.out &&
	grep stderr:baz {{$tmpl}}.out
'

#
# output corner case tests
#

test_expect_success HAVE_JQ 'flux-shell: error on bad output type' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"foobar\"" \
            > j1echostdout-16 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-16 -R R1 16
'

test_expect_success HAVE_JQ 'flux-shell: error on no path with file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            > j1echostdout-17 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-17 -R R1 17
'

test_expect_success HAVE_JQ 'flux-shell: error invalid path to file output' '
        cat j1echostdout \
            |  $jq ".attributes.system.shell.options.output.stdout.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.output.stdout.path = \"/foo/bar/baz\"" \
            > j1echostdout-18 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdout-18 -R R1 18
'

test_done
