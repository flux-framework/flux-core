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
test_expect_success 'test run_timeout with success' '
	run_timeout 1 /bin/true
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

test_expect_success 'test_under_flux works' '
	echo >&2 "$(pwd)" &&
	mkdir -p test-under-flux && (
		cd test-under-flux &&
		cat >.test.t <<-EOF &&
		#!$SHELL_PATH
		pwd
		test_description="test_under_flux (in sub sharness)"
		. "\$SHARNESS_TEST_SRCDIR"/sharness.sh
		test_under_flux 2
		test_expect_success "flux up" "
			flux up
		"
		test_done
		EOF
	chmod +x .test.t &&
	SHARNESS_TEST_DIRECTORY=`pwd` &&
	export SHARNESS_TEST_SRCDIR SHARNESS_TEST_DIRECTORY FLUX_BUILD_DIR debug &&
	./.test.t --verbose --debug >out 2>err
	) &&
	grep "ok: *\[0-1\]" test-under-flux/out
'

test_done
