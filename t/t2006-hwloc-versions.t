#!/bin/sh
#set -x

test_description='Test flux-hwloc with various versions of XML output

Ensure flux-hwloc reload works with XML produced by hwloc by v1 and v2
'

. `dirname $0`/sharness.sh

SIZE=1
test_under_flux ${SIZE} kvs

HWLOC_VERSION=$(${SHARNESS_TEST_SRCDIR}/hwloc/hwloc-version)
[ $HWLOC_VERSION -eq 2 ] && test_set_prereq HWLOC2

HWLOC_DATADIR="${SHARNESS_TEST_SRCDIR}/hwloc-data"
hwloc1_11=$(readlink -e  ${HWLOC_DATADIR}/1N/hwloc-versions/v1.11.11)
hwloc2_1=$(readlink -e  ${HWLOC_DATADIR}/1N/hwloc-versions/v2.1.0)
hwloc2to1=$(readlink -e  ${HWLOC_DATADIR}/1N/hwloc-versions/v2to1)

test_debug '
    echo ${hwloc1_11} &&
    echo ${hwloc2_1} &&
    echo ${hwloc2to1} &&
    echo ${HWLOC_VERSION}
'

test_expect_success 'hwloc: load aggregator module' '
    flux exec -r all flux module load aggregator
'
test_expect_success 'hwloc: load hwloc xml' '
    flux hwloc reload -v
'

test_expect_success 'hwloc: loading v1.11 xml works' '
    flux hwloc reload $hwloc1_11 &&
    cat <<-EOF > hwloc-info.expected3 &&
	1 Machine, 48 Cores, 96 PUs
	EOF
    flux hwloc info > hwloc-info.out3 &&
    test_cmp hwloc-info.expected3 hwloc-info.out3
'

test_expect_success HWLOC2 'hwloc: loading v2.1 xml works' '
    flux hwloc reload $hwloc2_1 &&
    cat <<-EOF > hwloc-info.expected4 &&
	1 Machine, 48 Cores, 96 PUs
	EOF
    flux hwloc info > hwloc-info.out4 &&
    test_cmp hwloc-info.expected4 hwloc-info.out4
'

test_expect_success 'hwloc: loading xml converted to v1 works' '
    flux hwloc reload $hwloc2to1 &&
    cat <<-EOF > hwloc-info.expected5 &&
	1 Machine, 48 Cores, 96 PUs
	EOF
    flux hwloc info > hwloc-info.out5 &&
    test_cmp hwloc-info.expected5 hwloc-info.out5
'

test_expect_success 'hwloc: unload aggregator' '
    flux exec -r all flux module remove aggregator
'

test_done
