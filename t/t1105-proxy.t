#!/bin/sh
#

test_description='Test flux-proxy'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

export TEST_URI=$FLUX_URI
export TEST_SOCKDIR=$(echo $FLUX_URI | sed -e "s!local://!!") &&
export TEST_FLUX=${FLUX_BUILD_DIR}/src/cmd/flux
export TEST_TMPDIR=${TMPDIR:-/tmp}
RPC=${FLUX_BUILD_DIR}/t/request/rpc
EVENT_TRACE="$SHARNESS_TEST_SRCDIR/scripts/event-trace.lua"

test_expect_success 'flux-proxy creates new socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	test "$PROXY_URI" != "$TEST_URI"
'

test_expect_success 'flux-proxy cleans up socket' '
	PROXY_URI=$(flux proxy $TEST_URI printenv FLUX_URI) &&
	path=$(echo $PROXY_URI | sed -e "s!local://!!") &&
	! test -d $path
'

test_expect_success 'flux-proxy exits with command return code' '
	flux proxy $TEST_URI /bin/true &&
	! flux proxy $TEST_URI /bin/false
'

test_expect_success 'flux-proxy forwards getattr request' '
	ATTR_SIZE=$(flux proxy $TEST_URI flux getattr size) &&
	test "$ATTR_SIZE" = "$SIZE"
'

test_expect_success 'flux-proxy permits dynamic service registration' "
        echo '{\"service\":\"fubar\"}' >service.add.in &&
        flux proxy $TEST_URI \
	  $RPC service.add 0 <service.add.in
"

test_expect_success 'flux-proxy can re-register service after disconnect' "
	echo '{\"service\":\"fubar\"}' >service2.add.in &&
        flux proxy $TEST_URI \
	  $RPC service.add 0 <service2.add.in
"

test_expect_success 'flux-proxy cannot register service with method (EINVAL)' "
	echo '{\"service\":\"fubar.baz\"}' >service3.add.in &&
        flux proxy $TEST_URI \
	  $RPC service.add 22 <service3.add.in
"

test_expect_success 'flux-proxy cannot shadow a broker service (EEXIST)' "
	echo '{\"service\":\"cmb\"}' >service4.add.in &&
        flux proxy $TEST_URI \
	  $RPC service.add 17 <service4.add.in
"

test_expect_success 'flux-proxy fails with unknown URI scheme (ENOENT)' '
	test_must_fail flux proxy badscheme:// 2>badscheme.err &&
	grep "No such file or directory" badscheme.err
'
test_expect_success 'flux-proxy fails with unknown URI path (ENOENT)' '
	test_must_fail flux proxy local:///noexist  2>badpath.err &&
	grep "No such file or directory" badpath.err
'

test_done
