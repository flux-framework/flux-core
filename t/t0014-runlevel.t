#!/bin/sh
#

test_description='Verify rc scripts execute with proper semantics
'
# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug

. `dirname $0`/sharness.sh

test_expect_success 'initial program is run when rc1/rc3 are nullified' '
	flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		true 2>normal.log
'

test_expect_success 'rc1 failure causes instance failure' '
	test_expect_code 1 flux start \
		-Sbroker.rc1_path=false -Sbroker.rc3_path= \
		-Slog-stderr-level=6 \
		sleep 3600 2>rc1_failure.log
'

test_expect_success 'rc1 bad path handled same as failure' '
	(
	  SHELL=/bin/sh &&
	  test_expect_code 127 flux start \
		-Sbroker.rc1_path=rc1-nonexist -Sbroker.rc3_path= \
		-Slog-stderr-level=6 \
		true 2>bad1.log
	)
'

test_expect_success 'default initial program is $SHELL' '
	run_timeout --env=SHELL=/bin/sh 60 \
		flux $SHARNESS_TEST_SRCDIR/scripts/runpty.py -i none \
		flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		>shell.log &&
	grep "rc2.0: /bin/sh Exited" shell.log
'

test_expect_success 'rc2 failure if stdin not a tty' '
	test_expect_code 1 \
		flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
                2>shell-notty.log &&
	grep "not a tty" shell-notty.log
'

test_expect_success 'rc3 failure causes instance failure' '
	test_expect_code 1 flux start \
		-Sbroker.rc3_path=false \
		-Slog-stderr-level=6 \
		true 2>rc3_failure.log
'

test_expect_success 'broker.rc2_none terminates by signal without error' '
	for timeout in 0.5 1 2 4; do
	    run_timeout -s ALRM $timeout flux start \
		-Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= -Sbroker.rc2_none &&
	    break
	done
'

test_expect_success 'flux admin cleanup-push true works' '
	flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		flux admin cleanup-push true
'

test_expect_success 'flux admin cleanup-push false causes instance failure' '
	test_expect_code 1 flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		flux admin cleanup-push false
'

test_expect_success 'cleanup does not run if rc1 fails' '
	test_expect_code 1 flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path=false -Sbroker.rc3_path= \
		flux admin cleanup-push memorable-string 2>nocleanup.err && \
	test_must_fail grep memorable-string nocleanup.err
'

test_expect_success 'flux admin cleanup-push (empty) fails' '
	test_expect_code 1 flux start \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		flux admin cleanup-push "" 2>push.err &&
	grep "cannot push an empty command line" push.err
'

test_expect_success 'flux admin cleanup-push with no commands fails' '
	test_expect_code 1 flux start \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		flux admin cleanup-push </dev/null 2>push2.err &&
	grep "commands array is empty" push2.err
'

test_expect_success 'flux admin cleanup-push (stdin) works' '
	echo true | flux start -Slog-stderr-level=6 \
		-Sbroker.rc1_path= -Sbroker.rc3_path= \
		flux admin cleanup-push 2>push-stdin.err &&
	grep cleanup.0 push-stdin.err
'

test_expect_success 'flux admin cleanup-push (stdin) retains cmd block order' '
	flux start -Sbroker.rc1_path= -Sbroker.rc3_path= \
		-Slog-stderr-level=6 \
		flux admin cleanup-push <<-EOT 2>hello.err &&
	echo Hello world
	echo Hello solar system
	EOT
	grep "cleanup.0: Hello world" hello.err &&
	grep "cleanup.1: Hello solar system" hello.err
'

test_expect_success 'capture the environment for all three rc scripts' '
	SLURM_FOO=42 FLUX_ENCLOSING_ID=66 flux start \
		-Slog-stderr-level=6 \
		-Sbroker.rc1_path="bash -c printenv >rc1.env" \
		-Sbroker.rc3_path="bash -c printenv >rc3.env" \
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
	var_is_unset FLUX_JOB_ID_PATH *.env &&
	var_is_unset FLUX_JOB_SIZE *.env &&
	var_is_unset FLUX_JOB_NNODES *.env &&
	var_is_unset FLUX_JOB_TMPDIR *.env &&
	var_is_unset FLUX_TASK_RANK *.env &&
	var_is_unset FLUX_TASK_LOCAL_ID *.env &&
	var_is_unset FLUX_KVS_NAMESPACE *.env
'

test_expect_success 'FLUX_ENCLOSING_ID not set if instance is not a job' '
	var_is_unset FLUX_ENCLOSING_ID *.env
'

test_expect_success 'capture the environment for instance run as a job' '
	flux start flux run flux start \
		-Slog-stderr-level=6 \
		-Sbroker.rc1_path="bash -c printenv >rc1.env2" \
		-Sbroker.rc3_path="bash -c printenv >rc3.env2" \
		"bash -c printenv >rc2.env2"
'

test_expect_success 'job environment is not set in rcs of subinstance' '
	var_is_unset FLUX_JOB_ID *.env2 &&
	var_is_unset FLUX_JOB_ID_PATH *.env2 &&
	var_is_unset FLUX_JOB_SIZE *.env2 &&
	var_is_unset FLUX_JOB_NNODES *.env2 &&
	var_is_unset FLUX_JOB_TMPDIR *.env2 &&
	var_is_unset FLUX_TASK_RANK *.env2 &&
	var_is_unset FLUX_TASK_LOCAL_ID *.env2 &&
	var_is_unset FLUX_KVS_NAMESPACE *.env2
'

test_expect_success 'FLUX_ENCLOSING_ID is set if instance is a job' '
	var_is_set FLUX_ENCLOSING_ID *.env2
'

test_done
