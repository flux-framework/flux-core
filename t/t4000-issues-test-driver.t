#!/bin/sh
#

test_description='Verify that fixed issues remain fixed'

. `dirname $0`/sharness.sh

if test_have_prereq ASAN; then
    skip_all='skipping issues tests under AddressSanitizer'
    test_done
fi

SIZE=4
test_under_flux ${SIZE}
echo "# $0: flux session size will be ${SIZE}"

for testscript in ${FLUX_SOURCE_DIR}/t/issues/*; do
    testname=`basename $testscript`
    test_expect_success  $testname "run_timeout 30 $testscript"
done

test_done
