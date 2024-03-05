#!/bin/sh

test_description='flux-resource watch tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# below, we will kill a broker in tests.  So we want to use system
# personality.  System personality sets exit mode = leader, ensuring
# flux start does not exit with error
test_under_flux 4 system

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

# wait for count of 2 for all.out, we can't be guaranteed exact number
# of lines, but we know atleast 2 because of rank 0 and rank > 0.
test_expect_success NO_CHAIN_LINT 'flux-resource watch initial setup works' '
        flux resource watch > watch.out &
        pid=$!
        echo $pid > watch.pid
        flux resource watch --all > all.out &
        pid=$!
        echo $pid > all.pid
        flux resource watch --ranks > ranks.out &
        pid=$!
        echo $pid > ranks.pid
        $waitfile --count=2 --timeout=60 --pattern=online all.out &&
        test_must_fail grep online watch.out &&
        test_must_fail grep offline watch.out &&
        test_must_fail grep drain watch.out &&
        test_must_fail grep offline all.out &&
        test_must_fail grep drain all.out &&
        test_must_fail grep online ranks.out &&
        test_must_fail grep offline ranks.out &&
        test_must_fail grep drain ranks.out
'
test_expect_success 'take down a broker' '
        flux overlay disconnect 3
'
test_expect_success NO_CHAIN_LINT 'flux-resource watch for offline' '
        $waitfile --count=1 --timeout=60 --pattern=offline watch.out &&
        $waitfile --count=1 --timeout=60 --pattern=offline all.out &&
        $waitfile --count=1 --timeout=60 --pattern="offline 3" ranks.out
'
test_expect_success 'drain a rank' '
        flux resource drain 2
'
test_expect_success NO_CHAIN_LINT 'flux-resource watch for drain' '
        $waitfile --count=1 --timeout=60 --pattern=drain watch.out &&
        $waitfile --count=1 --timeout=60 --pattern=drain all.out &&
        $waitfile --count=1 --timeout=60 --pattern="drain   2" ranks.out
'
test_expect_success 'undrain a rank' '
        flux resource undrain 2
'
test_expect_success NO_CHAIN_LINT 'flux-resource watch for undrain' '
        $waitfile --count=1 --timeout=60 --pattern=undrain watch.out &&
        $waitfile --count=1 --timeout=60 --pattern=undrain all.out &&
        $waitfile --count=1 --timeout=60 --pattern="undrain 2" ranks.out
'
test_expect_success NO_CHAIN_LINT 'cleanup watches' '
        kill $(cat watch.pid) &&
        kill $(cat all.pid) &&
        kill $(cat ranks.pid) &&
        ! wait $(cat watch.pid) &&
        ! wait $(cat all.pid) &&
        ! wait $(cat ranks.pid)
'
test_done
