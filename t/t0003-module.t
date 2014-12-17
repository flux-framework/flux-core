#!/bin/sh
#

test_description='Test basic module management 

Verify module load/unload/list
'

. `dirname $0`/sharness.sh
test_under_flux 2

test_expect_success 'module: load test module' '
	flux module load ${FLUX_BUILD_DIR}/src/test/module/.libs/parent.so
'

test_expect_success 'module: lsmod shows test module' '
	flux module list | grep parent
'

test_expect_success 'module: cannot load the same module twice' '
	test_must_fail flux module 0 load ${FLUX_BUILD_DIR}/src/test/module/.libs/parent.so
'

test_expect_success 'module: unload test module' '
	flux module remove parent
'

test_expect_success 'module: lsmod does not show test module' '
	! flux module list | grep parent
'

test_expect_success 'module: load test module' '
	flux module load ${FLUX_BUILD_DIR}/src/test/module/.libs/parent.so
'

test_expect_success 'module: load submodule with invalid arguments' '
	test_must_fail flux module load \
		${FLUX_BUILD_DIR}/src/test/module/.libs/child.so \
		wrong argument list
'

test_expect_success 'module: load submodule with valid arguments' '
	flux module load ${FLUX_BUILD_DIR}/src/test/module/.libs/child.so \
		foo=42 bar=abcd
'

test_expect_success 'module: lsmod shows submodule' '
	flux module list parent | grep parent.child
'

test_expect_success 'module: unload submodule' '
	flux module remove parent.child
'

test_expect_success 'module: lsmod does not show submodule' '
	! flux module list parent | grep parent.child
'

test_done
