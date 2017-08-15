#!/bin/sh
#

test_description='Test basic kvs usage in flux session

Verify basic KVS operations against a running flux comms session.
This test script verifies operation of the kvs and should be run
before other tests that depend on kvs.
'

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

KVSBASIC=${FLUX_BUILD_DIR}/t/kvs/basic
GETAS=${FLUX_BUILD_DIR}/t/kvs/getas

TEST=$TEST_NAME
KEY=test.a.b.c

#
#
#
test_kvs_key() {
	flux kvs get "$1" >output
	echo "$2" >expected
	test_cmp output expected
	#if ! test "$OUTPUT" = "$2"; then
	#	test_debug say_color error "Error: Output \'$OUTPUT\" != \'$2\'"
	#	return false
	#fi
}

test_kvs_type () {
	${KVSBASIC} type "$1" >output
	echo "$2" >expected
	test_cmp output expected
}

test_expect_success 'kvs: integer put' '
	flux kvs put $KEY=42 
'
test_expect_success 'kvs: integer type' '
	test_kvs_type $KEY int
'
test_expect_success 'kvs: value can be empty' '
	flux kvs put $KEY= &&
	  test_kvs_key $KEY "" &&
	  test_kvs_type $KEY string
'
test_expect_success 'kvs: null is converted to json null' '
	flux kvs put $KEY=null &&
	  test_kvs_key $KEY nil &&
	  test_kvs_type $KEY null
'

test_expect_success 'kvs: quoted null is converted to string' '
	flux kvs put $KEY=\"null\" &&
	  test_kvs_key $KEY null &&
	  test_kvs_type $KEY string
'

KEY=$TEST.b.c.d
DIR=$TEST.b.c
test_expect_success 'kvs: string put' '
	flux kvs put $KEY="Hello world"
'
test_expect_success 'kvs: string type' '
	test_kvs_type $KEY string
'
test_expect_success 'kvs: boolean put' '
	flux kvs put $KEY=true
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: put double' '
	flux kvs put $KEY=3.14159
'
test_expect_success 'kvs: double type' '
	test_kvs_type $KEY double
'

# issue 875
test_expect_success 'kvs: integer can be read as int, int64, or double' '
	flux kvs put $TEST.a=2 &&
	test_kvs_type $TEST.a int &&
	test $($GETAS -t int $TEST.a) = "2" &&
	test $($GETAS -t int -d $TEST a) = "2" &&
	test $($GETAS -t int64 $TEST.a) = "2" &&
	test $($GETAS -t int64 -d $TEST a) = "2" &&
	test $($GETAS -t double $TEST.a | cut -d. -f1) = "2" &&
	test $($GETAS -t double -d $TEST a | cut -d. -f1) = "2"
'
test_expect_success 'kvs: array put' '
	flux kvs put $KEY="[1,3,5,7]"
'
test_expect_success 'kvs: array type' '
	test_kvs_type $KEY array
'
test_expect_success 'kvs: object put' '
	flux kvs put $KEY="{\"a\":42}"
'
test_expect_success 'kvs: object type' '
	test_kvs_type $KEY object
'

test_expect_success 'kvs: put using no-merge flag' '
	flux kvs unlink -Rf $TEST &&
	${KVSBASIC} put-no-merge $DIR.a=69 &&
        ${KVSBASIC} put-no-merge $DIR.b.c.d.e.f.g=70 &&
        ${KVSBASIC} put-no-merge $DIR.c.a.b=3.14 &&
        ${KVSBASIC} put-no-merge $DIR.d=\"snerg\" &&
        ${KVSBASIC} put-no-merge $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: directory with multiple subdirs using dirat' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $DIR.a=69
        flux kvs put $DIR.b.c.d.e.f.g=70 &&
        flux kvs put $DIR.c.a.b=3.14 &&
        flux kvs put $DIR.d=\"snerg\" &&
        flux kvs put $DIR.e=true &&
        DIRREF=$(${KVSBASIC} get-treeobj $DIR) &&
	${KVSBASIC} dirat -r $DIRREF . | sort >output &&
	cat >expected <<EOF
a = 69
b.c.d.e.f.g = 70
c.a.b = 3.140000
d = snerg
e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: get_symlinkat works after symlink unlinked' '
	flux kvs unlink -Rf $TEST &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
	ROOTREF=$(${KVSBASIC} get-treeobj .) &&
	flux kvs unlink -R $TEST &&
	LINKVAL=$(${KVSBASIC} readlinkat $ROOTREF $TEST.a.b.link) &&
	test "$LINKVAL" = "$TEST.a.b.X"
'

test_expect_success 'kvs: get-treeobj: returns dirref object for root' '
	flux kvs unlink -Rf $TEST &&
	${KVSBASIC} get-treeobj . | grep -q \"dirref\"
'

test_expect_success 'kvs: get-treeobj: returns dirref object for directory' '
	flux kvs unlink -Rf $TEST &&
	flux kvs mkdir $TEST.a &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q \"dirref\"
'

test_expect_success 'kvs: get-treeobj: returns val object for small value' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a=b &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q \"val\"
'

test_expect_success 'kvs: get-treeobj: returns value ref for large value' '
	flux kvs unlink -Rf $TEST &&
	dd if=/dev/zero bs=4096 count=1 | ${KVSBASIC} copy-tokvs $TEST.a - &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q \"valref\"
'

test_expect_success 'kvs: get-treeobj: returns link value for symlink' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a.b.X=42 &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
	${KVSBASIC} get-treeobj $TEST.a.b.link | grep -q \"symlink\"
'

test_expect_success 'kvs: put-treeobj: can make root snapshot' '
	flux kvs unlink -Rf $TEST &&
	${KVSBASIC} get-treeobj . >snapshot &&
	${KVSBASIC} put-treeobj $TEST.snap="`cat snapshot`" &&
	${KVSBASIC} get-treeobj $TEST.snap >snapshot.cpy
	test_cmp snapshot snapshot.cpy
'

test_expect_success 'kvs: put-treeobj: clobbers destination' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a=42 &&
	${KVSBASIC} get-treeobj . >snapshot2 &&
	${KVSBASIC} put-treeobj $TEST.a="`cat snapshot2`" &&
	! flux kvs get $TEST.a &&
	flux kvs dir $TEST.a
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: not JSON' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a=xyz
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: unknown type' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"data\":\"MQA=\",\"type\":\"FOO\",\"ver\":1}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad link data' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"data\":42,\"type\":\"symlink\",\"ver\":1}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad ref data' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"data\":42,\"type\":\"dirref\",\"ver\":1}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad blobref' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}"
'

test_expect_success 'kvs: getat: fails bad on dirent' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail ${KVSBASIC} getat 42 $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}" $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"data\":42,\"type\":\"dirref\",\"ver\":1}" $TEST.a
'

test_expect_success 'kvs: getat: works on root from get-treeobj' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj .) $TEST.a.b.c) = 42
'

test_expect_success 'kvs: getat: works on subdir from get-treeobj' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj $TEST.a.b) c) = 42
'

test_expect_success 'kvs: getat: works on outdated root' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	ROOTREF=$(${KVSBASIC} get-treeobj .) &&
	flux kvs put $TEST.a.b.c=43 &&
	test $(${KVSBASIC} getat $ROOTREF $TEST.a.b.c) = 42
'

test_expect_success 'kvs: kvsdir_get_size works' '
	flux kvs mkdir $TEST.dirsize &&
	flux kvs put $TEST.dirsize.a=1 &&
	flux kvs put $TEST.dirsize.b=2 &&
	flux kvs put $TEST.dirsize.c=3 &&
	OUTPUT=$(${KVSBASIC} dirsize $TEST.dirsize) &&
	test "$OUTPUT" = "3"
'

test_expect_success 'kvs: store 16x3 directory tree' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h3 -w16 --prefix $TEST.dtree
'

test_expect_success 'kvs: walk 16x3 directory tree' '
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 4096
'

test_expect_success 'kvs: unlink, walk 16x3 directory tree with dirat' '
	DIRREF=$(${KVSBASIC} get-treeobj $TEST.dtree) &&
	flux kvs unlink -Rf $TEST.dtree &&
	test $(${KVSBASIC} dirat -r $DIRREF . | wc -l) = 4096
'

test_expect_success 'kvs: store 2x4 directory tree and walk' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h4 -w2 --prefix $TEST.dtree
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 16
'

# exercise kvsdir_get_symlink, _double, _boolean, 
test_expect_success 'kvs: add other types to 2x4 directory and walk' '
	flux kvs link $TEST.dtree $TEST.dtree.link &&
	flux kvs put $TEST.dtree.double=3.14 &&
	flux kvs put $TEST.dtree.booelan=true &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 19
'

test_expect_success 'kvs: store 3x4 directory tree using kvsdir_put functions' '
	flux kvs unlink -Rf $TEST.dtree &&
	${FLUX_BUILD_DIR}/t/kvs/dtree --mkdir -h4 -w3 --prefix $TEST.dtree &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 81
'

# test synchronization based on commit sequence no.

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	flux kvs put $TEST.xxx=99 &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && flux kvs get $TEST.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	flux kvs unlink -Rf $TEST.xxx &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && ! flux kvs get $TEST.xxx"
'

# commit test

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop, no merging' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit --nomerge 1 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,commits in a loop, mixed no merging' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit --nomerge 2 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

# fence test

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop, no merging' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) --nomerge 1 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

test_expect_success 'kvs: 8 threads/rank each doing 100 put,fence in a loop, mixed no merging' '
	THREADS=8 &&
	flux exec ${FLUX_BUILD_DIR}/t/kvs/commit \
		--fence $((${SIZE}*${THREADS})) --nomerge 2 ${THREADS} 100 \
		$(basename ${SHARNESS_TEST_FILE})
'

# watch tests

test_expect_success 'kvs: watch-mt: multi-threaded kvs watch program' '
	${FLUX_BUILD_DIR}/t/kvs/watch mt 100 100 $TEST.a &&
	flux kvs unlink -Rf $TEST.a
'

test_expect_success 'kvs: watch-selfmod: watch callback modifies watched key' '
	${FLUX_BUILD_DIR}/t/kvs/watch selfmod $TEST.a &&
	flux kvs unlink -Rf $TEST.a
'

test_expect_success 'kvs: watch-unwatch unwatch works' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatch $TEST.a &&
	flux kvs unlink -Rf $TEST.a
'

test_expect_success 'kvs: watch-unwatchloop 1000 watch/unwatch ok' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatchloop $TEST.a &&
	flux kvs unlink -Rf $TEST.a
'

test_expect_success 'kvs: 256 simultaneous watches works' '
	${FLUX_BUILD_DIR}/t/kvs/watch simulwatch $TEST.a 256 &&
	flux kvs unlink -Rf $TEST.a
'


# large values/dirs

test_expect_success 'kvs: store value exceeding RFC 10 max blob size of 1m' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $TEST.tortureval --count 1 --size=1048577
'

test_expect_success 'kvs: store 10,000 keys in one dir' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $TEST.bigdir --count 10000
'

test_expect_success LONGTEST 'kvs: store 1,000,000 keys in one dir' '
	${FLUX_BUILD_DIR}/t/kvs/torture --prefix $TEST.bigdir2 --count 1000000
'


# async fence

test_expect_success 'kvs: async kvs_fence allows puts with fence in progress' '
	${FLUX_BUILD_DIR}/t/kvs/asyncfence
'

# base64 data
test_expect_success 'kvs: copy-tokvs and copy-fromkvs work' '
	flux kvs unlink -Rf $TEST &&
	dd if=/dev/urandom bs=4096 count=1 >random.data &&
	${KVSBASIC} copy-tokvs $TEST.data random.data &&
	${KVSBASIC} copy-fromkvs $TEST.data reread.data &&
	test_cmp random.data reread.data
'

# kvs merging tests

# If commit-merge=1 and we set KVS_NO_MERGE on all commits, this test
# should behave similarly to commit-merge=0 and OUTPUT should equal
# THREADS.
test_expect_success 'kvs: test that KVS_NO_MERGE works with kvs_commit()' '
        THREADS=64 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/commitmerge --nomerge ${THREADS} \
                $(basename ${SHARNESS_TEST_FILE})`
	test "$OUTPUT" = "${THREADS}"
'

# All tests below assume commit-merge=0

# commit-merge option test
test_expect_success 'kvs: commit-merge disabling works' '
        THREADS=64 &&
        flux module remove -r 0 kvs &&
        flux module load -r 0 kvs commit-merge=0 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/commitmerge ${THREADS} $(basename ${SHARNESS_TEST_FILE})`
	test "$OUTPUT" = "${THREADS}"
'

test_done
