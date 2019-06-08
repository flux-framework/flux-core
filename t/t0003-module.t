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

test_expect_success 'module: load test module' '
	flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module: lsmod shows test module' '
	flux module list | grep parent
'

test_expect_success 'module: cannot load the same module twice' '
	test_must_fail flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module: unload test module' '
	flux module remove parent
'

test_expect_success 'module: lsmod does not show test module' '
	! flux module list | grep parent
'

test_expect_success 'module API: load test module' '
	flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module API: lsmod shows test module' '
	flux module list | grep parent
'

test_expect_success 'module API: cannot load the same module twice' '
        test_must_fail flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module API: unload test module' '
	flux module remove parent
'

test_expect_success 'module API: lsmod does not show test module' '
	! flux module list | grep parent
'

test_expect_success 'module: load test module (all ranks)' '
	flux module load -r all \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module: load submodule with invalid args (this rank)' '
	test_must_fail flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/child.so \
		wrong argument list
'

test_expect_success 'module: load submodule (all ranks)' '
	flux module load -r all \
		${FLUX_BUILD_DIR}/t/module/.libs/child.so \
		foo=42 bar=abcd
'

test_expect_success 'module: lsmod shows submodule (all ranks)' '
	flux module list -r all parent | grep parent.child
'

#
# Expected 'lsmod -r all parent' output is something like this:
#  parent.child         1113944 2517539    0  I         3 rank3,test1,test2
#  parent.child         1113944 2517539    0  I         0 rank0,test1,test2
#  parent.child         1113944 2517539    0  I         2 rank2,test1,test2
#  parent.child         1113944 2517539    0  I         1 rank1,test1,test2
#
test_expect_success 'module: lsmod -r all parent works' '
	flux module list -r all parent | grep child >child.lsmod.out
'
test_expect_success 'module: hardwired test1,test2 services are listed in sorted order' '
	grep -q test1,test2 child.lsmod.out
'
test_expect_success "module: hardwired rankN services are listed" '
	for rank in $(seq 0 $(($SIZE-1))); do \
		grep -q rank${rank} child.lsmod.out; \
	done
'
test_expect_success "module: there are size=$SIZE lines of output due to unique rankN service" '
	test $(wc -l < child.lsmod.out) -eq $SIZE
'

test_expect_success 'module: unload submodule (all ranks)' '
	flux module remove -r all parent.child
'

test_expect_success 'module: lsmod does not show submodule (all ranks)' '
	! flux module list -r all parent | grep parent.child
'

test_expect_success 'module: unload test module (all ranks)' '
	flux module remove -r all parent
'

test_expect_success 'module: insmod returns initialization error' '
	test_must_fail flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so --init-failure
'

test_expect_success 'module: list works with exclusion' '
	flux module list -r all -x [0-1] >listx.out &&
	grep -q [2-3] listx.out
'

test_expect_success 'module: list fails on invalid rank' '
	flux module list -r $(invalid_rank) 2> stderr &&
	grep "No route to host" stderr
'

test_expect_success 'module: load fails on invalid rank' '
	test_must_fail flux module load -r $(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so 2> stderr &&
	grep "No route to host" stderr
'

test_expect_success 'module: remove fails on invalid rank' '
	flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so &&
	flux module remove -r $(invalid_rank) parent 2> stderr &&
	flux module remove parent &&
	grep "No route to host" stderr
'

test_expect_success 'module: load works on valid and invalid rank' '
	test_must_fail flux module load -r 0,$(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so >stdout 2>stderr &&
	flux module list -r 0 | grep parent &&
	grep "No route to host" stderr
'

test_expect_success 'module: list works on valid and invalid rank' '
	flux module list -r 0,$(invalid_rank) 1> stdout 2> stderr &&
	grep "parent" stdout &&
	grep "No route to host" stderr
'

test_expect_success 'module: remove works on valid and invalid rank' '
	! flux module load -r 0,$(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so &&
	flux module remove -r 0,$(invalid_rank) parent 2> stderr &&
	! flux module list -r 0 | grep parent &&
	grep "No route to host" stderr
'

test_expect_success 'module: load fails on invalid module' '
	! flux module load nosuchmodule 2> stderr &&
	grep "nosuchmodule: not found in module search path" stderr
'

test_expect_success 'module: remove fails on invalid module' '
	flux module remove nosuchmodule 2> stderr &&
	grep "nosuchmodule: No such file or directory" stderr
'

test_expect_success 'module: info works' '
	flux module info ${FLUX_BUILD_DIR}/t/module/.libs/parent.so
'

test_expect_success 'module: info fails on invalid module' '
	! flux module info nosuchmodule
'

# N.B. avoid setting the actual debug bits - lets reserve LSB
TESTMOD=connector-local

test_expect_success 'flux module debug gets debug flags' '
	FLAGS=$(flux module debug $TESTMOD) &&
	test "$FLAGS" = "0x0"
'
test_expect_success 'flux module debug --setbit sets individual debug flags' '
	flux module debug --setbit 0x10000 $TESTMOD &&
	FLAGS=$(flux module debug $TESTMOD) &&
	test "$FLAGS" = "0x10000"
'
test_expect_success 'flux module debug --set replaces debug flags' '
	flux module debug --set 0xff00 $TESTMOD &&
	FLAGS=$(flux module debug $TESTMOD) &&
	test "$FLAGS" = "0xff00"
'
test_expect_success 'flux module debug --clearbit clears individual debug flags' '
	flux module debug --clearbit 0x1000 $TESTMOD &&
	FLAGS=$(flux module debug $TESTMOD) &&
	test "$FLAGS" = "0xef00"
'
test_expect_success 'flux module debug --clear clears debug flags' '
	flux module debug --clear $TESTMOD &&
	FLAGS=$(flux module debug $TESTMOD) &&
	test "$FLAGS" = "0x0"
'

# test stats

test_expect_success 'flux module stats gets comms statistics' '
	flux module stats $TESTMOD >comms.stats &&
	grep -q "#request (tx)" comms.stats &&
	grep -q "#request (rx)" comms.stats &&
	grep -q "#response (tx)" comms.stats &&
	grep -q "#response (rx)" comms.stats &&
	grep -q "#event (tx)" comms.stats &&
	grep -q "#event (rx)" comms.stats &&
	grep -q "#keepalive (tx)" comms.stats &&
	grep -q "#keepalive (rx)" comms.stats
'

test_expect_success 'flux module stats --parse "#event (tx)" counts events' '
	EVENT_TX=$(flux module stats --parse "#event (tx)" $TESTMOD) &&
	flux event pub xyz &&
	EVENT_TX2=$(flux module stats --parse "#event (tx)" $TESTMOD) &&
	test "$EVENT_TX" = $((${EVENT_TX2}-1))
'

test_expect_success 'flux module stats --clear works' '
	flux event pub xyz &&
	flux module stats --clear $TESTMOD &&
	EVENT_TX2=$(flux module stats --parse "#event (tx)" $TESTMOD) &&
	test "$EVENT_TX" = 0
'

test_expect_success 'flux module stats --clear-all works' '
	flux event pub xyz &&
	flux module stats --clear-all $TESTMOD &&
	EVENT_TX2=$(flux module stats --parse "#event (tx)" $TESTMOD) &&
	test "$EVENT_TX" = 0
'

test_expect_success 'flux module stats --scale works' '
	flux event pub xyz &&
	EVENT_TX=$(flux module stats --parse "#event (tx)" $TESTMOD) &&
	EVENT_TX2=$(flux module stats --parse "#event (tx)" --scale=2 $TESTMOD) &&
	test "$EVENT_TX2" -eq $((${EVENT_TX}*2))
'

test_expect_success 'flux module stats --rusage works' '
	flux module stats --rusage $TESTMOD >rusage.stats &&
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
	RSS=$(flux module stats --rusage --parse maxrss $TESTMOD) &&
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
	grep -q load module.help &&
	grep -q info module.help &&
	grep -q stats module.help &&
	grep -q debug module.help
'

test_expect_success 'flux module load "noexist" fails' '
	! flux module load noexist 2>noexist.out &&
	grep -q "not found" noexist.out
'

test_expect_success 'flux module detects bad nodeset' '
	! flux module load -r smurf kvs 2>badns-load.out &&
	grep -q "could not parse" badns-load.out &&
	! flux module list -x smurf 2>badns-list.out &&
	grep -q "could not parse" badns-list.out
'



test_done
