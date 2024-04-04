#!/bin/sh
#

test_description='Verify rc scripts execute with proper semantics
'
# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug

. `dirname $0`/sharness.sh

test_expect_success 'initial program is run when rc1/rc3 are nullified' '
	flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path= \
		/bin/true 2>normal.log
'

test_expect_success 'rc1 failure causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc1_path=/bin/false \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path= \
		-o,-Slog-stderr-level=6 \
		sleep 3600 2>rc1_failure.log
'

test_expect_success 'rc1 bad path handled same as failure' '
	(
	  SHELL=/bin/sh &&
	  test_expect_code 127 flux start \
		-o,-Sbroker.rc1_path=rc1-nonexist \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path= \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>bad1.log
	)
'

test_expect_success 'default initial program is $SHELL' '
	run_timeout --env=SHELL=/bin/sh 60 \
		flux $SHARNESS_TEST_SRCDIR/scripts/runpty.py -i none \
		flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,,-Sbroker.rc3_path= \
		-o,,-Sbroker.shutdown_path= \
		>shell.log &&
	grep "rc2.0: /bin/sh Exited" shell.log
'

test_expect_success 'rc2 failure if stdin not a tty' '
	test_expect_code 1 \
		flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path= \
                2>shell-notty.log &&
	grep "not a tty" shell-notty.log
'

test_expect_success 'rc3 failure causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Sbroker.rc3_path=/bin/false \
		-o,-Slog-stderr-level=6 \
		/bin/true 2>rc3_failure.log
'

test_expect_success 'broker.rc2_none terminates by signal without error' '
	for timeout in 0.5 1 2 4; do
	    run_timeout -s ALRM $timeout flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path= \
		-o,-Sbroker.rc2_none &&
	    break
	done
'

test_expect_success 'shutdown=/bin/true works' '
	flux start -o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path=/bin/true \
		/bin/true
'
test_expect_success 'shutdown=/bin/false causes instance failure' '
	test_expect_code 1 flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path= \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path=/bin/false \
		/bin/true
'

test_expect_success 'shutdown does not run if rc1 fails' '
	test_expect_code 1 flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path=/bin/false \
		-o,-Sbroker.rc3_path= \
		-o,-Sbroker.shutdown_path=memorable-string \
		/bin/true 2>nocleanup.err &&
	test_must_fail grep memorable-string nocleanup.err
'

test_expect_success 'capture the environment for all rc scripts' '
	SLURM_FOO=42 flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path="bash -c printenv >rc1.env" \
		-o,-Sbroker.rc3_path="bash -c printenv >rc3.env" \
		-o,-Sbroker.shutdown_path="bash -c printenv >shutdown.env" \
		"bash -c printenv >rc2.env"
'

var_is_unset() {
	local name=$1; shift
	while test $# -gt 0; do
		grep "^$name=" $1 && return 1
		shift
	done
}

var_is_set() {
	local name=$1; shift
	while test $# -gt 0; do
		grep "^$name=" $1 || return 1
		shift
	done
}

test_expect_success 'PMI_FD, PMI_SIZE, PMI_RANK are not set in rc scripts' '
	var_is_unset PMI_FD *.env &&
	var_is_unset PMI_RANK *.env &&
	var_is_unset PMI_SIZE *.env
'
test_expect_success 'SLURM_* vars were cleared from env of rc scripts' '
	var_is_unset SLURM_FOO *.env
'
test_expect_success 'FLUX_URI is set in rc scripts' '
	var_is_set FLUX_URI *.env
'

test_expect_success 'job environment is not set in rc scripts' '
	var_is_unset FLUX_JOB_ID *.env &&
	var_is_unset FLUX_JOB_SIZE *.env &&
	var_is_unset FLUX_JOB_NNODES *.env &&
	var_is_unset FLUX_JOB_TMPDIR *.env &&
	var_is_unset FLUX_TASK_RANK *.env &&
	var_is_unset FLUX_TASK_LOCAL_ID *.env &&
	var_is_unset FLUX_KVS_NAMESPACE *.env
'

test_expect_success 'capture the environment for instance run as a job' '
	flux start flux run flux start \
		-o,-Slog-stderr-level=6 \
		-o,-Sbroker.rc1_path="bash -c printenv >rc1.env2" \
		-o,-Sbroker.rc3_path="bash -c printenv >rc3.env2" \
		-o,-Sbroker.shutdown_path="bash -c printenv >shutdown.env2" \
		"bash -c printenv >rc2.env2"
'

test_expect_success 'job environment is not set in rcs of subinstance' '
	var_is_unset FLUX_JOB_ID *.env2 &&
	var_is_unset FLUX_JOB_SIZE *.env2 &&
	var_is_unset FLUX_JOB_NNODES *.env2 &&
	var_is_unset FLUX_JOB_TMPDIR *.env2 &&
	var_is_unset FLUX_TASK_RANK *.env2 &&
	var_is_unset FLUX_TASK_LOCAL_ID *.env2 &&
	var_is_unset FLUX_KVS_NAMESPACE *.env2
'

test_done
