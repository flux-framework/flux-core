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
	flux kvs get --json "$1" >output
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
	flux kvs put --json $KEY=42
'
test_expect_success 'kvs: integer type' '
	test_kvs_type $KEY int
'
test_expect_success 'kvs: value can be empty' '
	flux kvs put --json $KEY= &&
	  test_kvs_key $KEY "" &&
	  test_kvs_type $KEY string
'
test_expect_success 'kvs: null is converted to json null' '
	flux kvs put --json $KEY=null &&
	  test_kvs_key $KEY nil &&
	  test_kvs_type $KEY null
'

test_expect_success 'kvs: quoted null is converted to string' '
	flux kvs put --json $KEY=\"null\" &&
	  test_kvs_key $KEY null &&
	  test_kvs_type $KEY string
'

KEY=$TEST.b.c.d
DIR=$TEST.b.c
test_expect_success 'kvs: string put' '
	flux kvs put --json $KEY="Hello world"
'
test_expect_success 'kvs: string type' '
	test_kvs_type $KEY string
'
test_expect_success 'kvs: boolean put' '
	flux kvs put --json $KEY=true
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: put double' '
	flux kvs put --json $KEY=3.14159
'
test_expect_success 'kvs: double type' '
	test_kvs_type $KEY double
'

# issue 875
test_expect_success 'kvs: integer can be read as int, int64, or double' '
	flux kvs put --json $TEST.a=2 &&
	test_kvs_type $TEST.a int &&
	test $($GETAS -t int $TEST.a) = "2" &&
	test $($GETAS -t int -d $TEST a) = "2" &&
	test $($GETAS -t int64 $TEST.a) = "2" &&
	test $($GETAS -t int64 -d $TEST a) = "2" &&
	test $($GETAS -t double $TEST.a | cut -d. -f1) = "2" &&
	test $($GETAS -t double -d $TEST a | cut -d. -f1) = "2"
'
test_expect_success 'kvs: array put' '
	flux kvs put --json $KEY="[1,3,5,7]"
'
test_expect_success 'kvs: array type' '
	test_kvs_type $KEY array
'
test_expect_success 'kvs: object put' '
	flux kvs put --json $KEY="{\"a\":42}"
'
test_expect_success 'kvs: object type' '
	test_kvs_type $KEY object
'

test_expect_success 'kvs: put using --no-merge flag' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --no-merge --json $DIR.a=69 &&
        flux kvs put --no-merge --json $DIR.b.c.d.e.f.g=70 &&
        flux kvs put --no-merge --json $DIR.c.a.b=3.14 &&
        flux kvs put --no-merge --json $DIR.d=\"snerg\" &&
        flux kvs put --no-merge --json $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: directory with multiple subdirs using dir --at' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $DIR.a=69 &&
        flux kvs put --json $DIR.b.c.d.e.f.g=70 &&
        flux kvs put --json $DIR.c.a.b=3.14 &&
        flux kvs put --json $DIR.d=\"snerg\" &&
        flux kvs put --json $DIR.e=true &&
        DIRREF=$(flux kvs get --treeobj $DIR) &&
	flux kvs dir -R --at $DIRREF . | sort >output &&
	cat >expected <<EOF &&
a = 69
b.c.d.e.f.g = 70
c.a.b = 3.140000
d = snerg
e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: readlink --at works after symlink unlinked' '
	flux kvs unlink -Rf $TEST &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
	ROOTREF=$(flux kvs get --treeobj .) &&
	flux kvs unlink -R $TEST &&
	LINKVAL=$(flux kvs readlink --at $ROOTREF $TEST.a.b.link) &&
	test "$LINKVAL" = "$TEST.a.b.X"
'

test_expect_success 'kvs: get --treeobj: returns dirref object for root' '
	flux kvs unlink -Rf $TEST &&
	flux kvs get --treeobj . | grep -q \"dirref\"
'

test_expect_success 'kvs: get --treeobj: returns dirref object for directory' '
	flux kvs unlink -Rf $TEST &&
	flux kvs mkdir $TEST.a &&
	flux kvs get --treeobj $TEST.a | grep -q \"dirref\"
'

test_expect_success 'kvs: get --treeobj: returns val object for small value' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a=b &&
	flux kvs get --treeobj $TEST.a | grep -q \"val\"
'

test_expect_success 'kvs: get --treeobj: returns value ref for large value' '
	flux kvs unlink -Rf $TEST &&
	dd if=/dev/zero bs=4096 count=1 | flux kvs put --raw $TEST.a=- &&
	flux kvs get --treeobj $TEST.a | grep -q \"valref\"
'

test_expect_success 'kvs: get --treeobj: returns link value for symlink' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a.b.X=42 &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
	flux kvs get --treeobj $TEST.a.b.link | grep -q \"symlink\"
'

test_expect_success 'kvs: put --treeobj: can make root snapshot' '
	flux kvs unlink -Rf $TEST &&
	flux kvs get --treeobj . >snapshot &&
	flux kvs put --treeobj $TEST.snap="`cat snapshot`" &&
	flux kvs get --treeobj $TEST.snap >snapshot.cpy &&
	test_cmp snapshot snapshot.cpy
'

test_expect_success 'kvs: put --treeobj: clobbers destination' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a=42 &&
	flux kvs get --treeobj . >snapshot2 &&
	flux kvs put --treeobj $TEST.a="`cat snapshot2`" &&
	! flux kvs get --json $TEST.a &&
	flux kvs dir $TEST.a
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: not JSON' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs put --treeobj $TEST.a=xyz
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: unknown type' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":\"MQA=\",\"type\":\"FOO\",\"ver\":1}"
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: bad link data' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":42,\"type\":\"symlink\",\"ver\":1}"
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: bad ref data' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":42,\"type\":\"dirref\",\"ver\":1}" &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":"sha1-4087718d190b373fb490b27873f61552d7f29dbe",\"type\":\"dirref\",\"ver\":1}"
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: bad blobref' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" &&
	test_must_fail flux kvs put --treeobj $TEST.a="{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}"
'

test_expect_success 'kvs: get --at: fails bad on dirent' '
	flux kvs unlink -Rf $TEST &&
	test_must_fail flux kvs get --at 42 $TEST.a &&
	test_must_fail flux kvs get --at "{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" $TEST.a &&
	test_must_fail flux kvs get --at "{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}" $TEST.a &&
	test_must_fail flux kvs get --at "{\"data\":42,\"type\":\"dirref\",\"ver\":1}" $TEST.a &&
	test_must_fail flux kvs get --at "{\"data\":"sha1-4087718d190b373fb490b27873f61552d7f29dbe",\"type\":\"dirref\",\"ver\":1}" $TEST.a
'

test_expect_success 'kvs: get --at: works on root from get --treeobj' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a.b.c=42 &&
	test $(flux kvs get --at $(flux kvs get --treeobj .) $TEST.a.b.c) = 42
'

test_expect_success 'kvs: get --at: works on subdir from get --treeobj' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a.b.c=42 &&
	test $(flux kvs get --at $(flux kvs get --treeobj $TEST.a.b) c) = 42
'

test_expect_success 'kvs: get --at: works on outdated root' '
	flux kvs unlink -Rf $TEST &&
	flux kvs put --json $TEST.a.b.c=42 &&
	ROOTREF=$(flux kvs get --treeobj .) &&
	flux kvs put --json $TEST.a.b.c=43 &&
	test $(flux kvs get --at $ROOTREF $TEST.a.b.c) = 42
'

test_expect_success 'kvs: kvsdir_get_size works' '
	flux kvs mkdir $TEST.dirsize &&
	flux kvs put --json $TEST.dirsize.a=1 &&
	flux kvs put --json $TEST.dirsize.b=2 &&
	flux kvs put --json $TEST.dirsize.c=3 &&
	OUTPUT=$(flux kvs ls -1 $TEST.dirsize | wc -l) &&
	test "$OUTPUT" = "3"
'

# kvs reads/writes of raw data to/from content store work

largeval="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
largevalhash="sha1-0b22e9fecf9c832032fe976e67058df0322dcc5c"

test_expect_success 'kvs: large put stores raw data into content store' '
	flux kvs unlink -Rf $TEST &&
 	flux kvs put --json $TEST.largeval=$largeval &&
	flux kvs get --treeobj $TEST.largeval | grep -q \"valref\" &&
	flux kvs get --treeobj $TEST.largeval | grep -q ${largevalhash} &&
	flux content load ${largevalhash} | grep $largeval
'

test_expect_success 'kvs: valref that points to content store data can be read' '
        flux kvs unlink -Rf $TEST &&
	echo "$largeval" | flux content store &&
	flux kvs put --treeobj $TEST.largeval2="{\"data\":[\"${largevalhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --json $TEST.largeval2 | grep $largeval
'

test_expect_success 'kvs: valref that points to zero size content store data can be read' '
	flux kvs unlink -Rf $TEST &&
        hashval=`flux content store </dev/null` &&
	flux kvs put --treeobj $TEST.empty="{\"data\":[\"${hashval}\"],\"type\":\"valref\",\"ver\":1}" &&
	test $(flux kvs get --raw $TEST.empty|wc -c) -eq 0
'

test_expect_success 'kvs: valref can point to other treeobjs' '
	flux kvs unlink -Rf $TEST &&
        flux kvs mkdir $TEST.a.b.c &&
        dirhash=`flux kvs get --treeobj $TEST.a.b.c | grep -P "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $TEST.value="{\"data\":[\"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.value | grep dir
'

# multi-blobref valrefs

test_expect_success 'kvs: multi blob-ref valref can be read' '
        flux kvs unlink -Rf $TEST &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $TEST.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.multival | grep "abcdefgh" &&
        test $(flux kvs get --raw $TEST.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on left, can be read' '
        flux kvs unlink -Rf $TEST &&
	hashval1=`flux content store < /dev/null` &&
	hashval2=`echo -n "abcd" | flux content store` &&
	flux kvs put --treeobj $TEST.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.multival | grep "abcd" &&
        test $(flux kvs get --raw $TEST.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on right, can be read' '
        flux kvs unlink -Rf $TEST &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	flux kvs put --treeobj $TEST.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.multival | grep "abcd" &&
        test $(flux kvs get --raw $TEST.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref in middle, can be read' '
        flux kvs unlink -Rf $TEST &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	hashval3=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $TEST.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\", \"${hashval3}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.multival | grep "abcdefgh" &&
        test $(flux kvs get --raw $TEST.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with a blobref pointing to a treeobj' '
        flux kvs unlink -Rf $TEST &&
	hashval1=`echo -n "abcd" | flux content store` &&
        flux kvs mkdir $TEST.a.b.c &&
        dirhash=`flux kvs get --treeobj $TEST.a.b.c | grep -P "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $TEST.multival="{\"data\":[\"${hashval1}\", \"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $TEST.multival | grep dir
'

# dtree tests

test_expect_success 'kvs: store 16x3 directory tree' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h3 -w16 --prefix $TEST.dtree
'

test_expect_success 'kvs: walk 16x3 directory tree' '
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 4096
'

test_expect_success 'kvs: unlink, walk 16x3 directory tree with dir --at' '
	DIRREF=$(flux kvs get --treeobj $TEST.dtree) &&
	flux kvs unlink -Rf $TEST.dtree &&
	test $(flux kvs dir -R --at $DIRREF . | wc -l) = 4096
'

test_expect_success 'kvs: store 2x4 directory tree and walk' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h4 -w2 --prefix $TEST.dtree &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 16
'

test_expect_success 'kvs: add other types to 2x4 directory and walk' '
	flux kvs link $TEST.dtree $TEST.dtree.link &&
	flux kvs put --json $TEST.dtree.double=3.14 &&
	flux kvs put --json $TEST.dtree.booelan=true &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 19
'

test_expect_success 'kvs: store 3x4 directory tree using kvsdir_put functions' '
	flux kvs unlink -Rf $TEST.dtree &&
	${FLUX_BUILD_DIR}/t/kvs/dtree --mkdir -h4 -w3 --prefix $TEST.dtree &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 81
'

# test synchronization based on commit sequence no.

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	flux kvs put --json $TEST.xxx=99 &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && flux kvs get --json $TEST.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	flux kvs unlink -Rf $TEST.xxx &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && ! flux kvs get --json $TEST.xxx"
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

test_expect_success 'kvs: kvs put --raw and kvs get --raw  work' '
	flux kvs unlink -Rf $TEST &&
	dd if=/dev/urandom bs=4096 count=1 >random.data &&
	flux kvs put --raw $TEST.data=- <random.data &&
	flux kvs get --raw $TEST.data >reread.data &&
	test_cmp random.data reread.data
'

# kvs merging tests

# If commit-merge=1 and we set KVS_NO_MERGE on all commits, this test
# should behave similarly to commit-merge=0 and OUTPUT should equal
# THREADS.
test_expect_success 'kvs: test that KVS_NO_MERGE works with kvs_commit()' '
        THREADS=64 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/commitmerge --nomerge ${THREADS} \
                $(basename ${SHARNESS_TEST_FILE})` &&
	test "$OUTPUT" = "${THREADS}"
'

# All tests below assume commit-merge=0

# commit-merge option test
test_expect_success 'kvs: commit-merge disabling works' '
        THREADS=64 &&
        flux module remove -r 0 kvs &&
        flux module load -r 0 kvs commit-merge=0 &&
        OUTPUT=`${FLUX_BUILD_DIR}/t/kvs/commitmerge ${THREADS} $(basename ${SHARNESS_TEST_FILE})` &&
	test "$OUTPUT" = "${THREADS}"
'

test_done
