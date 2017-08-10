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

test_expect_success 'kvs: get a nonexistent key' '
	test_must_fail flux kvs get NOT.A.KEY
'


test_expect_success 'kvs: integer put' '
	flux kvs put $KEY=42 
'
test_expect_success 'kvs: integer type' '
	test_kvs_type $KEY int
'
test_expect_success 'kvs: integer get' '
	test_kvs_key $KEY 42
'
test_expect_success 'kvs: unlink works' '
	${KVSBASIC} unlink $KEY &&
	  test_must_fail flux kvs get $KEY
'
test_expect_success 'kvs: value can be empty' '
	flux kvs put $KEY= &&
	  test_kvs_key $KEY "" &&
	  test_kvs_type $KEY string
'
test_expect_success 'kvs: put JSON null is converted to string' '
	flux kvs put $KEY=null &&
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
test_expect_success 'kvs: string get' '
	test_kvs_key $KEY "Hello world"
'
test_expect_success 'kvs: boolean put (true)' '
	flux kvs put $KEY=true
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: boolean get (true)' '
	test_kvs_key $KEY true
'
test_expect_success 'kvs: boolean put (false)' '
	flux kvs put $KEY=false
'
test_expect_success 'kvs: boolean type' '
	test_kvs_type $KEY boolean
'
test_expect_success 'kvs: boolean get (false)' '
	test_kvs_key $KEY false
'
test_expect_success 'kvs: put double' '
	flux kvs put $KEY=3.14159
'
test_expect_success 'kvs: double type' '
	test_kvs_type $KEY double
'
test_expect_success 'kvs: get double' '
	test_kvs_key $KEY 3.141590
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
test_expect_success 'kvs: array get' '
	test_kvs_key $KEY "[1, 3, 5, 7]"
'
test_expect_success 'kvs: object put' '
	flux kvs put $KEY="{\"a\":42}"
'
test_expect_success 'kvs: object type' '
	test_kvs_type $KEY object
'
test_expect_success 'kvs: object get' '
	test_kvs_key $KEY "{\"a\": 42}"
'
test_expect_success 'kvs: try to retrieve key as directory should fail' '
	test_must_fail flux kvs dir $KEY
'
test_expect_success 'kvs: try to retrieve a directory as key should fail' '
	test_must_fail flux kvs get $DIR
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
	flux kvs mkdir $DIR  &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: put values in a directory then retrieve them' '
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b=70 &&
        flux kvs put $DIR.c=3.14 &&
        flux kvs put $DIR.d=\"snerg\" &&
        flux kvs put $DIR.e=true &&
	flux kvs dir $DIR | sort >output &&
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
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b=70 &&
        flux kvs put $DIR.c.d.e.f.g=3.14 &&
        flux kvs put $DIR.d=\"snerg\" &&
        flux kvs put $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
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
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b.c.d.e.f.g=70 &&
        flux kvs put $DIR.c.a.b=3.14 &&
        flux kvs put $DIR.d=\"snerg\" &&
        flux kvs put $DIR.e=true &&
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

test_expect_success 'kvs: put using no-merge flag' '
	${KVSBASIC} unlink $TEST &&
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
	${KVSBASIC} unlink $TEST &&
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

test_expect_success 'kvs: cleanup' '
	${KVSBASIC} unlink $TEST
'
test_expect_success 'kvs: dropcache works' '
       flux kvs dropcache
'
test_expect_success 'kvs: dropcache --all works' '
       flux kvs dropcache --all
'
test_expect_success 'kvs: symlink: works' '
	TARGET=$TEST.a.b.c &&
	flux kvs put $TARGET=\"foo\" &&
	flux kvs link $TARGET $TEST.Q &&
	OUTPUT=$(flux kvs get $TEST.Q) &&
	test "$OUTPUT" = "foo"
'
test_expect_success 'kvs: symlink: readlink fails on regular value' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	! flux kvs readlink $TEST.a.b.c
'
test_expect_success 'kvs: symlink: readlink fails on directory' '
	${KVSBASIC} unlink $TEST &&
	flux kvs mkdir $TEST.a.b.c &&
	! flux kvs readlink $TEST.a.b.
'
test_expect_success 'kvs: symlink: path resolution when intermediate component is a symlink' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	OUTPUT=$(flux kvs get $TEST.Z.Y.c) &&
	test "$OUTPUT" = "42"
'
test_expect_success 'kvs: symlink: path resolution with intermediate symlink and nonexistent key' '
	${KVSBASIC} unlink $TEST &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	test_must_fail flux kvs get $TEST.Z.Y
'
test_expect_success 'kvs: symlink: intermediate symlink points to another symlink' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	flux kvs link $TEST.Z.Y $TEST.X.W &&
	test_kvs_key $TEST.X.W.c 42
'
test_expect_success 'kvs: symlink: intermediate symlinks are followed by put' '
	${KVSBASIC} unlink $TEST &&
	flux kvs mkdir $TEST.a &&
	flux kvs link $TEST.a $TEST.link &&
	flux kvs readlink $TEST.link >/dev/null &&
	flux kvs put $TEST.link.X=42 &&
	flux kvs readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42 &&
	test_kvs_key $TEST.a.X 42
'

# This will fail if individual ops are applied out of order
test_expect_success 'kvs: symlink: kvs_copy removes symlinked destination' '
	${KVSBASIC} unlink $TEST &&
	flux kvs mkdir $TEST.a &&
	flux kvs link $TEST.a $TEST.link &&
	flux kvs put $TEST.a.X=42 &&
	flux kvs copy $TEST.a $TEST.link &&
	! flux kvs readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42
'

# This will fail if individual ops are applied out of order
test_expect_success 'kvs: symlink: kvs_move works' '
	${KVSBASIC} unlink $TEST &&
	flux kvs mkdir $TEST.a &&
	flux kvs link $TEST.a $TEST.link &&
	flux kvs put $TEST.a.X=42 &&
	flux kvs move $TEST.a $TEST.link &&
	! flux kvs readlink $TEST.link >/dev/null &&
	test_kvs_key $TEST.link.X 42 &&
	! flux kvs dir $TEST.a >/dev/null
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (top)' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.X=42 &&
	flux kvs link $TEST.a $TEST.link &&
	flux kvs copy $TEST.link $TEST.copy &&
	LINKVAL=$(flux kvs readlink $TEST.copy) &&
	test "$LINKVAL" = "$TEST.a"
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (mid)' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.X=42 &&
	flux kvs link $TEST.a.b $TEST.a.link &&
	flux kvs copy $TEST.a $TEST.copy &&
	LINKVAL=$(flux kvs readlink $TEST.copy.link) &&
	test "$LINKVAL" = "$TEST.a.b"
'

test_expect_success 'kvs: symlink: kvs_copy does not follow symlinks (bottom)' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.X=42 &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
	flux kvs copy $TEST.a $TEST.copy &&
	LINKVAL=$(flux kvs readlink $TEST.copy.b.link) &&
	test "$LINKVAL" = "$TEST.a.b.X"
'

test_expect_success 'kvs: get_symlinkat works after symlink unlinked' '
	${KVSBASIC} unlink $TEST &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
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
	flux kvs mkdir $TEST.a &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "DIRREF"
'

test_expect_success 'kvs: get-treeobj: returns value for small value' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a=b &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "FILEVAL"
'

test_expect_success 'kvs: get-treeobj: returns value ref for large value' '
	${KVSBASIC} unlink $TEST &&
	dd if=/dev/zero bs=4096 count=1 | ${KVSBASIC} copy-tokvs $TEST.a - &&
	${KVSBASIC} get-treeobj $TEST.a | grep -q "FILEREF"
'

test_expect_success 'kvs: get-treeobj: returns link value for symlink' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.X=42 &&
	flux kvs link $TEST.a.b.X $TEST.a.b.link &&
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
	flux kvs put $TEST.a=42 &&
	${KVSBASIC} get-treeobj . >snapshot2 &&
	${KVSBASIC} put-treeobj $TEST.a="`cat snapshot2`" &&
	! flux kvs get $TEST.a &&
	flux kvs dir $TEST.a
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
	flux kvs put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj .) $TEST.a.b.c) = 42
'

test_expect_success 'kvs: getat: works on subdir from get-treeobj' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	test $(${KVSBASIC} getat $(${KVSBASIC} get-treeobj $TEST.a.b) c) = 42
'

test_expect_success 'kvs: getat: works on outdated root' '
	${KVSBASIC} unlink $TEST &&
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
	${KVSBASIC} unlink $TEST.dtree &&
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
	${KVSBASIC} unlink $TEST.dtree &&
	${FLUX_BUILD_DIR}/t/kvs/dtree --mkdir -h4 -w3 --prefix $TEST.dtree &&
	test $(flux kvs dir -R $TEST.dtree | wc -l) = 81
'

test_expect_success 'kvs: put key of . fails' '
	test_must_fail flux kvs put .=1
'

# Keep the next two tests in order
test_expect_success 'kvs: symlink: dangling link' '
	${KVSBASIC} unlink $TEST &&
	flux kvs link $TEST.dangle $TEST.a.b.c
'
test_expect_success 'kvs: symlink: readlink on dangling link' '
	OUTPUT=$(flux kvs readlink $TEST.a.b.c) &&
	test "$OUTPUT" = "$TEST.dangle"
'
test_expect_success 'kvs: symlink: readlink works on non-dangling link' '
	${KVSBASIC} unlink $TEST &&
	flux kvs put $TEST.a.b.c="foo" &&
	flux kvs link $TEST.a.b.c $TEST.link &&
	OUTPUT=$(flux kvs readlink $TEST.link) &&
	test "$OUTPUT" = "$TEST.a.b.c"
'

# Check for limit on link depth

test_expect_success 'kvs: symlink: error on link depth' '
	${KVSBASIC} unlink $TEST &&
        flux kvs put $TEST.a=1 &&
	flux kvs link $TEST.a $TEST.b &&
	flux kvs link $TEST.b $TEST.c &&
	flux kvs link $TEST.c $TEST.d &&
	flux kvs link $TEST.d $TEST.e &&
	flux kvs link $TEST.e $TEST.f &&
	flux kvs link $TEST.f $TEST.g &&
	flux kvs link $TEST.g $TEST.h &&
	flux kvs link $TEST.h $TEST.i &&
	flux kvs link $TEST.i $TEST.j &&
	flux kvs link $TEST.j $TEST.k &&
	flux kvs link $TEST.k $TEST.l &&
        test_must_fail flux kvs get $TEST.l
'

test_expect_success 'kvs: symlink: error on link depth, loop' '
	${KVSBASIC} unlink $TEST &&
	flux kvs link $TEST.link1 $TEST.link2 &&
	flux kvs link $TEST.link2 $TEST.link1 &&
        test_must_fail flux kvs get $TEST.link1
'

# test synchronization based on commit sequence no.

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	flux kvs put $TEST.xxx=99 &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && ${KVSBASIC} exists $TEST.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	${KVSBASIC} unlink $TEST.xxx &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && ! ${KVSBASIC} exists $TEST.xxx"
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

test_expect_success 'kvs: 256 simultaneous watches works' '
	${FLUX_BUILD_DIR}/t/kvs/watch simulwatch $TEST.a 256 &&
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
