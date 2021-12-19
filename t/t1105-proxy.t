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
	echo '{\"service\":\"broker\"}' >service4.add.in &&
        flux proxy $TEST_URI \
	  $RPC service.add 17 <service4.add.in
"

test_expect_success 'flux-proxy calls out to flux-uri for unknown URI scheme' '
	result=$(flux proxy pid:$$ flux getattr local-uri) &&
	test_debug "echo proxy getattr local-uri = $result" &&
	test "$result" = "$FLUX_URI"
'

test_expect_success 'flux-proxy fails if flux-uri fails' '
	test_must_fail flux proxy bloop:1234 2>baduri.err &&
	test_debug "cat baduri.err" &&
	grep "Unable to resolve bloop:1234 to a URI" baduri.err
'

test_expect_success 'flux-proxy fails with unknown URI path (ENOENT)' '
	test_must_fail flux proxy local:///noexist  2>badpath.err &&
	grep "No such file or directory" badpath.err
'

test_expect_success 'flux-proxy forwards LD_LIBRARY_PATH' '
	cat >proxinator.sh <<-EOF &&
	#!/bin/sh
	echo ssh "\$@" > proxinator.log
	EOF
	chmod +x proxinator.sh &&
	(export LD_LIBRARY_PATH=/foo &&
		export FLUX_SSH=./proxinator.sh &&
		test_must_fail flux proxy ssh://hostname/baz/local) &&
	test_debug "cat ./proxinator.log" &&
	grep -E "ssh.* LD_LIBRARY_PATH=[^ ]*:?/foo .*/flux relay" ./proxinator.log
'

test_expect_success 'set bogus broker version' '
	flux getattr version >realversion &&
	flux setattr version 0.0.0
'
test_expect_success 'flux-proxy fails with version mismatch' '
	test_must_fail flux proxy $FLUX_URI /bin/true
'
test_expect_success 'flux-proxy --force works with version mismatch' '
	flux proxy --force $FLUX_URI /bin/true
'
test_expect_success 'restore real broker version' '
	flux setattr version $(cat realversion)
'

test_expect_success 'flux-proxy works with jobid argument' '
	id=$(flux mini submit -n1 flux start flux mini run sleep 30) &&
	flux job wait-event -t 10 $id memo &&
	uri=$(flux proxy $id?local flux getattr parent-uri) &&
	test_debug "echo flux proxy $id flux getattr parent-uri = $uri" &&
	test "$uri" = "$FLUX_URI"
'
test_expect_success 'flux-proxy works with /jobid argument' '
	uri=$(flux proxy /$id?local flux getattr parent-uri) &&
	test_debug "echo flux proxy $id flux getattr parent-uri = $uri" &&
	test "$uri" = "$FLUX_URI" &&
	flux job cancel $id &&
	flux job wait-event -vt 10 $id clean
'

test_done
