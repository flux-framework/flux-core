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

test_expect_success 'module: list fails on invalid rank' '
	flux module list -r $(invalid_rank) 2> stderr
	grep "No route to host" stderr
'

test_expect_success 'module: load fails on invalid rank' '
	flux module load -r $(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so 2> stderr
	grep "No route to host" stderr
'

test_expect_success 'module: remove fails on invalid rank' '
	flux module load \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
	flux module remove -r $(invalid_rank) parent 2> stderr
	flux module remove parent
	grep "No route to host" stderr
'

test_expect_success 'module: load works on valid and invalid rank' '
	flux module load -r 0,$(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so 1> stdout 2> stderr
	flux module list -r 0 | grep parent &&
	grep "No route to host" stderr
'

test_expect_success 'module: list works on valid and invalid rank' '
	flux module list -r 0,$(invalid_rank) 1> stdout 2> stderr
	grep "parent" stdout &&
	grep "No route to host" stderr
'

test_expect_success 'module: remove works on valid and invalid rank' '
	flux module load -r 0,$(invalid_rank) \
		${FLUX_BUILD_DIR}/t/module/.libs/parent.so
	flux module remove -r 0,$(invalid_rank) parent 2> stderr
	! flux module list -r 0 | grep parent &&
	grep "No route to host" stderr
'

test_expect_success 'module: load fails on invalid module' '
	flux module load nosuchmodule 2> stderr
	grep "nosuchmodule: not found in module search path" stderr
'

test_expect_success 'module: remove fails on invalid module' '
	flux module remove nosuchmodule 2> stderr
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

test_done
