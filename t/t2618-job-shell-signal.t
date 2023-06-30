#!/bin/sh
#
test_description='Test flux-shell signal before timelimit support'

. `dirname $0`/sharness.sh

test_under_flux 2 job

check_signal()
{
	local spec=$1 &&
	local sig=${2:-10} &&
	local tleft=${3:-60} &&
	name=test.${spec} &&
	attr=.attributes.system.shell.options.signal &&
	flux run --dry-run --signal=$spec hostname >$name.json 2>$name.err &&
	test_debug "jq $attr < $name.json" &&
	jq -e "$attr | .signum == $sig and .timeleft == $tleft" < $name.json
}

# Assumptions:
# SIGUSR1 == 10
# SIGUSR2 == 12
test_expect_success 'cli: check_signal() test function works' '
	test_expect_code 1 check_signal SIGFOO &&
	test_expect_code 1 check_signal USR2 &&
	test_expect_code 1 check_signal @2m
'
test_expect_success 'cli: --signal option works' '
	check_signal @ 10 60 &&
	check_signal 15 15 60 &&
	check_signal USR2 12 60 &&
	check_signal SIGUSR2 12 60 &&
	check_signal @5s 10 5 &&
	check_signal @10m 10 600 &&
	check_signal USR2@10m 12 600
'
test_expect_success 'cli: --signal option handles invalid args' '
	test_expect_code 1 check_signal SIG &&
	grep "signal.*is invalid" test.SIG.err &&
	test_expect_code 1 check_signal @3y &&
	grep "invalid Flux standard duration" test.@3y.err &&
	test_expect_code 1 check_signal -1 &&
	grep "signal must be > 0" test.-1.err
'
test_fails_by_signal() {
	local signum=$1
	shift
	"$@"
	exit_code=$?
	if test $exit_code = $((128+signum)); then
		return 0
	elif test $exit_code = 0; then
		echo >&2 "test_fails_by_signal: command succeeded: $*"
	elif test $exit_code -gt 129 -a $exit_code -le 192; then
		sig=$((exit_code+signum))
		echo >&2 "test_fails_by_signal: got unexpected signal $sig: $*"
	elif test $exit_code = 127; then
		echo >&2 "test_fails_by_signal: command not found: $*"
	else
		echo >&2 "test_fails_by_signal: exited with code $exit_code: $*"
	fi
	return 1
}

test_expect_success 'shell: signal option works' '
	test_fails_by_signal 10 flux run -t 5s --signal=@4.5s sleep 15
'
test_expect_success 'shell: signal option works with alternate signal' '
	test_fails_by_signal 12 flux run -t 5s --signal=USR2@4.5s sleep 15
'
test_expect_success 'shell: signal shell option can be an integer' '
	test_fails_by_signal 10 flux run -t 5s -o signal -o verbose sleep 10 \
		2>sig.log &&
	grep "will expire in 60.0s" sig.log
'
test_expect_success 'shell: signal option is ignored when --time-limit not set' '
	flux run --signal=@60 -o verbose sleep 0.5 >no-time-limit.out 2>&1 &&
	test_debug "cat no-time-limit.out" &&
	test_must_fail grep "Will send SIGUSR1 to job" no-time-limit.out
'
test_done
