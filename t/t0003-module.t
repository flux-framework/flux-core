#!/bin/sh
#

test_description='Test basic module management 

Verify module load/unload/list
'

. `dirname $0`/sharness.sh
test_under_flux 4

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

test_done
