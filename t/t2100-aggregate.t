#!/bin/sh

test_description='Test basic flux aggreagation via aggregator module'

. `dirname $0`/sharness.sh

test_under_flux 8

#  Set path to jq
#
jq=$(which jq 2>/dev/null)
if test -z "$jq"; then
    skip_all='jq not found. Skipping all tests'
    test_done
fi

kvs_json_check() {
    flux kvs get --json $1 | $jq -e "$2"
}

test_expect_success 'have aggregator module' '
    flux module list -r all | grep aggregator
'

test_expect_success 'flux-aggreagate: works' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate -v test 1 &&
    kvs_json_check test ".count == 8 and .min == 1 and .max == 1"
'

test_expect_success 'flux-aggreagate: works for floating-point numbers' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate test 1.825 &&
    kvs_json_check test ".count == 8 and .min == 1.825 and .max == 1.825"
'
test_expect_success 'flux-aggreagate: works for strings' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate test \"foo\" &&
    flux kvs get test &&
    kvs_json_check test ".count == 8" &&
    kvs_json_check test ".entries.\"[0-7]\" == \"foo\""
'
test_expect_success 'flux-aggreagate: works for arrays' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate test "[7,8,9]" &&
    flux kvs get test &&
    kvs_json_check test ".count == 8" &&
    kvs_json_check test "(.entries | length) == 1" &&
    kvs_json_check test ".entries.\"[0-7]\" == [7,8,9]"
'
test_expect_success 'flux-aggreagate: works for objects' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate test \
                  "{\"foo\":42, \"bar\": {\"baz\": 2}}" &&
    flux kvs get test &&
    kvs_json_check test ".count == 8" &&
    kvs_json_check test ".entries.\"[0-7]\".foo == 42" &&
    kvs_json_check test ".entries.\"[0-7]\".bar.baz == 2"
'
test_expect_success 'flux-aggregate: abort works' '
    test_expect_code 1 run_timeout 5 flux exec -n -r 0-7 flux aggregate . 1
'

test_expect_success 'flux-aggregate: different value per rank' '
    run_timeout 2 flux exec -n -r 0-7 bash -c "flux aggregate test \$(flux getattr rank)" &&
    kvs_json_check test ".count == 8 and .min == 0 and .max == 7"
'

test_expect_success 'flux-aggregate: different fp value per rank' '
    run_timeout 2 flux exec -n -r 0-7 bash -c "flux aggregate test 1.\$(flux getattr rank)" &&
    kvs_json_check test ".count == 8 and .min == 1 and .max == 1.7"
'

test_expect_success 'flux-aggregate: --timeout=0. - immediate forward' '
    run_timeout 2 flux exec -n -r 0-7 flux aggregate -t 0. test 1 &&
    kvs_json_check test \
        ".count == 8 and .total == 8 and .min == 1 and .max == 1"
'

test_expect_success 'flux-aggregate: --fwd-count works' '
    run_timeout 2 flux exec -n -r 0-7 bash -c \
     "flux aggregate -t10 -c \$((1+\$(flux getattr tbon.descendants))) test 1" &&
    kvs_json_check test \
        ".count == 8 and .total == 8 and .min == 1 and .max == 1"
'

test_done

# vi: ts=4 sw=4 expandtab

