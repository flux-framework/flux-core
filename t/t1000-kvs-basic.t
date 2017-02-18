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
	${KVSBASIC} get "$1" >output
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

test_expect_success 'kvs: get a nonexistent key' '
	test_must_fail ${KVSBASIC} get NOT.A.KEY
'


test_expect_success 'kvs: integer put' '
	${KVSBASIC} put $KEY=42 
'
test_expect_success 'kvs: integer type' '
	test_kvs_type $KEY int
'
test_expect_success 'kvs: integer get' '
	test_kvs_key $KEY 42
'
test_expect_success 'kvs: unlink works' '
	${KVSBASIC} unlink $KEY &&
	  test_must_fail ${KVSBASIC} get $KEY
'
test_expect_success 'kvs: value can be empty' '
	${KVSBASIC} put $KEY= &&
	  test_kvs_key $KEY "" &&
	  test_kvs_type $KEY string
'
KEY=$TEST.b.c.d
DIR=$TEST.b.c
test_expect_success 'kvs: string put' '
	${KVSBASIC} put $KEY="Hello world"
'
test_expect_success 'kvs: string type' '
	test_kvs_type $KEY string
'
test_expect_success 'kvs: string get' '
	test_kvs_key $KEY "Hello world"
'
test_expect_success 'kvs: boolean put (true)' '
	${KVSBASIC} put $KEY=true
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: boolean get (true)' '
	test_kvs_key $KEY true
'
test_expect_success 'kvs: boolean put (false)' '
	${KVSBASIC} put $KEY=false
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: boolean get (false)' '
	test_kvs_key $KEY false
'
test_expect_success 'kvs: put double' '
	${KVSBASIC} put $KEY=3.14159
'
test_expect_success 'kvs: double type' '
	test_kvs_type $KEY double
'
test_expect_success 'kvs: get double' '
	test_kvs_key $KEY 3.141590
'

# issue 875
test_expect_success 'kvs: integer can be read as int, int64, or double' '
	${KVSBASIC} put $TEST.a=2 &&
	test_kvs_type $TEST.a int &&
	test $($GETAS -t int $TEST.a) = "2" &&
	test $($GETAS -t int -d $TEST a) = "2" &&
	test $($GETAS -t int64 $TEST.a) = "2" &&
	test $($GETAS -t int64 -d $TEST a) = "2" &&
	test $($GETAS -t double $TEST.a | cut -d. -f1) = "2" &&
	test $($GETAS -t double -d $TEST a | cut -d. -f1) = "2"
'
test_expect_success 'kvs: array put' '
	${KVSBASIC} put $KEY="[1,3,5,7]"
'
test_expect_success 'kvs: array type' '
	test_kvs_type $KEY array
'
test_expect_success 'kvs: array get' '
	test_kvs_key $KEY "[ 1, 3, 5, 7 ]"
'
test_expect_success 'kvs: object put' '
	${KVSBASIC} put $KEY="{\"a\":42}"
'
test_expect_success 'kvs: object type' '
	test_kvs_type $KEY object
'
test_expect_success 'kvs: object get' '
	test_kvs_key $KEY "{ \"a\": 42 }"
'
test_expect_success 'kvs: try to retrieve key as directory should fail' '
	test_must_fail ${KVSBASIC} dir $KEY
'
test_expect_success 'kvs: try to retrieve a directory as key should fail' '
	test_must_fail ${KVSBASIC} get $DIR
'

test_empty_directory() {
	OUTPUT=`${KVSBASIC} dirsize $1` &&
	test "x$OUTPUT" = "x0"
}
test_expect_success 'kvs: empty directory remains after key removed' '
	${KVSBASIC} unlink $KEY &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: remove directory' '
	${KVSBASIC} unlink $TEST
'
test_expect_success 'kvs: empty directory can be created' '
	${KVSBASIC} mkdir $DIR  &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: put values in a directory then retrieve them' '
	${KVSBASIC} put $DIR.a=69 &&
        ${KVSBASIC} put $DIR.b=70 &&
        ${KVSBASIC} put $DIR.c=3.14 &&
        ${KVSBASIC} put $DIR.d=\"snerg\" &&
        ${KVSBASIC} put $DIR.e=true &&
	${KVSBASIC} dir $DIR | sort >output &&
	cat >expected <<EOF
$DIR.a = 69
$DIR.b = 70
$DIR.c = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'
test_expect_success 'kvs: create a dir with keys and subdir' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $DIR.a=69 &&
        ${KVSBASIC} put $DIR.b=70 &&
        ${KVSBASIC} put $DIR.c.d.e.f.g=3.14 &&
        ${KVSBASIC} put $DIR.d=\"snerg\" &&
        ${KVSBASIC} put $DIR.e=true &&
	${KVSBASIC} dir -r $DIR | sort >output &&
	cat >expected <<EOF
$DIR.a = 69
$DIR.b = 70
$DIR.c.d.e.f.g = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: directory with multiple subdirs' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $DIR.a=69 &&
        ${KVSBASIC} put $DIR.b.c.d.e.f.g=70 &&
        ${KVSBASIC} put $DIR.c.a.b=3.14 &&
        ${KVSBASIC} put $DIR.d=\"snerg\" &&
        ${KVSBASIC} put $DIR.e=true &&
	${KVSBASIC} dir -r $DIR | sort >output &&
	cat >expected <<EOF
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: put using no-merge flag' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put-no-merge $DIR.a=69 &&
        ${KVSBASIC} put-no-merge $DIR.b.c.d.e.f.g=70 &&
        ${KVSBASIC} put-no-merge $DIR.c.a.b=3.14 &&
        ${KVSBASIC} put-no-merge $DIR.d=\"snerg\" &&
        ${KVSBASIC} put-no-merge $DIR.e=true &&
	${KVSBASIC} dir -r $DIR | sort >output &&
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
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $DIR.a=69
        ${KVSBASIC} put $DIR.b.c.d.e.f.g=70 &&
        ${KVSBASIC} put $DIR.c.a.b=3.14 &&
        ${KVSBASIC} put $DIR.d=\"snerg\" &&
        ${KVSBASIC} put $DIR.e=true &&
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

test_expect_success 'kvs: cleanup' '
	${KVSBASIC} unlink $TEST
'
test_expect_success 'kvs: dropcache works' '
	${KVSBASIC} dropcache
'
test_expect_success 'kvs: dropcache-all works' '
	${KVSBASIC} dropcache-all
'
test_expect_success 'kvs: symlink: works' '
	TARGET=$TEST.a.b.c &&
	${KVSBASIC} put $TARGET=\"foo\" &&
	${KVSBASIC} link $TARGET $TEST.Q &&
	OUTPUT=$(${KVSBASIC} get $TEST.Q) &&
	test "$OUTPUT" = "foo"
'
test_expect_success 'kvs: symlink: readlink fails on regular value' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	! ${KVSBASIC} readlink $TEST.a.b.c
'
test_expect_success 'kvs: symlink: readlink fails on directory' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} mkdir $TEST.a.b.c &&
	! ${KVSBASIC} readlink $TEST.a.b.
'
test_expect_success 'kvs: symlink: path resolution when intermediate component is a symlink' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	${KVSBASIC} link $TEST.a.b $TEST.Z.Y &&
	OUTPUT=$(${KVSBASIC} get $TEST.Z.Y.c) &&
	test "$OUTPUT" = "42"
'
test_expect_success 'kvs: symlink: path resolution with intermediate symlink and nonexistent key' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} link $TEST.a.b $TEST.Z.Y &&
	test_must_fail ${KVSBASIC} get $TEST.Z.Y
'
test_expect_success 'kvs: symlink: intermediate symlink points to another symlink' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	${KVSBASIC} link $TEST.a.b $TEST.Z.Y &&
	${KVSBASIC} link $TEST.Z.Y $TEST.X.W &&
	test_kvs_key $TEST.X.W.c 42
'
test_expect_success 'kvs: symlink: intermediate symlinks are followed by put' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} mkdir $TEST.a &&
	${KVSBASIC} link $TEST.a $TEST.link &&
	${KVSBASIC} readlink $TEST.link >/dev/null &&
	${KVSBASIC} put $TEST.link.X=42 &&
	${KVSBASIC} readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42 &&
	test_kvs_key $TEST.a.X 42
'

# This will fail if individual ops are applied out of order
test_expect_success 'kvs: symlink: kvs_copy removes symlinked destination' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} mkdir $TEST.a &&
	${KVSBASIC} link $TEST.a $TEST.link &&
	${KVSBASIC} put $TEST.a.X=42 &&
	${KVSBASIC} copy $TEST.a $TEST.link &&
	! ${KVSBASIC} readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42
'

# This will fail if individual ops are applied out of order
test_expect_success 'kvs: symlink: kvs_move works' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} mkdir $TEST.a &&
	${KVSBASIC} link $TEST.a $TEST.link &&
	${KVSBASIC} put $TEST.a.X=42 &&
	${KVSBASIC} move $TEST.a $TEST.link &&
	! ${KVSBASIC} readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42 &&
	! ${KVSBASIC} dir $TEST.a >/dev/null
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (top)' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.X=42 &&
	${KVSBASIC} link $TEST.a $TEST.link &&
	${KVSBASIC} copy $TEST.link $TEST.copy &&
	LINKVAL=$(${KVSBASIC} readlink $TEST.copy) &&
	test "$LINKVAL" = "$TEST.a"
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (mid)' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.X=42 &&
	${KVSBASIC} link $TEST.a.b $TEST.a.link &&
	${KVSBASIC} copy $TEST.a $TEST.copy &&
	LINKVAL=$(${KVSBASIC} readlink $TEST.copy.link) &&
	test "$LINKVAL" = "$TEST.a.b"
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (bottom)' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.X=42 &&
	${KVSBASIC} link $TEST.a.b.X $TEST.a.b.link &&
	${KVSBASIC} copy $TEST.a $TEST.copy &&
	LINKVAL=$(${KVSBASIC} readlink $TEST.copy.b.link) &&
	test "$LINKVAL" = "$TEST.a.b.X"
'

test_expect_success 'kvs: get_symlinkat works after symlink unlinked' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} link $TEST.a.b.X $TEST.a.b.link &&
	ROOTREF=$(${KVSBASIC} get-treeobj .) &&
	${KVSBASIC} unlink $TEST &&
	LINKVAL=$(${KVSBASIC} readlinkat $ROOTREF $TEST.a.b.link) &&
	test "$LINKVAL" = "$TEST.a.b.X"
'

test_expect_success 'kvs: get-treeobj: returns directory reference for root' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} get-treeobj . | grep -q "DIRREF"
'

test_expect_success 'kvs: get-treeobj: returns directory reference for directory' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} mkdir $TEST.a &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "DIRREF"
'

test_expect_success 'kvs: get-treeobj: returns value for small value' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a=b &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "FILEVAL"
'

test_expect_success 'kvs: get-treeobj: returns value ref for large value' '
	${KVSBASIC} unlink $TEST &&
	dd if=/dev/zero bs=4096 count=1 | ${KVSBASIC} copy-tokvs $TEST.a - &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "FILEREF"
'

test_expect_success 'kvs: get-treeobj: returns link value for symlink' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.X=42 &&
	${KVSBASIC} link $TEST.a.b.X $TEST.a.b.link &&
	${KVSBASIC} get-treeobj $TEST.a.b.link | grep -q LINKVAL
'

test_expect_success 'kvs: put-treeobj: can make root snapshot' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} get-treeobj . >snapshot &&
	${KVSBASIC} put-treeobj $TEST.snap="`cat snapshot`" &&
	${KVSBASIC} get-treeobj $TEST.snap >snapshot.cpy
	test_cmp snapshot snapshot.cpy
'

test_expect_success 'kvs: put-treeobj: clobbers destination' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a=42 &&
	${KVSBASIC} get-treeobj . >snapshot2 &&
	${KVSBASIC} put-treeobj $TEST.a="`cat snapshot2`" &&
	! ${KVSBASIC} get $TEST.a &&
	${KVSBASIC} dir $TEST.a
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: not JSON' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a=xyz
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: unknown type' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"ERSTWHILE\":\"fubar\"}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad link type' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"LINKVAL\":42}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad ref type' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"DIRREF\":{}}"
'

test_expect_success 'kvs: put-treeobj: fails bad dirent: bad blobref' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"DIRREF\":\"sha2-aaa\"}" &&
	test_must_fail ${KVSBASIC} put-treeobj $TEST.a="{\"DIRREF\":\"sha1-bbb\"}"
'

test_expect_success 'kvs: getat: fails bad on dirent' '
	${KVSBASIC} unlink $TEST &&
	test_must_fail ${KVSBASIC} getat 42 $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"DIRREF\":\"sha2-aaa\"}" $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"DIRREF\":\"sha1-bbb\"}" $TEST.a &&
	test_must_fail ${KVSBASIC} getat "{\"DIRVAL\":{}}" $TEST.a
'

test_expect_success 'kvs: getat: works on root from get-treeobj' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj .) $TEST.a.b.c) = 42
'

test_expect_success 'kvs: getat: works on subdir from get-treeobj' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj $TEST.a.b) c) = 42
'

test_expect_success 'kvs: getat: works on outdated root' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c=42 &&
	ROOTREF=$(${KVSBASIC} get-treeobj .) &&
	${KVSBASIC} put $TEST.a.b.c=43 &&
	test $(${KVSBASIC} getat $ROOTREF $TEST.a.b.c) = 42
'

test_expect_success 'kvs: kvsdir_get_size works' '
	${KVSBASIC} mkdir $TEST.dirsize &&
	${KVSBASIC} put $TEST.dirsize.a=1 &&
	${KVSBASIC} put $TEST.dirsize.b=2 &&
	${KVSBASIC} put $TEST.dirsize.c=3 &&
	OUTPUT=$(${KVSBASIC} dirsize $TEST.dirsize) &&
	test "$OUTPUT" = "3"
'

test_expect_success 'kvs: store 16x3 directory tree' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h3 -w16 --prefix $TEST.dtree
'

test_expect_success 'kvs: walk 16x3 directory tree' '
	test $(${KVSBASIC} dir -r $TEST.dtree | wc -l) = 4096
'

test_expect_success 'kvs: unlink, walk 16x3 directory tree with dirat' '
	DIRREF=$(${KVSBASIC} get-treeobj $TEST.dtree) &&
	${KVSBASIC} unlink $TEST.dtree &&
	test $(${KVSBASIC} dirat -r $DIRREF . | wc -l) = 4096
'

test_expect_success 'kvs: store 2x4 directory tree and walk' '
	${FLUX_BUILD_DIR}/t/kvs/dtree -h4 -w2 --prefix $TEST.dtree
	test $(${KVSBASIC} dir -r $TEST.dtree | wc -l) = 16
'

# exercise kvsdir_get_symlink, _double, _boolean, 
test_expect_success 'kvs: add other types to 2x4 directory and walk' '
	${KVSBASIC} link $TEST.dtree $TEST.dtree.link &&
	${KVSBASIC} put $TEST.dtree.double=3.14 &&
	${KVSBASIC} put $TEST.dtree.booelan=true &&
	test $(${KVSBASIC} dir -r $TEST.dtree | wc -l) = 19
'

test_expect_success 'kvs: store 3x4 directory tree using kvsdir_put functions' '
	${KVSBASIC} unlink $TEST.dtree &&
	${FLUX_BUILD_DIR}/t/kvs/dtree --mkdir -h4 -w3 --prefix $TEST.dtree &&
	test $(${KVSBASIC} dir -r $TEST.dtree | wc -l) = 81
'

test_expect_success 'kvs: put key of . fails' '
	test_must_fail ${KVSBASIC} put .=1
'

# Keep the next two tests in order
test_expect_success 'kvs: symlink: dangling link' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} link $TEST.dangle $TEST.a.b.c
'
test_expect_success 'kvs: symlink: readlink on dangling link' '
	OUTPUT=$(${KVSBASIC} readlink $TEST.a.b.c) &&
	test "$OUTPUT" = "$TEST.dangle"
'
test_expect_success 'kvs: symlink: readlink works on non-dangling link' '
	${KVSBASIC} unlink $TEST &&
	${KVSBASIC} put $TEST.a.b.c="foo" &&
	${KVSBASIC} link $TEST.a.b.c $TEST.link &&
	OUTPUT=$(${KVSBASIC} readlink $TEST.link) &&
	test "$OUTPUT" = "$TEST.a.b.c"
'

# test synchronization based on commit sequence no.

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	${KVSBASIC} put $TEST.xxx=99 &&
	VERS=$(${KVSBASIC} version) &&
	flux exec sh -c "${KVSBASIC} wait ${VERS} && ${KVSBASIC} exists $TEST.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	${KVSBASIC} unlink $TEST.xxx &&
	VERS=$(${KVSBASIC} version) &&
	flux exec sh -c "${KVSBASIC} wait ${VERS} && ! ${KVSBASIC} exists $TEST.xxx"
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

test_expect_success 'kvs: watch 5 versions of key'  '
	${KVSBASIC} unlink $TEST.foo &&
        ${KVSBASIC} put $TEST.foo.a=1 &&
	${KVSBASIC} watch 5 $TEST.foo.a >watch_out &
        for i in $(seq 2 5); do
            ${KVSBASIC} put $TEST.foo.a=${i}
        done
'

test_expect_success 'kvs: watch 5 versions of directory'  '
	${KVSBASIC} unlink $TEST.foo &&
	${KVSBASIC} watch-dir -r 5 $TEST.foo >watch_dir_out &
	while $(grep -s '===============' watch_dir_out | wc -l) -lt 5; do
	    ${KVSBASIC} put $TEST.foo.a=$(date +%N); \
	done
'

test_expect_success 'kvs: watch-mt: multi-threaded kvs watch program' '
	${FLUX_BUILD_DIR}/t/kvs/watch mt 100 100 $TEST.a &&
	${KVSBASIC} unlink $TEST.a
'

test_expect_success 'kvs: watch-selfmod: watch callback modifies watched key' '
	${FLUX_BUILD_DIR}/t/kvs/watch selfmod $TEST.a &&
	${KVSBASIC} unlink $TEST.a
'

test_expect_success 'kvs: watch-unwatch unwatch works' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatch $TEST.a &&
	${KVSBASIC} unlink $TEST.a
'

test_expect_success 'kvs: watch-unwatchloop 1000 watch/unwatch ok' '
	${FLUX_BUILD_DIR}/t/kvs/watch unwatchloop $TEST.a &&
	${KVSBASIC} unlink $TEST.a
'

test_expect_success 'kvs: 8192 simultaneous watches works' '
	${FLUX_BUILD_DIR}/t/kvs/watch simulwatch $TEST.a 8192 &&
	${KVSBASIC} unlink $TEST.a
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
	${KVSBASIC} unlink $TEST &&
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
