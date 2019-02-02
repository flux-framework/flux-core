#!/bin/sh
#

test_description='Verify that fixed issues remain fixed'

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

for testscript in ${FLUX_SOURCE_DIR}/t/issues/*; do
    testname=`basename $testscript`
    test_expect_success  $testname $testscript
done

test_done
