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

test_expect_success 'hwloc: each rank reloads a non-overlapping set of a node ' '
    flux hwloc reload $exclu2
'

test_expect_success 'hwloc: every rank reloads the same xml of a node' '
    flux hwloc reload $shared2
'

test_expect_success 'hwloc: only one rank reloads an xml file' '
    flux hwloc reload --ranks="[0]" $exclu2
'

test_expect_success 'hwloc: return an error code on an invalid DIR' '
    test_expect_code 1 flux hwloc reload nonexistence
'

test_done
