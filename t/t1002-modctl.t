#!/bin/sh
#

test_description='Test distributed module management 

Verify distributed module load/unload/list with modctl
'

. `dirname $0`/sharness.sh
test_under_flux 3

test_expect_success 'modctl: load test module on all ranks' '
	flux module load --rank=all \
		${FLUX_BUILD_DIR}/src/test/module/.libs/parent.so
'

test_expect_success 'modctl: lsmod shows test module on all ranks' '
	flux module list --rank=all | grep parent | grep '[0-2]'
'

test_expect_success 'module: unload test module on all ranks' '
	flux module remove --rank=all parent
'

test_expect_success 'module: lsmod does not show test module on any ranks' '
	! flux module list --rank=all | grep parent
'

test_expect_success 'module: load test module on all ranks' '
	flux module load --rank=all \
		${FLUX_BUILD_DIR}/src/test/module/.libs/parent.so
'

test_expect_success 'module: load submodule on all ranks' '
	flux module load --rank=all \
		${FLUX_BUILD_DIR}/src/test/module/.libs/child.so \
		foo=42 bar=abcd
'

test_expect_success 'module: lsmod shows submodule on all ranks' '
	flux module list --rank=all parent | grep parent.child | grep '[0-2]'
'

test_expect_success 'module: unload submodule on all ranks' '
	flux module remove --rank=all parent.child
'

test_expect_success 'module: lsmod does not show submodule on any ranks' '
	! flux module list --rank=all parent | grep parent.child
'

test_done
