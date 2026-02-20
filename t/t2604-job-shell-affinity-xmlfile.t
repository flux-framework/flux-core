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
test_done

