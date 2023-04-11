#!/bin/sh
#

test_description='Test basic module management

Verify module load/unload/list
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

invalid_rank() {
	echo $((${SIZE} + 1))
}

testmod=${FLUX_BUILD_DIR}/t/module/.libs/testmod.so

module_status_bad_proto() {
	flux python -c "import flux; print(flux.Flux().rpc(\"broker.module-status\").get())"
}

module_status () {
	flux python -c "import flux; print(flux.Flux().rpc(\"broker.module-status\",{\"status\":0}).get())"
}

test_expect_success 'module: load test module' '
	flux module load $testmod
'
test_expect_success 'module: reload test module' '
	flux module reload $testmod
'

test_expect_success 'module: lsmod shows test module' '
	flux module list | grep testmod
'

test_expect_success 'module: cannot load the same module twice' '
	test_must_fail flux module load $testmod
'

test_expect_success 'module: unload test module' '
	flux module remove testmod
'

test_expect_success 'module: lsmod does not show test module' '
	! flux module list | grep testmod
'

test_expect_success 'module: insmod returns initialization error' '
	test_must_fail flux module load $testmod --init-failure
'

test_expect_success 'module: load fails on invalid module' '
	test_must_fail flux module load nosuchmodule 2>load.err &&
	grep "module not found" load.err
'

test_expect_success 'module: remove fails on invalid module' '
	test_must_fail flux module remove nosuchmodule 2>nosuch.err &&
	grep "nosuchmodule: No such file or directory" nosuch.err
'
test_expect_success 'module: remove -f succeeds on nonexistent module' '
	flux module remove -f nosuchmodule
'

# N.B. avoid setting the actual debug bits - lets reserve LSB
REALMOD=connector-local

test_expect_success 'flux module debug gets debug flags' '
	FLAGS=$(flux module debug $REALMOD) &&
	test "$FLAGS" = "0x0"
'
test_expect_success 'flux module debug --setbit sets individual debug flags' '
	flux module debug --setbit 0x10000 $REALMOD &&
	FLAGS=$(flux module debug $REALMOD) &&
	test "$FLAGS" = "0x10000"
'
test_expect_success 'flux module debug --set replaces debug flags' '
	flux module debug --set 0xff00 $REALMOD &&
	FLAGS=$(flux module debug $REALMOD) &&
	test "$FLAGS" = "0xff00"
'
test_expect_success 'flux module debug --clearbit clears individual debug flags' '
	flux module debug --clearbit 0x1000 $REALMOD &&
	FLAGS=$(flux module debug $REALMOD) &&
	test "$FLAGS" = "0xef00"
'
test_expect_success 'flux module debug --clear clears debug flags' '
	flux module debug --clear $REALMOD &&
	FLAGS=$(flux module debug $REALMOD) &&
	test "$FLAGS" = "0x0"
'

# test stats

test_expect_success 'flux module stats gets comms statistics' '
	flux module stats $REALMOD >comms.stats
'

test_expect_success 'flux module stats --parse tx.event counts events' '
	EVENT_TX=$(flux module stats --parse tx.event $REALMOD) &&
	flux event pub xyz &&
	EVENT_TX2=$(flux module stats --parse tx.event $REALMOD) &&
	test "$EVENT_TX" = $((${EVENT_TX2}-1))
'

test_expect_success 'flux module stats --clear works' '
	flux event pub xyz &&
	flux module stats --clear $REALMOD &&
	EVENT_TX2=$(flux module stats --parse tx.event $REALMOD) &&
	test "$EVENT_TX" = 0
'

test_expect_success 'flux module stats --clear-all works' '
	flux event pub xyz &&
	flux module stats --clear-all $REALMOD &&
	EVENT_TX2=$(flux module stats --parse tx.event $REALMOD) &&
	test "$EVENT_TX" = 0
'
test_expect_success 'flux module stats --scale works' '
	flux event pub xyz &&
	EVENT_TX=$(flux module stats --parse tx.event $REALMOD) &&
	EVENT_TX2=$(flux module stats --parse tx.event --scale=2 $REALMOD) &&
	test "$EVENT_TX2" -eq $((${EVENT_TX}*2))
'


test_expect_success 'flux module stats --rusage works' '
	flux module stats --rusage $REALMOD >rusage.stats &&
	grep -q utime rusage.stats &&
	grep -q stime rusage.stats &&
	grep -q maxrss rusage.stats &&
	grep -q ixrss rusage.stats &&
	grep -q idrss rusage.stats &&
	grep -q isrss rusage.stats &&
	grep -q minflt rusage.stats &&
	grep -q majflt rusage.stats &&
	grep -q nswap rusage.stats &&
	grep -q inblock rusage.stats &&
	grep -q oublock rusage.stats &&
	grep -q msgsnd rusage.stats &&
	grep -q msgrcv rusage.stats &&
	grep -q nsignals rusage.stats &&
	grep -q nvcsw rusage.stats &&
	grep -q nivcsw rusage.stats
'

test_expect_success 'flux module stats --rusage --parse maxrss works' '
	RSS=$(flux module stats --rusage --parse maxrss $REALMOD) &&
	test "$RSS" -gt 0
'

# try to hit some error cases

test_expect_success 'flux module with no arguments prints usage and fails' '
	! flux module 2>noargs.help &&
	grep -q Usage: noargs.help
'

test_expect_success 'flux module -h lists subcommands' '
	! flux module -h 2>module.help &&
	grep -q list module.help &&
	grep -q remove module.help &&
	grep -q reload module.help &&
	grep -q load module.help &&
	grep -q stats module.help &&
	grep -q debug module.help
'

test_expect_success 'flux module load "noexist" fails' '
	! flux module load noexist 2>noexist.out &&
	grep -q "not found" noexist.out
'

test_expect_success 'flux_module_set_running - load test module' '
	run_timeout 10 \
		flux module load ${FLUX_BUILD_DIR}/t/module/.libs/running.so
'
test_expect_success 'flux_module_set_running - signal module to enter reactor' '
	flux event pub running.go
'
test_expect_success 'flux_module_set_running - remove test module' '
	flux module remove running
'
test_expect_success 'broker.module-status rejects malformed request' '
	test_must_fail module_status_bad_proto 2>proto.err &&
	grep "error decoding/finding broker.module-status" proto.err
'
test_expect_success 'broker.module-status rejects request from unknown sender' '
	test_must_fail module_status 2>sender.err &&
	grep "error decoding/finding broker.module-status" sender.err
'

test_done
