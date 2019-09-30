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

#
# input file tests
#

test_expect_success 'flux-shell: generate input file for stdin input file tests' '
       echo "foo" > input_stdin_file &&
       echo "doh" >> input_stdin_file
'

test_expect_success 'flux-shell: generate echo stdin file jobspecs and matching Rs' '
       flux jobspec srun -N1 -n1 ${TEST_SUBPROCESS_DIR}/test_echo -O -n > j1echostdinfile &&
       flux jobspec srun -N2 -n2 ${TEST_SUBPROCESS_DIR}/test_echo -O -n > j2echostdinfile &&
       cat >R1 <<-EOT &&
{"version": 1, "execution":{ "R_lite":[
        { "children": { "core": "0" }, "rank": "0" }
]}}
EOT
       cat >R2 <<-EOT
{"version": 1, "execution":{ "R_lite":[
        { "children": { "core": "0-1" }, "rank": "0" }
]}}
EOT
'

test_expect_success HAVE_JQ 'flux-shell: run 1-task input file as stdin job' '
        cat j1echostdinfile \
            |  $jq ".attributes.system.shell.options.input.stdin.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.input.stdin.path = \"input_stdin_file\"" \
            > j1echostdinfile-0 &&
        ${FLUX_SHELL} -v -s -r 0 -j j1echostdinfile-0 -R R1 0 > out0 &&
        grep "0: foo" out0 &&
        grep "0: doh" out0
'

test_expect_success HAVE_JQ 'flux-shell: run 2-task input file as stdin job' '
        cat j2echostdinfile \
            |  $jq ".attributes.system.shell.options.input.stdin.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.input.stdin.path = \"input_stdin_file\"" \
            > j2echostdinfile-1 &&
        ${FLUX_SHELL} -v -s -r 0 -j j2echostdinfile-1 -R R2 1 > out1 &&
        grep "0: foo" out1 &&
        grep "0: doh" out1 &&
        grep "1: foo" out1 &&
        grep "1: doh" out1
'

test_expect_success HAVE_JQ 'flux-shell: input file as stdin bad type' '
        cat j1echostdinfile \
            |  $jq ".attributes.system.shell.options.input.stdin.type = \"foobar\"" \
            |  $jq ".attributes.system.shell.options.input.stdin.path = \"input_stdin_file\"" \
            > j1echostdinfile-2 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdinfile-2 -R R1 2 > out2
'

test_expect_success HAVE_JQ 'flux-shell: input file as stdin bad path' '
        cat j1echostdinfile \
            |  $jq ".attributes.system.shell.options.input.stdin.type = \"file\"" \
            |  $jq ".attributes.system.shell.options.input.stdin.path = \"/foo/bar/baz\"" \
            > j1echostdinfile-3 &&
        ! ${FLUX_SHELL} -v -s -r 0 -j j1echostdinfile-3 -R R1 3 > out3
'

test_done
