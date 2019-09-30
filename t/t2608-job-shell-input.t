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

test_expect_success 'flux-shell: generate input file for stdin input file tests' '
       echo "foo" > input_stdin_file &&
       echo "doh" >> input_stdin_file
'

#
# input file tests
#

test_expect_success 'flux-shell: run 1-task input file as stdin job' '
        flux mini run -n1 --input=input_stdin_file \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > out0 &&
        test_cmp input_stdin_file out0
'

test_expect_success 'flux-shell: run 2-task input file as stdin job' '
        flux mini run -n2 --input=input_stdin_file --label-io \
             ${TEST_SUBPROCESS_DIR}/test_echo -O -n > out1 &&
        grep "0: foo" out1 &&
        grep "0: doh" out1 &&
        grep "1: foo" out1 &&
        grep "1: doh" out1
'

test_expect_success 'job-shell: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done
