#!/bin/sh
#
test_description='Test flux-shell oom plugin'

. `dirname $0`/sharness.sh

test_under_flux 1

test_expect_success 'run with -o oom.wrongopt fails' '
	test_must_fail flux run -o oom.wrongopt true 2>wrongopt.err &&
	test_debug "cat wrongopt.err" &&
	grep -q "left unpacked: wrongopt" wrongopt.err
'

# Sometimes /proc/PID/oom_score_adj: Permission denied.  Why?
test_expect_success 'run with -o oom.adjust=1000 does not fail' '
	flux run -o verbose -o oom.adjust=1000 true 2>adjust.debug &&
	test_debug "cat adjust.debug"
'

test_expect_success 'failure to set the requested adjustment is non-fatal' '
	flux run -o verbose -o oom.adjust=-1000 true 2>warn.debug &&
	test_debug "cat warn.debug" &&
	grep -q "Permission denied" warn.debug
'

test_done
