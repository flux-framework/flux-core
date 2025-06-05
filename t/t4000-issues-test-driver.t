#!/bin/sh
#

test_description='Verify that fixed issues remain fixed'

. `dirname $0`/sharness.sh

if test_have_prereq ASAN; then
    skip_all='skipping issues tests under AddressSanitizer'
    test_done
fi

# Note: use test_under_flux "job" personality so that only 2 cores per fake
# node are configured in the test instance. This ensures a maximum of 4
# issues test scripts (invoked as jobs below) run simultaneously, instead
# of 1 per real core, which could cause test failures on overloaded systems.
#
SIZE=2
test_under_flux ${SIZE} job
echo "# $0: flux session size will be ${SIZE}"

if test -z "$T4000_ISSUES_GLOB"; then
    T4000_ISSUES_GLOB="*"
fi

flux bulksubmit -n1 -o pty --job-name={./%} -t 10m \
	--flags=waitable \
	--quiet --watch  \
	flux start {} \
	::: ${FLUX_SOURCE_DIR}/t/issues/${T4000_ISSUES_GLOB}

for id in $(flux jobs -ano {id}); do
    test_expect_success $(flux jobs -no {name} $id) "flux job attach $id"
done

test_done
