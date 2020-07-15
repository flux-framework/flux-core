#!/bin/sh

test_description='flux-resource list tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

for input in ${SHARNESS_TEST_SRCDIR}/resource-status/*.json; do
    name=$(basename ${input%%.json})
    test_expect_success "flux-resource list input check: $name" '
        base=${input%%.json} &&
        expected=${base}.expected &&
        name=$(basename $base) &&
        flux resource list --from-stdin < $input > $name.output 2>&1 &&
        test_cmp $expected $name.output
    '
done

test_done
