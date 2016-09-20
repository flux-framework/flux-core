#!/bin/sh

test_description='Test basic flux aggreagation via aggregator module'

. `dirname $0`/sharness.sh

test_under_flux 8 wreck

kvscheck="$SHARNESS_TEST_SRCDIR/scripts/kvs-get-ex.lua"

test_expect_success 'have aggregator module' '
    flux module list -r all | grep aggregator
'

test_expect_success 'flux-aggreagate: works' '
    run_timeout 2 flux exec -r 0-7 flux aggregate test 1 &&
    $kvscheck test "x.count == 8"
'

test_expect_success 'flux-aggregate: abort works' '
    test_expect_code 1 run_timeout 5 flux exec -r 0-7 flux aggregate . 1
'

test_expect_success 'flux-aggregate: different value per rank' '
    run_timeout 2 flux exec -r 0-7 bash -c "flux aggregate test \$(flux getattr rank)" &&
    $kvstest test "x.count == 8" &&
    $kvstest test "x.min == 0" &&
    $kvstest test "x.max == 7"
'

test_expect_success 'flux-aggregate: --timeout=0. - immediate forward' '
    run_timeout 2 flux exec -r 0-7 flux aggregate -t 0. test 1 &&
    $kvstest test "x.count == 8" &&
    $kvstest test "x.total == 8" &&
    $kvstest test "x.min == 1" &&
    $kvstest test "x.max == 1"
'

test_expect_success 'flux-aggregate: --fwd-count works' '
    run_timeout 2 flux exec -r 0-7 bash -c \
     "flux aggregate -t10 -c \$((1+\$(flux getattr tbon.descendants))) test 1" &&
    $kvstest test "x.count == 8" &&
    $kvstest test "x.total == 8" &&
    $kvstest test "x.min == 1" &&
    $kvstest test "x.max == 1"
'


test_done

# vi: ts=4 sw=4 expandtab

