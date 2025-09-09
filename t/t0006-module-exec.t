#!/bin/sh
#

test_description='Test flux-module-exec'

. `dirname $0`/sharness.sh
test_under_flux 1 minimal

testmod=$(realpath ${FLUX_BUILD_DIR}/t/module/.libs/testmod.so)

flux setattr log-stderr-level 6

test_expect_success 'flux-module-exec requires an argument' '
	test_must_fail flux module-exec 2>nomod.err &&
	grep Usage nomod.err
'
test_expect_success 'flux-module-exec fails on --badopt' '
	test_must_fail flux module-exec --badopt $testmod 2>badopt.err &&
	grep "unrecognized option" badopt.err
'
test_expect_success 'flux-module-exec fails on unknown module' '
	test_must_fail flux module-exec badmod 2>badmod.err &&
	grep "module not found in search path" badmod.err
'
test_expect_success 'simulated module failure causes command failure' '
	test_must_fail flux module-exec \
	    $testmod --init-failure 2>modfail.err &&
	grep "module failed" modfail.err
'
test_expect_success 'rank attribute is cached in module handle' '
	flux module-exec $testmod --attr-is-cached=rank
'
test_expect_success 'size attribute is cached in module handle' '
	flux module-exec $testmod --attr-is-cached=size
'
test_expect_success 'config is cached in module handle' '
	flux module-exec $testmod --config-is-cached
'
test_expect_success NO_CHAIN_LINT 'start testmod in the background' '
	flux module-exec $testmod &
	echo $! >modexec.pid
'
# Usage: ping_retry target retries sleep
ping_retry() {
	tries=$(($2+1))
	while ! flux ping -c 1 $1; do
	    tries=$(($tries-1))
	    test $tries -eq 0 && return 1
	    sleep $3
	done
	return 0
}
test_expect_success NO_CHAIN_LINT 'ping testmod works' '
	ping_retry testmod 30 0.2
'
test_expect_success NO_CHAIN_LINT 'flux module stats testmod works' '
	flux module stats testmod
'
test_expect_success NO_CHAIN_LINT 'flux module remove testmod fails' '
	test_must_fail flux module remove testmod
'
test_expect_success NO_CHAIN_LINT 'publish testmod.panic' '
	flux event pub testmod.panic
'
test_expect_success NO_CHAIN_LINT 'testmod unloads with error' '
	pid=$(cat <modexec.pid) &&
	test_must_fail wait $pid
'

# Usage: shutdown modname
shutdown_mod() {
	flux python -c "import flux; flux.Flux().rpc(\"$1.shutdown\")"
}

test_expect_success NO_CHAIN_LINT 'start heartbeat in the background' '
	flux module-exec heartbeat &
	echo $! >heartbeat.pid
'
test_expect_success NO_CHAIN_LINT 'ping heartbeat works' '
	ping_retry heartbeat 30 0.2
'
test_expect_success NO_CHAIN_LINT 'send heartbeat.shutdown' '
	shutdown_mod heartbeat
'
test_expect_success NO_CHAIN_LINT 'heartbeat unloads without error' '
	pid=$(cat <heartbeat.pid) &&
	wait $pid
'

test_done
