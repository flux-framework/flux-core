#!/bin/sh
#

test_description='Stress test KVS in flux session'

. `dirname $0`/sharness.sh

# kvs-stress takes too long with ASAN
if ! test_have_prereq NO_ASAN; then
    skip_all='skipping kvs-stress tests since AddressSanitizer is active'
    test_done
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b.c

# dtree tests

test_expect_success 'kvs: store 16x3 directory tree' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h3 -w16 --prefix $DIR.dtree
'

test_expect_success 'kvs: walk 16x3 directory tree' '
	test $(flux kvs dir -R $DIR.dtree | wc -l) = 4096
'

test_expect_success 'kvs: unlink, walk 16x3 directory tree with dir --at' '
	DIRREF=$(flux kvs get --treeobj $DIR.dtree) &&
	flux kvs unlink -Rf $DIR.dtree &&
	test $(flux kvs dir -R --at $DIRREF . | wc -l) = 4096
'

test_expect_success 'kvs: store 2x4 directory tree and walk' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h4 -w2 --prefix $DIR.dtree &&
	test $(flux kvs dir -R $DIR.dtree | wc -l) = 16
'

test_expect_success 'kvs: add other types to 2x4 directory and walk' '
	flux kvs link $DIR.dtree $DIR.dtree.link &&
	flux kvs put $DIR.dtree.double=3.14 &&
	flux kvs put $DIR.dtree.boolean=true &&
	test $(flux kvs dir -R $DIR.dtree | wc -l) = 19
'

test_expect_success 'kvs: store 3x4 directory tree using kvsdir functions' '
	flux kvs unlink -Rf $DIR.dtree &&
	${FLUX_BUILD_DIR}/t/kvs/dtree --mkdir -h4 -w3 --prefix $DIR.dtree &&
	test $(flux kvs dir -R $DIR.dtree | wc -l) = 81
'

# commit test

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop, no merging' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit --nomerge 1 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop, mixed no merging' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit --nomerge 2 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

# fence test

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop, no merging' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) --nomerge 1 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop, mixed no merging' '
	THREADS=8 &&
	flux exec -n ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) --nomerge 2 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

# large dirs

test_expect_success 'kvs: store value exceeding RFC 10 max blob size of 1m' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $DIR.tortureval --count 1 --size=1048577
'

test_expect_success 'kvs: store 10,000 keys in one dir' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $DIR.bigdir --count 10000
'

test_expect_success LONGTEST 'kvs: store 100,000 keys in one dir' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $DIR.bigdir2 --count 100000
'

# kvs merging tests

# If transaction-merge=1 and we set KVS_NO_MERGE on all commits, this test
# should behave similarly to transaction-merge=0 and OUTPUT should equal
# THREADS.
test_expect_success 'kvs: test that KVS_NO_MERGE works with kvs_commit()' '
        THREADS=64 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/transactionmerge --nomerge ${THREADS} \
                $(basename ${SHARNESS_TEST_FILE})` &&
	test "$OUTPUT" = "${THREADS}"
'

# All tests below assume transaction-merge=0

# transaction-merge option test
test_expect_success 'kvs: transaction-merge disabling works' '
        THREADS=64 &&
        flux module reload kvs transaction-merge=0 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/transactionmerge ${THREADS} $(basename ${SHARNESS_TEST_FILE})` &&
	test "$OUTPUT" = "${THREADS}"
'

test_done
