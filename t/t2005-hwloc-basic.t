#!/bin/sh
#set -x

test_description='Test basics of flux-hwloc reload subcommand

Ensure flux-hwloc reload subcommand works
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

test_expect_success 'hwloc: load aggregator module' '
    flux module load -r all aggregator
'
test_expect_success 'hwloc: load hwloc xml' '
    flux hwloc reload -v
'

#  Set path to lstopo or lstopo-no-graphics command:
#
lstopo=$(which lstopo 2>/dev/null || which lstopo-no-graphics 2>/dev/null)
test -n "$lstopo" && test_set_prereq HAVE_LSTOPO

#  Set path to jq
#
jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ

invalid_rank() {
	echo $((${SIZE} + 1))
}

test_expect_success 'hwloc: ensure we have system lstopo output' '
    flux hwloc topology > system.xml &&
    test -f system.xml &&
    test -s system.xml &&
    grep "<object type=\"System\" os_index=\"0\">" system.xml
'

test_expect_success 'hwloc: each rank reloads a non-overlapping set of a node ' '
    flux hwloc reload $exclu2
'

test_expect_success 'hwloc: internal aggregate-load cmd works' '
    flux exec -r all \
        flux hwloc aggregate-load --key=foo --unpack=bar --print-result | \
	$jq -S . >aggregate.out &&
    test_debug "flux kvs get bar" &&
    cat <<-EOF | $jq -S . >aggregate.expected &&
	{ "count": 2, "total": 2,
	  "entries": {
	    "0": { "Core": 8, "NUMANode": 1, "PU": 8, "Package": 1,
                   "cpuset": "0-7" },
	    "1": { "Core": 8, "NUMANode": 1, "PU": 8, "Package": 1,
	           "cpuset": "8-15"}
	  }
	}
	EOF
    test_cmp aggregate.expected aggregate.out
'


test_expect_success HAVE_JQ 'hwloc: by_rank aggregate key exists after reload' '
    flux kvs get resource.hwloc.by_rank | $jq -S . >  by_rank.out &&
    cat <<-EOF | $jq -S . >by_rank.expected &&
	{
	  "0": { "Core": 8, "NUMANode": 1, "PU": 8, "Package": 1,
                 "cpuset": "0-7" },
	  "1": { "Core": 8, "NUMANode": 1, "PU": 8, "Package": 1,
	         "cpuset": "8-15"}
	}
	EOF
    test_cmp by_rank.expected by_rank.out
'

#  Keep this test after 'reload exclu2' above so we're processing
#   know topology xml from reload.
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

test_expect_success 'hwloc: every rank reloads the same xml of a node' '
    flux hwloc reload $shared2 &&
    cat <<-EOF > hwloc-info.expected2 &&
	2 Machines, 32 Cores, 32 PUs
	EOF
    flux hwloc info > hwloc-info.out2 &&
    test_cmp hwloc-info.expected2 hwloc-info.out2
'

test_expect_success HAVE_JQ 'hwloc: only one rank reloads an xml file' '
    flux hwloc reload --rank="[0]" $exclu2 &&
    flux kvs get resource.hwloc.by_rank | $jq -S . > mixed.out &&
    cat <<-EOF | $jq -S . >mixed.expected &&
	{
	 "0": {"NUMANode": 1, "Package": 1, "Core": 8, "PU": 8,
               "cpuset": "0-7" },
	 "1": {"NUMANode": 2, "Package": 2, "Core": 16, "PU": 16,
	       "cpuset": "0-15" }
	}
	EOF
    test_cmp mixed.expected mixed.out
'

test_expect_success HAVE_JQ 'hwloc: reload xml with GPU resources' '
    flux hwloc reload --rank=all $sierra &&
    flux kvs get resource.hwloc.by_rank | $jq -S . > sierra.out &&
    cat <<-EOF | $jq -S . > sierra.expected &&
	{"[0-1]":
          {"NUMANode": 6, "Package": 2, "Core": 44, "PU": 176, "GPU": 4,
           "cpuset": "0-175" }
        }
	EOF
    test_cmp sierra.expected sierra.out
'

test_expect_success 'hwloc: return an error code on an invalid DIR' '
    test_expect_code 1 flux hwloc reload nonexistence
'

test_expect_success 'hwloc: return an error code on valid DIR, invalid files' '
    test_expect_code 1 flux hwloc reload /
'

test_expect_success 'hwloc: reload with invalid rank fails' '
    test_expect_code 1 flux hwloc reload -r $(invalid_rank) &&
    test_expect_code 1 flux hwloc reload -r "0-$(invalid_rank)" &&
    test_expect_code 1 flux hwloc reload -r foo
'

test_expect_success HAVE_LSTOPO 'hwloc: lstopo works' '
    flux hwloc reload $exclu2 &&
    flux hwloc lstopo > lstopo.out1 &&
    sed -n 1p lstopo.out1 | grep "^System (32G.*"
'

test_expect_success HAVE_LSTOPO 'hwloc: lstopo subcommand passes options to lstopo' '
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
    flux hwloc topology > system.out4 &&
    test_cmp system.xml system.out4
'

test_expect_success HAVE_LSTOPO 'hwloc: test failure of lstopo command' '
    test_must_fail flux hwloc lstopo --input f:g:y
'

test_expect_success 'hwloc: unload aggregator' '
    flux module remove -r all aggregator
'

test_done
