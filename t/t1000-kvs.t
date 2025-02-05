#!/bin/sh
#

test_description='Test flux-kvs and kvs in flux session

These are the basic/core flux kvs tests and should be run before
any other tests that require kvs functionality.
'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b
KEY=test.a.b.c
SUBDIR1=test.a.b.d
SUBDIR2=test.a.b.e

#
# Basic put, get, unlink tests
#

test_expect_success 'kvs: integer put' '
	flux kvs put $KEY.integer=42
'
test_expect_success 'kvs: double put' '
	flux kvs put $KEY.double=3.14
'
test_expect_success 'kvs: string put' '
	flux kvs put $KEY.string=foo
'
test_expect_success 'kvs: boolean true put' '
	flux kvs put $KEY.booleantrue=true
'
test_expect_success 'kvs: boolean false put' '
	flux kvs put $KEY.booleanfalse=false
'
test_expect_success 'kvs: array put' '
	flux kvs put $KEY.array="[1,3,5]"
'
test_expect_success 'kvs: object put' '
	flux kvs put $KEY.object="{\"a\":42}"
'
test_expect_success 'kvs: integer get' '
	test_kvs_key $KEY.integer 42
'
test_expect_success 'kvs: double get' '
	test_kvs_key $KEY.double 3.14
'
test_expect_success 'kvs: string get' '
	test_kvs_key $KEY.string foo
'
test_expect_success 'kvs: boolean true get' '
	test_kvs_key $KEY.booleantrue true
'
test_expect_success 'kvs: boolean false get' '
	test_kvs_key $KEY.booleanfalse false
'
test_expect_success 'kvs: array get' '
	test_kvs_key $KEY.array "[1,3,5]"
'
test_expect_success 'kvs: object get' '
	test_kvs_key $KEY.object "{\"a\":42}"
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.integer &&
	  test_must_fail flux kvs get $KEY.integer
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.double &&
	  test_must_fail flux kvs get $KEY.double
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.string &&
	  test_must_fail flux kvs get $KEY.string
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.booleantrue &&
	  test_must_fail flux kvs get $KEY.booleantrue
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.booleanfalse &&
	  test_must_fail flux kvs get $KEY.booleanfalse
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.array &&
	  test_must_fail flux kvs get $KEY.array
'
test_expect_success 'kvs: unlink works' '
	flux kvs unlink $KEY.object &&
	  test_must_fail flux kvs get $KEY.object
'

#
# Basic put, get, unlink tests w/ multiple key inputs
#

test_expect_success 'kvs: put (multiple)' '
	flux kvs put $KEY.a=42 $KEY.b=3.14 $KEY.c=foo $KEY.d=true $KEY.e="[1,3,5]" $KEY.f="{\"a\":42}"
'
test_expect_success 'kvs: get (multiple)' '
	flux kvs get $KEY.a $KEY.b $KEY.c $KEY.d $KEY.e $KEY.f >output &&
	cat >expected <<EOF &&
42
3.14
foo
true
[1,3,5]
{"a":42}
EOF
	test_cmp expected output
'
test_expect_success 'kvs: unlink (multiple)' '
	flux kvs unlink $KEY.a $KEY.b $KEY.c $KEY.d $KEY.e $KEY.f &&
          test_must_fail flux kvs get $KEY.a &&
          test_must_fail flux kvs get $KEY.b &&
          test_must_fail flux kvs get $KEY.c &&
          test_must_fail flux kvs get $KEY.d &&
          test_must_fail flux kvs get $KEY.e &&
          test_must_fail flux kvs get $KEY.f
'

#
# Basic dir tests
#

test_expect_success 'kvs: mkdir' '
	flux kvs mkdir $DIR
'
test_expect_success 'kvs: dir success listing empty dir' '
	flux kvs dir $DIR | sort >output &&
	cat >expected <<EOF &&
EOF
	test_cmp expected output
'
test_expect_success 'kvs: mkdir subdir' '
	flux kvs mkdir $SUBDIR1
'
test_expect_success 'kvs: dir lists subdir' '
	flux kvs dir $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.d.
EOF
	test_cmp expected output
'
test_expect_success 'kvs: dir -R lists subdir' '
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.d.
EOF
	test_cmp expected output
'
test_expect_success 'kvs: dir -R DIR' '
	flux kvs put $DIR.a=42 $DIR.b=3.14 $DIR.c=foo $DIR.d=true $DIR.e="[1,3,5]" $DIR.f="{\"a\":42}" &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 42
$DIR.b = 3.14
$DIR.c = foo
$DIR.d = true
$DIR.e = [1,3,5]
$DIR.f = {"a":42}
EOF
	test_cmp expected output
'
test_expect_success 'kvs: dir -R -d DIR' '
	flux kvs dir -R -d $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a
$DIR.b
$DIR.c
$DIR.d
$DIR.e
$DIR.f
EOF
	test_cmp expected output
'

test_expect_success 'kvs: kvs dir -R DIR with period end' '
	flux kvs dir -R $DIR. | sort >output &&
        cat >expected <<EOF &&
$DIR.a = 42
$DIR.b = 3.14
$DIR.c = foo
$DIR.d = true
$DIR.e = [1,3,5]
$DIR.f = {"a":42}
EOF
        test_cmp expected output
'

test_expect_success 'kvs: kvs dir -R -d DIR with period end' '
	flux kvs dir -R -d $DIR. | sort >output &&
        cat >expected <<EOF &&
$DIR.a
$DIR.b
$DIR.c
$DIR.d
$DIR.e
$DIR.f
EOF
        test_cmp expected output
'

test_expect_success 'kvs: kvs dir -R on root "."' '
	flux kvs dir -R "." | sort >output &&
        cat >expected <<EOF &&
$DIR.a = 42
$DIR.b = 3.14
$DIR.c = foo
$DIR.d = true
$DIR.e = [1,3,5]
$DIR.f = {"a":42}
EOF
        test_cmp expected output
'

test_expect_success 'kvs: kvs dir -R -d on root "."' '
	flux kvs dir -R -d "." | sort >output &&
        cat >expected <<EOF &&
$DIR.a
$DIR.b
$DIR.c
$DIR.d
$DIR.e
$DIR.f
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir works' '
        flux kvs unlink $SUBDIR1 &&
          test_must_fail flux kvs dir $SUBDIR1
'
test_expect_success 'kvs: unlink -R works' '
        flux kvs unlink -R $DIR &&
          test_must_fail flux kvs dir $DIR
'

#
# Basic dir tests (multiple inputs)
#

test_expect_success 'kvs: mkdir (multiple)' '
        flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR $SUBDIR1 $SUBDIR2
'
test_expect_success 'kvs: dir multiple subdirs' '
	flux kvs dir $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.d.
$DIR.e.
EOF
	test_cmp expected output
'

#
# More complex dir tests
#

test_expect_success 'kvs: create a dir with keys and subdir' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b=70 &&
        flux kvs put $DIR.c.d.e.f.g=3.14 &&
        flux kvs put $DIR.d=snerg &&
        flux kvs put $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 69
$DIR.b = 70
$DIR.c.d.e.f.g = 3.14
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

test_expect_success 'kvs: directory with multiple subdirs' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b.c.d.e.f.g=70 &&
        flux kvs put $DIR.c.a.b=3.14 &&
        flux kvs put $DIR.d=snerg &&
        flux kvs put $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.14
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

#
# get corner case tests
#

test_expect_success 'kvs: get a nonexistent key' '
	test_must_fail flux kvs get NOT.A.KEY
'
test_expect_success 'kvs: try to retrieve a directory as key should fail' '
        flux kvs mkdir $DIR.a.b.c &&
	test_must_fail flux kvs get $DIR
'

#
# put corner case tests
#

test_expect_success 'kvs: put with invalid input' '
	test_must_fail flux kvs put NOVALUE
'
test_expect_success 'kvs: put key of . fails' '
	test_must_fail flux kvs put .=1
'

#
# dir corner case tests
#

test_empty_directory() {
	OUTPUT=`flux kvs dir -R $1 | wc -l` &&
	test "x$OUTPUT" = "x0"
}

test_expect_success 'kvs: try to retrieve key as directory should fail' '
        flux kvs put $DIR.a.b.c.d=42 &&
	test_must_fail flux kvs dir $DIR.a.b.c.d
'
test_expect_success 'kvs: empty directory can be created' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR &&
	test_empty_directory $DIR
'

#
# unlink corner case tests
#

test_expect_success 'kvs: unlink nonexistent key fails' '
        test_must_fail flux kvs unlink NOT.A.KEY
'
test_expect_success 'kvs: unlink nonexistent key with -f does not fail' '
        flux kvs unlink -f NOT.A.KEY
'
test_expect_success 'kvs: unlink nonexistent dir with -f does not fail' '
        flux kvs unlink -Rf NOT.A.KEY
'
test_expect_success 'kvs: unlink non-empty dir fails' '
        flux kvs mkdir $SUBDIR1 $SUBDIR2 &&
	test_must_fail flux kvs unlink $DIR
'
test_expect_success 'kvs: unlink -R works' '
        flux kvs unlink -R $DIR &&
          test_must_fail flux kvs dir $SUBDIR1 &&
          test_must_fail flux kvs dir $SUBDIR2 &&
          test_must_fail flux kvs dir $DIR
'
test_expect_success 'kvs: empty directory remains after key removed' '
	flux kvs unlink -Rf $DIR &&
        flux kvs put $DIR.a=1 &&
        test_kvs_key $DIR.a 1 &&
        flux kvs unlink $DIR.a &&
	test_empty_directory $DIR
'
test_expect_success 'kvs: unlink works on link that points to invalid target' '
	flux kvs unlink -Rf $DIR &&
        flux kvs link invalid $DIR.link &&
        flux kvs unlink $DIR.link
'
test_expect_success 'kvs: unlink works on link that points to invalid namespace' '
	flux kvs unlink -Rf $DIR &&
        flux kvs put $DIR.foo=1 &&
        flux kvs link --target-namespace=invalid $DIR.foo $DIR.link &&
        flux kvs unlink $DIR.link
'
test_expect_success 'kvs: unlink works on link with infinite cycle' '
	flux kvs unlink -Rf $DIR &&
        flux kvs link $DIR.a $DIR.b &&
        flux kvs link $DIR.b $DIR.a &&
        flux kvs unlink $DIR.a &&
        flux kvs unlink $DIR.b &&
	test_empty_directory $DIR
'

#
# empty string corner case tests
#
test_expect_success 'kvs: put/get empty string' '
	flux kvs unlink -Rf $DIR &&
	echo -n "" | flux kvs put --raw $DIR.a=- &&
	flux kvs get $DIR.a >output &&
        echo -n "" > expected &&
	test_cmp output expected
'
test_expect_success 'kvs: dir can read and display empty string' '
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 
EOF
	test_cmp expected output
'

#
# get/put --raw tests
#
test_expect_success 'kvs: put/get --raw works with multiple key=val pairs' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a=xyz $DIR.b=zyx &&
	printf "%s" 'xyzzyx' >twovals.expected &&
	flux kvs get --raw $DIR.a $DIR.b >twovals.actual &&
	test_cmp twovals.expected twovals.actual
'
test_expect_success 'kvs: put --raw a=- reads value from stdin' '
	flux kvs unlink -Rf $DIR &&
	printf "%s" "abc" | flux kvs put --raw $DIR.a=- &&
	printf "%s" "abc" >rawstdin.expected &&
	flux kvs get --raw $DIR.a >rawstdin.actual &&
	test_cmp rawstdin.expected rawstdin.actual
'
test_expect_success 'kvs: put --raw a=- b=42 works' '
	flux kvs unlink -Rf $DIR &&
	printf "%s" "abc" | flux kvs put --raw $DIR.a=- $DIR.b=42 &&
	printf "%s" "abc42" >rawstdin2.expected &&
	flux kvs get --raw $DIR.a $DIR.b >rawstdin2.actual &&
	test_cmp rawstdin2.expected rawstdin2.actual
'
test_expect_success 'kvs: put --raw a=- b=- works with a getting all of stdin' '
	flux kvs unlink -Rf $DIR &&
	printf "%s" "abc" | flux kvs put --raw $DIR.a=- $DIR.b=- &&
	printf "%s" "abc" >rawstdin3a.expected &&
	flux kvs get --raw $DIR.a >rawstdin3a.actual &&
	flux kvs get --raw $DIR.b >rawstdin3b.actual &&
	test_cmp rawstdin3a.expected rawstdin3a.actual &&
	test_cmp /dev/null rawstdin3b.actual
'

#
# get/put --treeobj tests
#
test_expect_success 'kvs: get --treeobj: returns dirref object for root' '
	flux kvs get --treeobj . | grep -q \"dirref\"
'
test_expect_success 'kvs: treeobj of all types handled by get --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.val=hello &&
	flux kvs put $DIR.valref=$(seq -s 1 100) &&
	flux kvs mkdir $DIR.dirref &&
	flux kvs link foo $DIR.symlink &&
	flux kvs link --target-namespace=A bar $DIR.symlinkNS &&
	flux kvs get --treeobj $DIR.val | grep -q val &&
	flux kvs get --treeobj $DIR.valref | grep -q valref &&
	flux kvs get --treeobj $DIR.dirref | grep -q dirref &&
	flux kvs get --treeobj $DIR.symlink | grep -q symlink &&
	flux kvs get --treeobj $DIR.symlinkNS | grep -q symlink
'
test_expect_success 'kvs: get --treeobj: returns value ref for large value' '
	flux kvs unlink -Rf $DIR &&
	dd if=/dev/zero bs=4096 count=1 | flux kvs put --raw $DIR.a=- &&
	flux kvs get --treeobj $DIR.a | grep -q \"valref\"
'
test_expect_success 'kvs: treeobj is created by put --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --treeobj $DIR.val="{\"data\":\"YgA=\",\"type\":\"val\",\"ver\":1}" &&
	flux kvs get $DIR.val >val.output &&
	echo "b" >val.expected &&
	test_cmp val.expected val.output
'
test_expect_success 'kvs: put --treeobj: can make root snapshot' '
       flux kvs unlink -Rf $DIR &&
       flux kvs get --treeobj . >snapshot &&
       flux kvs put --treeobj $DIR.a="`cat snapshot`" &&
       flux kvs get --treeobj $DIR.a >snapshot.cpy &&
       test_cmp snapshot snapshot.cpy
'
test_expect_success 'kvs: treeobj can be used to create arbitrary snapshot' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.a=hello &&
	flux kvs put $DIR.a.b=goodbye &&
	flux kvs put --treeobj $DIR.b=$(flux kvs get --treeobj $DIR.a) &&
	(flux kvs get $DIR.a.a && flux kvs get $DIR.a.b) >snap.expected &&
	(flux kvs get $DIR.b.a && flux kvs get $DIR.b.b) >snap.actual &&
	test_cmp snap.expected snap.actual
'

#
# get/put --treeobj corner case tests
#

test_expect_success 'kvs: put --treeobj: clobbers destination' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=42 &&
	flux kvs get --treeobj . >snapshot2 &&
	flux kvs put --treeobj $DIR.a="`cat snapshot2`" &&
	! flux kvs get $DIR.a &&
	flux kvs dir $DIR.a
'
test_expect_success 'kvs: put --treeobj: fails bad dirent: not JSON' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs put --treeobj $DIR.a=xyz
'
test_expect_success 'kvs: put --treeobj: fails bad dirent: unknown type' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":\"MQA=\",\"type\":\"FOO\",\"ver\":1}"
'
test_expect_success 'kvs: put --treeobj: fails bad dirent: bad link data' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":42,\"type\":\"symlink\",\"ver\":1}"
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: bad ref data' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":42,\"type\":\"dirref\",\"ver\":1}" &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":"sha1-4087718d190b373fb490b27873f61552d7f29dbe",\"type\":\"dirref\",\"ver\":1}"
'

test_expect_success 'kvs: put --treeobj: fails bad dirent: bad blobref' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" &&
	test_must_fail flux kvs put --treeobj $DIR.a="{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}"
'

#
# put --append tests
#
test_expect_success 'kvs: append on non-existent key is same as create' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --append $DIR.a=abc &&
	printf "%s\n" "abc" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: basic append works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=abc &&
	flux kvs put --append $DIR.a=def &&
	flux kvs put --append $DIR.a=ghi &&
	printf "%s%s%s\n" "abcdefghi" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: basic append works with --raw' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a=abc &&
	flux kvs put --append --raw $DIR.a=def &&
	flux kvs put --append --raw $DIR.a=ghi &&
	printf "%s" "abcdefghi" >expected &&
	flux kvs get --raw $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: basic append works with newlines' '
	flux kvs unlink -Rf $DIR &&
	echo "abc" | flux kvs put --raw $DIR.a=- &&
	echo "def" | flux kvs put --append --raw $DIR.a=- &&
	echo "ghi" | flux kvs put --append --raw $DIR.a=- &&
	printf "%s\n%s\n%s\n" "abc" "def" "ghi" >expected &&
	flux kvs get --raw $DIR.a >output &&
	test_cmp output expected
'

#
# put --no-merge tests
#
test_expect_success 'kvs: put using --no-merge flag' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --no-merge $DIR.a=69 &&
        flux kvs put --no-merge $DIR.b.c.d.e.f.g=70 &&
        flux kvs put --no-merge $DIR.c.a.b=3.14 &&
        flux kvs put --no-merge $DIR.d=snerg &&
        flux kvs put --no-merge $DIR.e=true &&
	flux kvs dir -R $DIR | sort >output &&
	cat >expected <<EOF &&
$DIR.a = 69
$DIR.b.c.d.e.f.g = 70
$DIR.c.a.b = 3.14
$DIR.d = snerg
$DIR.e = true
EOF
	test_cmp expected output
'

#
# ls tests
#

test_expect_success 'kvs: ls -1F works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR &&
	flux kvs ls -1F >output &&
	cat >expected <<-EOF &&
	test.
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -1F . works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR &&
	flux kvs ls -1F . >output &&
	cat >expected <<-EOF &&
	test.
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -1F DIR works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
	flux kvs mkdir $DIR.b &&
	flux kvs link b $DIR.c &&
	flux kvs link --target-namespace=foo c $DIR.d &&
	flux kvs ls -1F $DIR >output &&
	cat >expected <<-EOF &&
	a
	b.
	c@
	d@
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -1F DIR. works' '
	flux kvs ls -1F $DIR. >output &&
	cat >expected <<-EOF &&
	a
	b.
	c@
	d@
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -1Fd DIR.a DIR.b DIR.c DIR.d works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
	flux kvs mkdir $DIR.b &&
	flux kvs link b $DIR.c &&
	flux kvs link --target-namespace=foo c $DIR.d &&
	flux kvs ls -1Fd $DIR.a $DIR.b $DIR.c $DIR.d >output &&
	cat >expected <<-EOF &&
	$DIR.a
	$DIR.b.
	$DIR.c@
	$DIR.d@
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -1RF shows directory titles' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
	flux kvs put $DIR.b.d=42 &&
	flux kvs link b $DIR.c &&
	flux kvs ls -1RF $DIR | grep : | wc -l >output &&
	cat >expected <<-EOF &&
	2
	EOF
	test_cmp expected output
'
# test assumes COLUMNS environment not set, clear in a subshell just in case
test_expect_success 'kvs: ls with no options adjusts output width to 80' '
	flux kvs unlink -Rf $DIR &&
	${FLUX_BUILD_DIR}/t/kvs/dtree -p$DIR -h1 -w50 &&
	$(unset COLUMNS; flux kvs ls $DIR | wc -wl >output) &&
	cat >expected <<-EOF &&
	      5      50
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -w40 adjusts output width to 40' '
	flux kvs unlink -Rf $DIR &&
	${FLUX_BUILD_DIR}/t/kvs/dtree -p$DIR -h1 -w50 &&
	flux kvs ls -w40 $DIR | wc -wl >output &&
	cat >expected <<-EOF &&
	     10      50
	EOF
	test_cmp expected output
'
test_columns_variable_preserved && test_set_prereq USE_COLUMNS
test_expect_success USE_COLUMNS 'kvs: ls with COLUMNS=20 adjusts output width to 20' '
	flux kvs unlink -Rf $DIR &&
	${FLUX_BUILD_DIR}/t/kvs/dtree -p$DIR -h1 -w50 &&
	COLUMNS=20 flux kvs ls $DIR | wc -wl >output &&
	cat >expected <<-EOF &&
	     25      50
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls -R lists deep directory hierarchy' '
	flux kvs unlink -Rf $DIR &&
	${FLUX_BUILD_DIR}/t/kvs/dtree -p$DIR -h8 -w1 &&
	flux kvs ls -R $DIR >output &&
	cat >expected <<-EOF &&
	$DIR:
	0000

	$DIR.0000:
	0000

	$DIR.0000.0000:
	0000

	$DIR.0000.0000.0000:
	0000

	$DIR.0000.0000.0000.0000:
	0000

	$DIR.0000.0000.0000.0000.0000:
	0000

	$DIR.0000.0000.0000.0000.0000.0000:
	0000

	$DIR.0000.0000.0000.0000.0000.0000.0000:
	0000
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls key. works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a &&
	flux kvs ls -d $DIR.a. >output &&
	cat >expected <<-EOF &&
	$DIR.a
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls key. fails if key is not a directory' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=42 &&
	test_must_fail flux kvs ls -d $DIR.a.
'
test_expect_success 'kvs: ls key. fails if key does not exist' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs ls $DIR.a
'
test_expect_success 'kvs: ls does not follow symlink with -d' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.foo=1 &&
	flux kvs link $DIR.foo $DIR.link &&
	flux kvs ls -d $DIR.link >output &&
	cat >expected <<-EOF &&
	$DIR.link
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls does not follow symlink with -F' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.foo=1 &&
	flux kvs link $DIR.foo $DIR.link &&
	flux kvs ls -F $DIR.link >output &&
	cat >expected <<-EOF &&
	$DIR.link@
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls outputs linkname when link points to value' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.foo=1 &&
	flux kvs link $DIR.foo $DIR.link &&
	flux kvs ls $DIR.link >output &&
	cat >expected <<-EOF &&
	$DIR.link
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls outputs linkname when link points to invalid target' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link invalid $DIR.link &&
	flux kvs ls $DIR.link >output &&
	cat >expected <<-EOF &&
	$DIR.link
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls outputs dir when link points to dir' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b=1 &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs ls $DIR.link >output &&
	cat >expected <<-EOF &&
	b
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: ls outputs dir and header when link points to dir and -R' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b=1 &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs ls -R $DIR.link >output &&
	cat >expected <<-EOF &&
	$DIR.link:
	b
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: namespace create setup' '
	flux kvs namespace create TESTLSNS
'
test_expect_success 'kvs: ls --namespace -1F DIR works' '
	flux kvs unlink --namespace=TESTLSNS -Rf $DIR.ns &&
	flux kvs put --namespace=TESTLSNS $DIR.ns.a=69 &&
	flux kvs mkdir --namespace=TESTLSNS $DIR.ns.b &&
	flux kvs link --namespace=TESTLSNS b $DIR.ns.c &&
	flux kvs link --namespace=TESTLSNS --target-namespace=foo c $DIR.ns.d &&
	flux kvs ls --namespace=TESTLSNS -1F $DIR.ns >output &&
	cat >expected <<-EOF &&
	a
	b.
	c@
	d@
	EOF
	test_cmp expected output
'
test_expect_success 'kvs: namespace remove cleanup' '
	flux kvs namespace remove TESTLSNS
'

#
# link/readlink tests
#

test_expect_success 'kvs: link works' '
	TARGET=$DIR.target &&
	flux kvs put $TARGET="foo" &&
	flux kvs link $TARGET $DIR.link &&
	OUTPUT=$(flux kvs get $DIR.link) &&
	test "$OUTPUT" = "foo"
'
test_expect_success 'kvs: readlink works' '
	TARGET=$DIR.target &&
	flux kvs put $TARGET="foo" &&
	flux kvs link $TARGET $DIR.link &&
	OUTPUT=$(flux kvs readlink $DIR.link) &&
	test "$OUTPUT" = "$TARGET"
'
test_expect_success 'kvs: readlink works (multiple inputs)' '
	TARGET1=$DIR.target1 &&
	TARGET2=$DIR.target2 &&
	flux kvs put $TARGET1="foo1" &&
	flux kvs put $TARGET2="foo2" &&
	flux kvs link $TARGET1 $DIR.link1 &&
	flux kvs link $TARGET2 $DIR.link2 &&
	flux kvs readlink $DIR.link1 $DIR.link2 >output &&
	cat >expected <<EOF &&
$TARGET1
$TARGET2
EOF
	test_cmp output expected
'
test_expect_success 'kvs: link: path resolution when intermediate component is a link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	flux kvs link $DIR.a.b $DIR.Z.Y &&
	OUTPUT=$(flux kvs get $DIR.Z.Y.c) &&
	test "$OUTPUT" = "42"
'
test_expect_success 'kvs: link: intermediate link points to another link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	flux kvs link $DIR.a.b $DIR.Z.Y &&
	flux kvs link $DIR.Z.Y $DIR.X.W &&
	test_kvs_key $DIR.X.W.c 42
'
test_expect_success 'kvs: link: intermediate links are followed by put' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs readlink $DIR.link >/dev/null &&
	flux kvs put $DIR.link.X=42 &&
	flux kvs readlink $DIR.link >/dev/null &&
	test_kvs_key $DIR.link.X 42 &&
	test_kvs_key $DIR.a.X 42
'
# This will fail if individual ops are applied out of order
test_expect_success 'kvs: link: copy removes linked destination' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs put $DIR.a.X=42 &&
	flux kvs copy $DIR.a $DIR.link &&
	! flux kvs readlink $DIR.link >/dev/null &&
	test_kvs_key $DIR.link.X 42
'
# This will fail if individual ops are applied out of order
test_expect_success 'kvs: link: move works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs put $DIR.a.X=42 &&
	flux kvs move $DIR.a $DIR.link &&
	! flux kvs readlink $DIR.link >/dev/null &&
	test_kvs_key $DIR.link.X 42 &&
	! flux kvs dir $DIR.a >/dev/null
'
test_expect_success 'kvs: link: copy does not follow links (top)' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.X=42 &&
	flux kvs link $DIR.a $DIR.link &&
	flux kvs copy $DIR.link $DIR.copy &&
	LINKVAL=$(flux kvs readlink $DIR.copy) &&
	test "$LINKVAL" = "$DIR.a"
'
test_expect_success 'kvs: link: copy does not follow links (mid)' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.X=42 &&
	flux kvs link $DIR.a.b $DIR.a.link &&
	flux kvs copy $DIR.a $DIR.copy &&
	LINKVAL=$(flux kvs readlink $DIR.copy.link) &&
	test "$LINKVAL" = "$DIR.a.b"
'
test_expect_success 'kvs: link: copy does not follow links (bottom)' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.X=42 &&
	flux kvs link $DIR.a.b.X $DIR.a.b.link &&
	flux kvs copy $DIR.a $DIR.copy &&
	LINKVAL=$(flux kvs readlink $DIR.copy.b.link) &&
	test "$LINKVAL" = "$DIR.a.b.X"
'
test_expect_success 'kvs: link: dangling link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.dangle $DIR.a.b.c
'
test_expect_success 'kvs: link: readlink on dangling link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.dangle $DIR.a.b.c &&
	OUTPUT=$(flux kvs readlink $DIR.a.b.c) &&
	test "$OUTPUT" = "$DIR.dangle"
'

#
# link/readlink corner case tests
#

test_expect_success 'kvs: readlink fails on regular value' '
        flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.target=42 &&
	! flux kvs readlink $DIR.target
'
test_expect_success 'kvs: readlink fails on directory' '
        flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a.b.c &&
	! flux kvs readlink $DIR.a.b.
'
test_expect_success 'kvs: link: path resolution with intermediate link and nonexistent key' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.a.b $DIR.Z.Y &&
	test_must_fail flux kvs get $DIR.Z.Y
'

#
# link/readlink tests - across namespace
#

test_expect_success 'kvs: namespace create setup' '
	flux kvs namespace create TESTSYMLINKNS
'
test_expect_success 'kvs: symlink w/ Namespace works' '
	TARGET=$DIR.target &&
	flux kvs put --namespace=TESTSYMLINKNS $TARGET="foo" &&
	flux kvs link --target-namespace=TESTSYMLINKNS $TARGET $DIR.symlinkNS &&
	OUTPUT=$(flux kvs get $DIR.symlinkNS) &&
	test "$OUTPUT" = "foo"
'
test_expect_success 'kvs: symlink w/ Namespace fails on bad namespace' '
	TARGET=$DIR.target &&
	flux kvs put $TARGET="foo" &&
	flux kvs link --target-namespace=TESTSYMLINKNS-FAKE $TARGET $DIR.symlinkNS &&
	! flux kvs get $DIR.symlinkNS
'
test_expect_success 'kvs: readlink on symlink w/ Namespace works' '
	TARGET=$DIR.target &&
	flux kvs put $TARGET="foo" &&
	flux kvs link --target-namespace=TESTSYMLINKNS $TARGET $DIR.symlinkNS &&
	OUTPUT=$(flux kvs readlink $DIR.symlinkNS) &&
	test "$OUTPUT" = "TESTSYMLINKNS::$TARGET"
'
test_expect_success 'kvs: readlink works with nslnks (multiple inputs)' '
	TARGET1=$DIR.target1 &&
	TARGET2=$DIR.target2 &&
	flux kvs put --namespace=TESTSYMLINKNS $TARGET1="foo1" &&
	flux kvs put --namespace=TESTSYMLINKNS $TARGET2="foo2" &&
	flux kvs link --target-namespace=TESTSYMLINKNS $TARGET1 $DIR.symlinkNS1 &&
	flux kvs link --target-namespace=TESTSYMLINKNS $TARGET2 $DIR.symlinkNS2 &&
	flux kvs readlink $DIR.symlinkNS1 $DIR.symlinkNS2 >output &&
	cat >expected <<EOF &&
TESTSYMLINKNS::$TARGET1
TESTSYMLINKNS::$TARGET2
EOF
	test_cmp output expected
'
test_expect_success 'kvs: symlinkNS: path resolution when intermediate component is a link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs put --namespace=TESTSYMLINKNS $DIR.a.b.c=42 &&
	flux kvs link --target-namespace=TESTSYMLINKNS $DIR.a.b $DIR.Z.Y &&
	OUTPUT=$(flux kvs get $DIR.Z.Y.c) &&
	test "$OUTPUT" = "42"
'
test_expect_success 'kvs: symlinkNS: intermediate link points to another namespace link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs put --namespace=TESTSYMLINKNS $DIR.a.b.c=42 &&
	flux kvs link --namespace=TESTSYMLINKNS --target-namespace=TESTSYMLINKNS $DIR.a.b $DIR.Z.Y &&
	flux kvs link --target-namespace=TESTSYMLINKNS $DIR.Z.Y $DIR.X.W &&
	test_kvs_key $DIR.X.W.c 42
'
test_expect_success 'kvs: symlinkNS: intermediate link points to another symlink' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs put --namespace=TESTSYMLINKNS $DIR.a.b.c=42 &&
	flux kvs link --namespace=TESTSYMLINKNS $DIR.a.b $DIR.Z.Y &&
	flux kvs link --target-namespace=TESTSYMLINKNS $DIR.Z.Y $DIR.X.W &&
	test_kvs_key $DIR.X.W.c 42
'
test_expect_success 'kvs: symlinkNS: put cant cross namespace' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs mkdir --namespace=TESTSYMLINKNS $DIR.a &&
	flux kvs link --target-namespace=TESTSYMLINKNS $DIR.a $DIR.link &&
	! flux kvs put $DIR.link.X=42
'
test_expect_success 'kvs: symlinkNS: dangling link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs link --target-namespace=TESTSYMLINKNS-FAKE $DIR.dangle $DIR.a.b.c
'
test_expect_success 'kvs: symlinkNS: readlink on dangling link' '
	flux kvs unlink -Rf $DIR &&
	flux kvs unlink --namespace=TESTSYMLINKNS -Rf $DIR &&
	flux kvs link --target-namespace=TESTSYMLINKNS-FAKE $DIR.dangle $DIR.a.b.c &&
	OUTPUT=$(flux kvs readlink $DIR.a.b.c) &&
	test "$OUTPUT" = "TESTSYMLINKNS-FAKE::$DIR.dangle"
'
test_expect_success 'kvs: namespace remove cleanup' '
	flux kvs namespace remove TESTSYMLINKNS
'

#
# get --at tests
#

test_expect_success 'kvs: get --at: works on root from get --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	test $(flux kvs get --at $(flux kvs get --treeobj .) $DIR.a.b.c) = 42
'

test_expect_success 'kvs: get --at: works on subdir from get --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	test $(flux kvs get --at $(flux kvs get --treeobj $DIR.a.b) c) = 42
'

test_expect_success 'kvs: get --at: works on outdated root' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	ROOTREF=$(flux kvs get --treeobj .) &&
	flux kvs put $DIR.a.b.c=43 &&
	test $(flux kvs get --at $ROOTREF $DIR.a.b.c) = 42
'
test_expect_success 'kvs: readlink --at works after symlink unlinked' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.a.b.X $DIR.a.b.link &&
	ROOTREF=$(flux kvs get --treeobj .) &&
	flux kvs unlink -R $DIR &&
	LINKVAL=$(flux kvs readlink --at $ROOTREF $DIR.a.b.link) &&
	test "$LINKVAL" = "$DIR.a.b.X"
'
test_expect_success 'kvs: readlink --at works after symlink w/ Namespace unlinked' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link --target-namespace=foo $DIR.a.b.X $DIR.a.b.link &&
	ROOTREF=$(flux kvs get --treeobj .) &&
	flux kvs unlink -R $DIR &&
	LINKVAL=$(flux kvs readlink --at $ROOTREF $DIR.a.b.link) &&
	test "$LINKVAL" = "foo::$DIR.a.b.X"
'
test_expect_success 'kvs: directory with multiple subdirs using dir --at' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=69 &&
        flux kvs put $DIR.b.c.d.e.f.g=70 &&
        flux kvs put $DIR.c.a.b=3.14 &&
        flux kvs put $DIR.d=snerg &&
        flux kvs put $DIR.e=true &&
        flux kvs link $DIR.a $DIR.f &&
        DIRREF=$(flux kvs get --treeobj $DIR) &&
	flux kvs dir -R --at $DIRREF . | sort >output &&
	cat >expected <<EOF &&
a = 69
b.c.d.e.f.g = 70
c.a.b = 3.14
d = snerg
e = true
f -> $DIR.a
EOF
	test_cmp expected output
'

#
# get --at corner cases
#

test_expect_success 'kvs: get --at: fails bad on dirent' '
	flux kvs unlink -Rf $DIR &&
	test_must_fail flux kvs get --at 42 $DIR.a &&
	test_must_fail flux kvs get --at "{\"data\":[\"sha1-aaa\"],\"type\":\"dirref\",\"ver\":1}" $DIR.a &&
	test_must_fail flux kvs get --at "{\"data\":[\"sha1-bbb\"],\"type\":\"dirref\",\"ver\":1}" $DIR.a &&
	test_must_fail flux kvs get --at "{\"data\":42,\"type\":\"dirref\",\"ver\":1}" $DIR.a &&
	test_must_fail flux kvs get --at "{\"data\":"sha1-4087718d190b373fb490b27873f61552d7f29dbe",\"type\":\"dirref\",\"ver\":1}" $DIR.a
'

#
# -O, -b, -s options in write commands
#

test_expect_success 'kvs: --treeobj-root on write ops works' '
	flux kvs unlink -Rf $DIR &&
        flux kvs put -O $DIR.a=1 > output &&
        grep "dirref" output &&
        flux kvs unlink -O $DIR.a > output &&
        grep "dirref" output &&
        flux kvs mkdir -O $DIR.a > output &&
        grep "dirref" output &&
        flux kvs link -O $DIR.a $DIR.b > output &&
        grep "dirref" output
'

test_expect_success 'kvs: --blobref on write ops works' '
	flux kvs unlink -Rf $DIR &&
        flux kvs put -b $DIR.a=1 > output &&
        flux kvs getroot -b > expected &&
        test_cmp output expected &&
        flux kvs unlink -b $DIR.a > output &&
        flux kvs getroot -b > expected &&
        test_cmp output expected &&
        flux kvs mkdir -b $DIR.a > output &&
        flux kvs getroot -b > expected &&
        test_cmp output expected &&
        flux kvs link -b $DIR.a $DIR.b > output &&
        flux kvs getroot -b > expected &&
        test_cmp output expected
'

test_expect_success 'kvs: --sequence on write ops works' '
	flux kvs unlink -Rf $DIR &&
        VER=$(flux kvs version) &&
        VER=$((VER + 1)) &&
        SEQ=$(flux kvs put -s $DIR.a=1) &&
        test $VER -eq $SEQ &&
        VER=$((VER + 1)) &&
        SEQ=$(flux kvs unlink -s $DIR.a) &&
        test $VER -eq $SEQ &&
        VER=$((VER + 1)) &&
        SEQ=$(flux kvs mkdir -s $DIR.a) &&
        test $VER -eq $SEQ &&
        VER=$((VER + 1)) &&
        SEQ=$(flux kvs link -s $DIR.a $DIR.b) &&
        test $VER -eq $SEQ
'

#
# version/wait tests
#

test_expect_success NO_CHAIN_LINT 'kvs: version and wait' '
	VERS=$(flux kvs version)
        VERS=$((VERS + 1))
        flux kvs wait $VERS &
        kvswaitpid=$! &&
        flux kvs put $DIR.xxx=99 &&
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success 'flux kvs getroot returns valid dirref object' '
	flux kvs put test.a=42 &&
	DIRREF=$(flux kvs getroot) &&
	flux kvs put test.a=43 &&
	flux kvs get --at "$DIRREF" test.a >get.out &&
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

#
# getroot tests
#

test_expect_success 'flux kvs getroot --sequence returns increasing rootseq' '
	SEQ=$(flux kvs getroot --sequence) &&
	flux kvs put test.b=hello &&
	SEQ2=$(flux kvs getroot --sequence) &&
	test $SEQ -lt $SEQ2
'

test_expect_success 'flux kvs getroot --owner returns instance owner' '
	OWNER=$(flux kvs getroot --owner) &&
	test $OWNER -eq $(id -u)
'

test_expect_success 'flux kvs getroot --blobref returns changing blobrefs' '
	BLOBREF1=$(flux kvs getroot --blobref) &&
	flux kvs put test.c=barf &&
	BLOBREF2=$(flux kvs getroot --blobref) &&
	test $BLOBREF1 != $BLOBREF2
'

test_expect_success 'flux kvs getroot works on alt namespace' '
	flux kvs namespace create testns1 &&
	SEQ=$(flux kvs getroot --namespace=testns1 --sequence) &&
	test $SEQ -eq 0 &&
	flux kvs put --namespace=testns1 test.c=moop &&
	SEQ2=$(flux kvs getroot --namespace=testns1 --sequence) &&
	test $SEQ -lt $SEQ2 &&
	flux kvs namespace remove testns1
'

#
# Other get options
#

test_expect_success 'kvs: get --label works' '
	flux kvs put test.ZZZ=42 &&
	flux kvs get --label test.ZZZ |grep test.ZZZ=42
'

#
# Malformed requests
#
test_expect_success 'getroot request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.getroot 71 </dev/null
'
test_expect_success 'wait-version request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.wait-version 71 </dev/null
'
test_expect_success 'lookup request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.lookup 71 </dev/null
'
test_expect_success 'lookup-plus request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.lookup-plus 71 </dev/null
'
test_expect_success 'commit request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.commit 71 </dev/null
'
test_expect_success 'namespace-create request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.namespace-create 71 </dev/null
'
test_expect_success 'namespace-remove request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.namespace-remove 71 </dev/null
'
test_expect_success 'setroot-pause request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.setroot-pause 71 </dev/null
'
test_expect_success 'setroot-unpause request with empty payload fails with EPROTO(71)' '
	${RPC} kvs.setroot-unpause 71 </dev/null
'

#
# ensure no lingering pending requests
#

test_expect_success 'kvs: no pending requests at end of tests before module removal' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0
'

#
# module corner cases
#

test_expect_success 'module fails to load with unknown option' '
	flux module remove kvs &&
	test_must_fail flux module load kvs badopt
'

test_expect_success 'module fails to load with bad input to transaction-merge' '
        test_must_fail flux module reload kvs transaction-merge=foobar
'

test_done
