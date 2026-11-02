#!/bin/sh

test_description='Test FLUX_HWLOC_XMLFILE with cpu-affinity=dry-run'

test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug

. `dirname $0`/sharness.sh

XMLFILE=${SHARNESS_TEST_SRCDIR}/hwloc-data/sierra.xml

# sierra.xml: 44 cores, 4 PUs per core
SIERRA_CORES=44

test_expect_success 'hwloc XML file exists' '
	test -f ${XMLFILE}
'

test_expect_success 'FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM allows non-local topology to load' '
	FLUX_HWLOC_XMLFILE=${XMLFILE} \
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start flux resource list -no {ncores} >cores.out &&
	test_debug "cat cores.out" &&
	test $(cat cores.out) -eq ${SIERRA_CORES}
'
test_expect_success 'FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM is ignored without FLUX_HWLOC_XMLFILE' '
	flux start flux resource list -no {ncores} > local-cores.exp &&
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start flux resource list -no {ncores} >local-cores.out &&
	test_debug "cat local-cores.out" &&
	test_cmp local-cores.exp local-cores.out
'
test_expect_success 'cpu-affinity works with FLUX_HWLOC_XMLFILE' '
	FLUX_HWLOC_XMLFILE=${XMLFILE} \
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start flux run -n1 -c1 -o cpu-affinity=dry-run true \
		> dry-run1.out 2>&1 &&
	test_debug "cat dry-run1.out" &&
	grep "cpus: 0-3" dry-run1.out
'
test_expect_success 'cpu-affinity=per-task works with FLUX_HWLOC_XMLFILE' '
	FLUX_HWLOC_XMLFILE=${XMLFILE} \
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start flux run -n2 -c1 -o cpu-affinity=dry-run,per-task true \
		> dry-run2.out 2>&1 &&
	test_debug "cat dry-run2.out" &&
	grep "task 0: cpus: 0-3" dry-run2.out &&
	grep "task 1: cpus: 4-7" dry-run2.out
'

# Pin per-task cpusets on a real topology so that changes to the per-task
# distribution, with or without slots in R, are detected.
test_expect_success 'cpu-affinity=per-task without slots is unchanged' '
	FLUX_HWLOC_XMLFILE=${XMLFILE} \
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start sh -c "
	    flux run -n5 -o cpu-affinity=dry-run,per-task true &&
	    flux run -n3 -c5 -o cpu-affinity=dry-run,per-task true" \
		2>&1 | sed -n "s/^.*\(task [0-9]*: cpus: .*\)$/\1/p" \
		>noslots.out &&
	test_debug "cat noslots.out" &&
	cat <<-EOF >noslots.expected &&
	task 0: cpus: 0-3
	task 1: cpus: 4-7
	task 2: cpus: 8-11
	task 3: cpus: 12-15
	task 4: cpus: 16-19
	task 0: cpus: 0-19
	task 1: cpus: 20-39
	task 2: cpus: 40-59
	EOF
	test_cmp noslots.expected noslots.out
'
test_expect_success 'create R with slots and test script' '
	flux R encode -r 0 -c 0-7 | jq -c ".execution.R_lite[0].slots = {
		\"a\": {\"count\": 1, \"core\": \"0-1\"},
		\"b\": {\"count\": 2, \"core\": \"2-7\"}}" >R.slots &&
	cat <<-EOF >slots.sh
	flux jobtap load alloc-bypass.so &&
	flux run -n3 --setattr=system.alloc-bypass.R="\$(cat R.slots)" \
	    -o cpu-affinity=dry-run,per-task true &&
	flux run --dry-run -n6 \
	    --setattr=system.alloc-bypass.R="\$(cat R.slots)" \
	    -o cpu-affinity=dry-run,per-task true | \
	    jq ".tasks[0].count = {\"total\": 6}" >j6.json &&
	flux job attach \$(flux job submit j6.json) &&
	flux run --dry-run -n2 \
	    --setattr=system.alloc-bypass.R="\$(cat R.slots)" \
	    -o cpu-affinity=dry-run,per-task true | \
	    jq ".resources[0].label = \"b\" | .tasks[0].slot = \"b\"" \
	    >jb.json &&
	flux job attach \$(flux job submit jb.json) &&
	flux run -n3 --setattr=system.alloc-bypass.R="\$(cat R.slots)" \
	    -o cpu-affinity=dry-run true
	EOF
'
test_expect_success 'cpu-affinity=per-task binds tasks to slots in R' '
	FLUX_HWLOC_XMLFILE=${XMLFILE} \
	FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM=1 \
	flux start sh ./slots.sh 2>&1 | \
		grep -o "\(task [0-9]*: \)*cpus: .*" >slots.out &&
	test_debug "cat slots.out" &&
	cat <<-EOF >slots.expected &&
	cpus: 0-31
	task 0: cpus: 0-7
	task 1: cpus: 8-19
	task 2: cpus: 20-31
	cpus: 0-31
	task 0: cpus: 0-3
	task 1: cpus: 8-11
	task 2: cpus: 20-23
	task 3: cpus: 4-7
	task 4: cpus: 12-19
	task 5: cpus: 24-31
	cpus: 0-31
	task 0: cpus: 8-19
	task 1: cpus: 20-31
	cpus: 0-31
	EOF
	test_cmp slots.expected slots.out
'
test_done

