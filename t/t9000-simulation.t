#!/bin/sh

test_description='Test flux simulator command'

. $(dirname $0)/sharness.sh

test_under_flux 1

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

flux setattr log-stderr-level 1

SIM_JOBTRACES_DIR=${SHARNESS_TEST_SRCDIR}/simulator/job-traces

test_expect_success 'flux simulator fails with usage message' '
	test_must_fail flux simulator 2>usage.err &&
	grep -i usage: usage.err
'

test_expect_success 'flux simulator with single node works' '
	flux simulator $SIM_JOBTRACES_DIR/10-single-node.csv 1 16 >run1.out &&
    grep -i "utilization: 100" run1.out
'

test_expect_success 'flux simulator with multiple nodes works' '
	flux simulator $SIM_JOBTRACES_DIR/10-single-node.csv 3 16 >run2.out &&
    grep -i "utilization: 83" run2.out
'

test_expect_failure 'flux simulator errors out on unstaisfiable jobs' '
	run_timeout 5 flux simulator $SIM_JOBTRACES_DIR/10-multi-node.csv 1 16 >run3.out 2>run3.err &&
    grep -i "unsatisfiable" run3.err
'

test_done