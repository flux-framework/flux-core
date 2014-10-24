#!/bin/sh
#

test_description='Test basic kvs usage in flux session

Verify basic KVS operations against a running flux comms session.
This test script verifies operation of the kvs and should be run
before other tests that depend on kvs.
'

. `dirname $0`/sharness.sh
test_under_flux 4

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
	flux kvs type "$1" >output
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
	flux kvs unlink $KEY &&
	  test_must_fail flux kvs get $KEY
'
test_expect_success 'kvs: value can be empty' '
	flux kvs put $KEY= &&
	  test_kvs_key $KEY "" &&
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
test_expect_success 'kvs: array put' '
	flux kvs put $KEY="[1,3,5,7]"
'
test_expect_success 'kvs: array type' '
	test_kvs_type $KEY array
'
test_expect_success 'kvs: array get' '
	test_kvs_key $KEY "[ 1, 3, 5, 7 ]"
'
test_expect_success 'kvs: object put' '
	flux kvs put $KEY="{\"a\":42}"
'
test_expect_success 'kvs: object type' '
	test_kvs_type $KEY object
'
test_expect_success 'kvs: object get' '
	test_kvs_key $KEY "{ \"a\": 42 }"
'
test_expect_success 'kvs: try to retrieve key as directory should fail' '
	test_must_fail flux kvs dir $KEY
'
test_expect_success 'kvs: try to retrieve a directory as key should fail' '
	test_must_fail flux kvs get $DIR
'

test_empty_directory() {
	OUTPUT=`flux kvs dir $1` &&
	test "x$OUTPUT" = "x"
}
test_expect_success 'kvs: empty directory remains after key removed' '
	flux kvs unlink $KEY &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: remove directory' '
	flux kvs unlink $TEST
'
test_expect_success 'kvs: empty directory can be created' '
	flux kvs mkdir $DIR  &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: put values in a directory then retrieve them' '
	flux kvs put $DIR.a=69 $DIR.b=70 $DIR.c=3.14 $DIR.d=\"snerg\" $DIR.e=true &&
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
	flux kvs unlink $TEST &&
	flux kvs put $DIR.a=69 $DIR.b=70 $DIR.c.d.e.f.g=3.14 $DIR.d=\"snerg\" $DIR.e=true &&
	flux kvs dir $DIR | sort >output &&
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
	flux kvs unlink $TEST &&
	flux kvs put $DIR.a=69 $DIR.b.c.d.e.f.g=70 $DIR.c.a.b=3.14 $DIR.d=\"snerg\" $DIR.e=true &&
	flux kvs dir $DIR | sort >output &&
	cat >expected <<EOF
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.140000
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: cleanup' '
	flux kvs unlink $TEST
'
test_expect_success 'kvs: symlink: works' '
	TARGET=$TEST.a.b.c &&
	flux kvs put $TARGET=\"foo\" &&
	flux kvs link $TARGET $TEST.Q &&
	OUTPUT=$(flux kvs get $TEST.Q) &&
	test "$OUTPUT" = "foo"
'
test_expect_success 'kvs: symlink: path resolution when intermediate component is a symlink' '
	flux kvs unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	OUTPUT=$(flux kvs get $TEST.Z.Y.c) &&
	test "$OUTPUT" = "42"
'
test_expect_success 'kvs: symlink: path resolution with intermediate symlink and nonexistent key' '
	flux kvs unlink $TEST &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	test_must_fail flux kvs get $TEST.Z.Y
'
test_expect_success 'kvs: symlink: intermediate symlink points to another symlink' '
	flux kvs unlink $TEST &&
	flux kvs put $TEST.a.b.c=42 &&
	flux kvs link $TEST.a.b $TEST.Z.Y &&
	flux kvs link $TEST.Z.Y $TEST.X.W &&
	test_kvs_key $TEST.X.W.c 42
'

# Keep the next two tests in order
test_expect_success 'kvs: symlink: dangling link' '
	flux kvs unlink $TEST &&
	flux kvs link $TEST.dangle $TEST.a.b.c
'
test_expect_success 'kvs: symlink: readlink on dangling link' '
	OUTPUT=$(flux kvs readlink $TEST.a.b.c) &&
	test "$OUTPUT" = "$TEST.dangle"
'
test_expect_success 'kvs: symlink: readlink works on non-dangling link' '
	flux kvs unlink $TEST &&
	flux kvs put $TEST.a.b.c="foo" &&
	flux kvs link $TEST.a.b.c $TEST.link &&
	OUTPUT=$(flux kvs readlink $TEST.link) &&
	test "$OUTPUT" = "$TEST.a.b.c"
'

test_expect_success 'kvs: tcommit: start 100 API threads each doing 50 put,commits in a loop' '
	${FLUX_BUILD_DIR}/src/test/tcommit 100 50 \
		$(basename ${SHARNESS_TEST_FILE})
'
test_expect_success 'kvs: tcommit: start 100 API threads each doing 50 put,fence in a loop' '
	${FLUX_BUILD_DIR}/src/test/tcommit --fence 100 50 \
		$(basename ${SHARNESS_TEST_FILE})
'
test_expect_success 'kvs: tkvswatch-mt: multi-threaded kvs watch program' '
	${FLUX_BUILD_DIR}/src/test/tkvswatch mt 100 100 TEST.a &&
	flux kvs unlink TEST.a
'

test_expect_success 'kvs: tkvswatch-selfmod: watch callback modifies watched key' '
	${FLUX_BUILD_DIR}/src/test/tkvswatch selfmod TEST.a &&
	flux kvs unlink TEST.a
'

test_done
