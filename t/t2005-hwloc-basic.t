#!/bin/sh
#set -x

test_description='Test basics of flux-hwloc reload subcommand

Ensure flux-hwloc reload subcommand works
'

. `dirname $0`/sharness.sh

test_under_flux 2

shared2=`readlink -e ${SHARNESS_TEST_SRCDIR}/hwloc-data/1N/shared/02-brokers`
exclu2=`readlink -e ${SHARNESS_TEST_SRCDIR}/hwloc-data/1N/nonoverlapping/02-brokers`

test_debug '
    echo ${dn} &&
    echo ${shared} &&
    echo ${exclu2}
'

#
#  Ensure resource-hwloc is initialized here. There is currently no
#   way to synchronize, so we run `flux hwloc lstopo` repeatedly
#   until it succeeds.
#
count=0
while ! flux hwloc lstopo > lstopo.system; do
   sleep 0.5
   count=$(expr $count + 1)
   test $count -eq 5 && break
done

#  Set path to lstopo or lstopo-no-graphics command:
#
lstopo=$(which lstopo 2>/dev/null || which lstopo-no-graphics 2>/dev/null)
test -n "$lstopo" && test_set_prereq HAVE_LSTOPO

test_expect_success 'hwloc: ensure we have system lstopo output' '
    test -f lstopo.system &&
    test -s lstopo.system &&
    head -1 lstopo.system | grep ^System
'

test_expect_success 'hwloc: each rank reloads a non-overlapping set of a node ' '
    flux hwloc reload $exclu2
'

test_expect_success 'hwloc: every rank reloads the same xml of a node' '
    flux hwloc reload $shared2
'

test_expect_success 'hwloc: only one rank reloads an xml file' '
    flux hwloc reload --rank="[0]" $exclu2
'

test_expect_success 'hwloc: return an error code on an invalid DIR' '
    test_expect_code 1 flux hwloc reload nonexistence
'

test_expect_success 'hwloc: return an error code on valid DIR, invalid files' '
    test_expect_code 1 flux hwloc reload /
'

test_expect_success 'hwloc: lstopo works' '
    flux hwloc reload $exclu2 &&
    flux hwloc lstopo > lstopo.out1 &&
    sed -n 1p lstopo.out1 | grep "^System (32G.*"
'

test_expect_success 'hwloc: lstopo subcommand passes options to lstopo' '
    flux hwloc lstopo --help | grep ^Usage
'

test_expect_success HAVE_LSTOPO 'hwloc: topology subcommand works' '
    flux hwloc topology > topology.out2 &&
    flux hwloc lstopo > lstopo.out2 &&
    $lstopo --if xml -i topology.out2 --of console > lstopo.out3 &&
    test_cmp lstopo.out2 lstopo.out3
'

test_expect_success 'hwloc: reload with no args reloads system topology' '
    flux hwloc reload &&
    flux hwloc lstopo > lstopo.out4 &&
    test_cmp lstopo.system lstopo.out4
'

test_expect_success 'hwloc: test failure of lstopo command' '
    test_must_fail flux hwloc lstopo --input f:g:y
'

test_done
