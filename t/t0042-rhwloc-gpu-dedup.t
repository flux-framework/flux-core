#!/bin/sh
#
test_description='Test GPU deduplication in rhwloc'

. `dirname $0`/sharness.sh

XMLDIR=${SHARNESS_TEST_SRCDIR}/hwloc-data

test_expect_success 'NVIDIA system with multi-backend deduplication' '
	test -f ${XMLDIR}/ipa20.xml &&
	count=$(flux R encode --xml=${XMLDIR}/ipa20.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo NVIDIA ipa20: $count GPUs" &&
	test "$count" = "4"
'

test_expect_success 'AMD system in SPX mode (4 GPUs baseline)' '
	test -f ${XMLDIR}/tuo.SPX.xml &&
	count=$(flux R encode --xml=${XMLDIR}/tuo.SPX.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo AMD SPX mode: $count GPUs" &&
	test "$count" = "4"
'

test_expect_success 'AMD system in CPX mode (24 partitioned GPUs from 4 physical)' '
	test -f ${XMLDIR}/tuo.CPX.xml &&
	count=$(flux R encode --xml=${XMLDIR}/tuo.CPX.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo AMD CPX mode: $count GPUs" &&
	test "$count" = "24"
'

test_expect_success 'AMD system in TPX mode (12 partitioned GPUs from 4 physical)' '
	test -f ${XMLDIR}/tuo.TPX.xml &&
	count=$(flux R encode --xml=${XMLDIR}/tuo.TPX.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo AMD TPX mode: $count GPUs" &&
	test "$count" = "12"
'

test_expect_success 'FLUX_HWLOC_GPU_NO_DEDUP disables deduplication on NVIDIA' '
	test -f ${XMLDIR}/ipa20.xml &&
	count=$(FLUX_HWLOC_GPU_NO_DEDUP=1 \
	        flux R encode --xml=${XMLDIR}/ipa20.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo NVIDIA with NO_DEDUP: $count GPUs" &&
	test "$count" = "12"
'

test_expect_success 'FLUX_HWLOC_GPU_NO_DEDUP on AMD CPX shows all partitions' '
	test -f ${XMLDIR}/tuo.CPX.xml &&
	count=$(FLUX_HWLOC_GPU_NO_DEDUP=1 \
	        flux R encode --xml=${XMLDIR}/tuo.CPX.xml | \
	        flux R decode --count gpu) &&
	test_debug "echo AMD CPX with NO_DEDUP: $count GPUs" &&
	test "$count" = "24"
'

test_done
