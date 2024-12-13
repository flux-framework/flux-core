#!/bin/sh
#
test_description='Test flux-shell rlimit support'

. `dirname $0`/sharness.sh

test_under_flux 1 job

test_expect_success 'flux-shell: propagates rlimits' '
	( ulimit -n 123 &&
	  flux run sh -c "ulimit -n" >ulimit-n.out
	) &&
	test_debug "cat ulimit-n.out" &&
	test "$(cat ulimit-n.out)" = "123"
'
test_expect_success 'flux-shell: works when no rlimits propagated' '
	flux run --rlimit=-* hostname
'
test_expect_success 'flux-shell: works when all rlimits propagated' '
	flux run --rlimit=* sh -c "ulimit -a"
'
test_expect_success 'flux-shell: works when no specific rlimit propagated' '
	flux run --rlimit=nofile=123 sh -c "ulimit -n" >ulimit-n2.out &&
	test_debug "cat ulimit-n2.out" &&
	test "$(cat ulimit-n.out)" = "123"
'
test_expect_success 'flux-shell: nonfatal rlimit errors are logged' '
	flux run --output=nofile.out --rlimit nofile=inf true &&
	grep "nofile exceeds current max"  nofile.out
'
test_expect_success 'flux-shell: invalid rlimit option is fatal error' '
	test_must_fail flux run -o rlimit.foo=1234 true
'
test_done
