#!/bin/sh

test_description='flux-resource list tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Use a static format to avoid breaking output if default flux-resource list
#  format ever changes
FORMAT="{state:>10} {nnodes:>6} {ncores:>8} {ngpus:>8}"

for input in ${SHARNESS_TEST_SRCDIR}/resource-status/*.json; do
    name=$(basename ${input%%.json})
    test_expect_success "flux-resource list input check: $name" '
        base=${input%%.json} &&
        expected=${base}.expected &&
        name=$(basename $base) &&
        flux resource list -o "$FORMAT" \
            --from-stdin < $input > $name.output 2>&1 &&
        test_debug "cat $name.output" &&
        test_cmp $expected $name.output
    '
done

test_done
