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
RUNPTY="$SHARNESS_TEST_SRCDIR/scripts/runpty.py"

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
	flux proxy $TEST_URI true &&
	! flux proxy $TEST_URI false
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
	(export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/foo &&
		export FLUX_SSH=./proxinator.sh &&
		test_must_fail flux proxy ssh://hostname/baz/local) &&
	test_debug "cat ./proxinator.log" &&
	grep -E "ssh.* LD_LIBRARY_PATH=[^ ]*:?/foo .*flux relay" ./proxinator.log
'

test_expect_success 'set bogus broker version' '
	flux getattr version >realversion &&
	flux setattr version 0.0.0
'
test_expect_success 'flux-proxy fails with version mismatch' '
	test_must_fail flux proxy $FLUX_URI true
'
test_expect_success 'flux-proxy --force works with version mismatch' '
	flux proxy --force $FLUX_URI true
'
test_expect_success 'restore real broker version' '
	flux setattr version $(cat realversion)
'

test_expect_success 'flux-proxy works with jobid argument' '
	id=$(flux submit -n1 flux start flux run sleep 30) &&
	flux job wait-event -t 10 $id memo &&
	uri=$(flux proxy $id?local flux getattr parent-uri) &&
	test_debug "echo flux proxy $id flux getattr parent-uri = $uri" &&
	test "$uri" = "$FLUX_URI"
'
test_expect_success 'flux-proxy works with /jobid argument' '
	uri=$(flux proxy /$id?local flux getattr parent-uri) &&
	test_debug "echo flux proxy $id flux getattr parent-uri = $uri" &&
	test "$uri" = "$FLUX_URI"
'
tssh=${SHARNESS_TEST_SRCDIR}/scripts/tssh
test_expect_success 'flux-proxy sets FLUX_PROXY_REMOTE for remote URIs' '
	FLUX_SSH=${tssh} \
	    flux proxy ${id}?remote printenv FLUX_PROXY_REMOTE \
	      >proxy_remote.out &&
	test_debug "cat proxy_remote.out" &&
	test "$(cat proxy_remote.out)" = "$(hostname)"
'
test_expect_success 'flux-proxy does not set FLUX_PROXY_REMOTE for local URIs' '
	test_must_fail flux proxy ${id}?local printenv FLUX_PROXY_REMOTE
'
test_expect_success 'flux-proxy preserves options in FLUX_PROXY_REMOTE' '
	uri=$(flux uri ${id} | sed "s|ssh://|ssh://user@|") &&
	FLUX_SSH=${tssh} \
	    flux proxy $uri printenv FLUX_PROXY_REMOTE \
	      >proxy_remote_user.out &&
	test_debug "cat proxy_remote_user.out" &&
	test "$(cat proxy_remote_user.out)" = "user@$(hostname)"
'
test_expect_success 'parent-uri under remote flux-proxy is rewritten' '
	uri=$(flux uri ${id} | sed "s|ssh://|ssh://user@|") &&
	FLUX_SSH=${tssh} \
	    flux proxy $uri flux getattr parent-uri >proxy-parent-uri.out &&
	test_debug "cat proxy-parent-uri.out" &&
	grep ssh://user@$(hostname) proxy-parent-uri.out
'
test_expect_success 'flux-start does not hang under flux-proxy' '
	run_timeout --kill-after=10 --env=FLUX_SSH=${tssh} 60 \
		flux proxy $uri flux start true
'
test_expect_success 'cancel test job' '
	flux cancel $id &&
	flux job wait-event -vt 10 $id clean
'
test_expect_success NO_CHAIN_LINT 'flux-proxy attempts to restore terminal on error' '
	cat <<-EOF >test.sh &&
	#!/bin/bash
	flux --parent cancel \$(flux getattr jobid)
	while flux getattr jobid; do sleep 0.1; done
	EOF
	chmod +x test.sh
	id=$(flux batch -n1 --wrap flux run sleep 600) &&
	flux job wait-event -vt 60 $id memo &&
	$RUNPTY -o pty.out -f asciicast \
		flux proxy --nohup ${id}?local $(pwd)/test.sh &&
	grep "\[\?25h" pty.out
'
test_expect_success NO_CHAIN_LINT 'flux-proxy sends SIGHUP to children without --nohup' '
	SHELL=/bin/sh &&
	cat <<-EOF >test.sh &&
	#!/bin/bash
	flux --parent cancel \$(flux getattr jobid)
	while true; do sleep 0.1; done
	EOF
	chmod +x test.sh
	id=$(flux batch -n1 --wrap flux run sleep 600) &&
	flux job wait-event -vt 60 $id memo &&
	test_expect_code 129 $RUNPTY -o pty.out -f asciicast \
		flux proxy ${id}?local $(pwd)/test.sh &&
	grep "\[\?25h" pty.out &&
	grep "SIGHUP" pty.out
'
test_expect_success 'flux-proxy falls back to /tmp with invalid TMPDIR' '
	id=$(flux batch -n1 --wrap sleep inf) &&
	flux proxy ${id}?local flux uptime &&
	flux cancel $id &&
	flux job wait-event -Hvt 60 $id clean
'
test_done
