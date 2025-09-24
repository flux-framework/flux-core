#!/bin/sh
#

test_description='Test flux-module-exec'

. `dirname $0`/sharness.sh
test_under_flux 1 minimal

testmod=$(realpath ${FLUX_BUILD_DIR}/t/module/.libs/testmod.so)
legacy=$(realpath ${FLUX_BUILD_DIR}/t/module/.libs/legacy.so)
rpc_stream=${FLUX_BUILD_DIR}/t/request/rpc_stream
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

flux setattr log-stderr-level 6

test_expect_success 'flux-module-exec requires an argument' '
	test_must_fail flux module-exec 2>nomod.err &&
	grep Usage nomod.err
'
test_expect_success 'flux-module-exec fails on --badopt' '
	test_must_fail flux module-exec --badopt $testmod 2>badopt.err &&
	grep "unrecognized option" badopt.err
'
test_expect_success 'FLUX_MODULE_URI and --name fail when used together' '
	test_must_fail sh -c "FLUX_MODULE_URI=foo://bar flux module-exec \
	    --name=foo $testmod" 2>urimod.err &&
	grep "are incompatible" urimod.err
'
test_expect_success 'FLUX_MODULE_URI and free args fail when used together' '
	test_must_fail sh -c "FLUX_MODULE_URI=foo://bar flux module-exec \
	    $testmod arg" 2>uriarg.err &&
	grep "are incompatible" uriarg.err
'

##
# test mode
##

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

##
# broker mode
##

test_expect_success 'flux module load --exec heartbeat works' '
	flux module load --exec heartbeat
'
test_expect_success 'flux ping heartbeat works' '
	flux ping -c 1 heartbeat
'
test_expect_success 'flux module stats heartbeat works' '
	flux module stats heartbeat
'
test_expect_success 'flux module remove heartbeat works' '
	flux module remove heartbeat
'
test_expect_success 'flux module load --exec fails on unknown module' '
	test_must_fail flux module load --exec badmod 2>badexecmod.err &&
	grep "module not found in search path" badexecmod.err
'
test_expect_success 'flux module load --exec fails on module init failure' '
	test_must_fail flux module load --exec \
	    $testmod --init-failure 2>modexecfail.err &&
	grep "error" modexecfail.err
'
test_expect_success 'flux module load testmod' '
	flux module load $testmod
'
test_expect_success 'flux module reload --exec testmod works' '
	flux module reload --exec $testmod
'
test_expect_success 'flux ping testmod works' '
	flux ping -c 1 testmod
'
test_expect_success 'simulated module panic cause module to exit' '
	flux setattr broker.module-nopanic 1 &&
	flux dmesg -C &&
	flux event pub testmod.panic &&
	sh -c "while flux module stats testmod; do true; done" &&
	flux dmesg | grep "module runtime failure"
'
test_expect_success 'flux module load testmod' '
	flux module load --exec $testmod
'
test_expect_success NO_CHAIN_LINT 'start background streaming RPC' '
	$rpc_stream testmod.info </dev/null >stream.out 2>stream.err &
	echo $! >stream.pid
'
test_expect_success NO_CHAIN_LINT 'wait for first response to RPC' '
	$waitfile -t 15 stream.out
'
test_expect_success 'simulated module segfault causes module to exit' '
	flux dmesg -C &&
	flux event pub testmod.segfault &&
	sh -c "while flux module stats testmod; do true; done" &&
	flux dmesg >segfault.out
'
test_expect_success 'segfault is reported' '
	grep "testmod: killed by Segmentation fault" segfault.out
'
test_expect_success 'broker treats this the same as spurious module exit' '
	grep "module runtime failure" segfault.out
'
test_expect_success NO_CHAIN_LINT 'streaming RPC was terminated' '
	pid=$(cat stream.pid) &&
	test_must_fail wait $pid
'
test_expect_success NO_CHAIN_LINT 'broker responded with module disconnect' '
	grep "module disconnect" stream.err
'
test_expect_success 'legacy module cannot be loaded under new name' '
        test_must_fail flux module load --exec --name=newname $legacy
'

test_done
