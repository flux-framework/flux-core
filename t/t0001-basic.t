#!/bin/sh
#

test_description='Test the very basics

Ensure the very basics of flux commands work.
This suite verifies functionality that may be assumed working by
other tests.
'

. `dirname $0`/sharness.sh

test_expect_success 'TEST_NAME is set' '
	test -n "$TEST_NAME"
'
test_expect_success 'run_timeout works' '
	test_expect_code 142 run_timeout 1 sleep 2
'
test_expect_success 'we can find a flux binary' '
	flux --help >/dev/null
'
test_expect_success 'flux-keygen works' "
	flux keygen --force
"
test_expect_success 'flux-config works' '
	flux config get general/cmbd_path
'
test_expect_success 'path to cmbd is sane' '
	cmbd_path=$(flux config get general/cmbd_path)
	test -x ${cmbd_path}
'
test_expect_success 'flux-start works' "
	flux start --size=2 'flux up' | grep '^ok: *\[0-1\]'
"
test_done
