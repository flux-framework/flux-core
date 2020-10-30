#!/bin/sh
#set -x

test_description='Test basics of flux-hwloc subcommand
'

. `dirname $0`/sharness.sh

SIZE=2
test_under_flux ${SIZE} kvs

HWLOC_DATADIR="${SHARNESS_TEST_SRCDIR}/hwloc-data"
shared2=$(readlink -e ${HWLOC_DATADIR}/1N/shared/02-brokers)
exclu2=$(readlink -e  ${HWLOC_DATADIR}/1N/nonoverlapping/02-brokers)
sierra=$(readlink -e  ${HWLOC_DATADIR}/sierra2)

test_debug '
    echo ${shared} &&
    echo ${exclu2} &&
    echo ${sierra}
'

test_expect_success 'hwloc: load resource module' '
    for i in $(seq 0 $((SIZE-1))); do
        flux exec -r $i flux module load resource
    done
'

#  Set path to lstopo or lstopo-no-graphics command:
#
lstopo=$(which lstopo 2>/dev/null || which lstopo-no-graphics 2>/dev/null)
test -n "$lstopo" && test_set_prereq HAVE_LSTOPO

invalid_rank() {
	echo $((${SIZE} + 1))
}

test_expect_success 'hwloc: ensure we have system lstopo output' '
    flux hwloc topology > system.topology.out &&
    test -f system.topology.out &&
    test -s system.topology.out &&
    test 2 -eq \
         $(grep "<object type=\"Machine\" os_index=\"0\"" system.topology.out | \
           wc -l)
'

test_expect_success 'hwloc: reload non-overlapping set of a node ' '
    flux resource reload --xml $exclu2
'

#  Keep this test after 'reload exclu2' above so we're processing
#   known topology xml from reload.
#
test_expect_success 'hwloc: flux-hwloc info reports expected resources' '
    cat <<-EOF > hwloc-info.expected1 &&
	2 Machines, 16 Cores, 16 PUs
	EOF
    flux hwloc info > hwloc-info.out1 &&
    test_cmp hwloc-info.expected1 hwloc-info.out1
'

test_expect_success 'hwloc: flux-hwloc info -r works' '
    cat <<-EOF >hwloc-info-r.expected &&
	1 Machine, 8 Cores, 8 PUs
	EOF
    flux hwloc info -r 1 > hwloc-info-r.out &&
    test_cmp hwloc-info-r.expected hwloc-info-r.out
'

test_expect_success 'hwloc: reload the same xml of a node' '
    flux resource reload --xml $shared2 &&
    cat <<-EOF > hwloc-info.expected2 &&
	2 Machines, 32 Cores, 32 PUs
	EOF
    flux hwloc info > hwloc-info.out2 &&
    test_cmp hwloc-info.expected2 hwloc-info.out2
'

test_expect_success HAVE_JQ 'hwloc: reload xml with GPU resources' '
    flux resource reload --xml $sierra
'

#  Keep this test after 'reload sierra' above so we're processing
#   known topology xml with GPUs from reload.
#
test_expect_success 'hwloc: flux-hwloc info reports expected GPU resources' '
    cat <<-EOF > hwloc-info.expected3 &&
	2 Machines, 88 Cores, 352 PUs, 8 GPUs
	EOF
    flux hwloc info > hwloc-info.out3 &&
    test_cmp hwloc-info.expected3 hwloc-info.out3
'

test_expect_success 'hwloc: remove resource module' '
    flux exec -r all flux module remove resource
'
test_done
