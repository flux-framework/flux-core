#!/bin/sh
#

test_description='kvs internals tests in a flux session

These tests are deeper than basic KVS tests, testing to ensure
internal functionality of the KVS is working as designed.  These tests
will generally cover non-obvious issues/features that a general user
would be unaware of.
'

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b

test_kvs_key() {
	flux kvs get --json "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

#
# large value test
#
test_expect_success 'kvs: kvs get/put large raw values works' '
	flux kvs unlink -Rf $DIR &&
	dd if=/dev/urandom bs=4096 count=1 >random.data &&
	flux kvs put --raw $DIR.data=- <random.data &&
	flux kvs get --raw $DIR.data >reread.data &&
	test_cmp random.data reread.data
'

#
# key normalization tests
#
test_expect_success 'kvs: put with leading path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json ......$DIR.a.b.c=42 &&
	test_kvs_key $DIR.a.b.c 42
'
test_expect_success 'kvs: put with trailing path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json $DIR.a.b.c........=43 &&
	test_kvs_key $DIR.a.b.c 43
'
test_expect_success 'kvs: put with extra embedded path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json $DIR.....a....b...c=44 &&
	test_kvs_key $DIR.a.b.c 44
'
test_expect_success 'kvs: get with leading path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json $DIR.a.b.c=42 &&
	test_kvs_key ......$DIR.a.b.c 42
'
test_expect_success 'kvs: get with trailing path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json $DIR.a.b.c=43 &&
	test_kvs_key $DIR.a.b.c........ 43
'
test_expect_success 'kvs: get with extra embedded path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --json $DIR.a.b.c=44 &&
	test_kvs_key $DIR.....a....b...c 44
'

#
# zero-length value tests
#
test_expect_success 'kvs: zero-length value handled by put/get --raw' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get --raw $DIR.a >empty.output &&
	test_cmp /dev/null empty.output
'
test_expect_success 'kvs: zero-length value handled by get with no options' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get $DIR.a >empty2.output &&
	test_cmp /dev/null empty2.output
'
test_expect_success 'kvs: zero-length value handled by get --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get --treeobj $DIR.a
'
test_expect_success 'kvs: zero-length value NOT handled by get --json' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	test_must_fail flux kvs get --json $DIR.a
'
test_expect_success 'kvs: zero-length value is made by put with no options' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs get --raw $DIR.a >empty3.output &&
	test_cmp /dev/null empty3.output
'
test_expect_success 'kvs: zero-length value does not cause dir to fail' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs dir $DIR
'
test_expect_success 'kvs: zero-length value does not cause ls -FR to fail' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs ls -FR $DIR
'
test_expect_success 'kvs: append to zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs put --append $DIR.a=abc &&
	printf "%s\n" "abc" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: append a zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=abc &&
	flux kvs put --append $DIR.a= &&
	printf "%s\n" "abc" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: append a zero length value to zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs put --append $DIR.a= &&
	flux kvs get $DIR.a >empty.output &&
	test_cmp /dev/null empty.output
'

#
# ELOOP tests
#
test_expect_success 'kvs: link: error on link depth' '
	flux kvs unlink -Rf $DIR &&
        flux kvs put --json $DIR.a=1 &&
	flux kvs link $DIR.a $DIR.b &&
	flux kvs link $DIR.b $DIR.c &&
	flux kvs link $DIR.c $DIR.d &&
	flux kvs link $DIR.d $DIR.e &&
	flux kvs link $DIR.e $DIR.f &&
	flux kvs link $DIR.f $DIR.g &&
	flux kvs link $DIR.g $DIR.h &&
	flux kvs link $DIR.h $DIR.i &&
	flux kvs link $DIR.i $DIR.j &&
	flux kvs link $DIR.j $DIR.k &&
	flux kvs link $DIR.k $DIR.l &&
        test_must_fail flux kvs get --json $DIR.l
'
test_expect_success 'kvs: link: error on link depth, loop' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.link1 $DIR.link2 &&
	flux kvs link $DIR.link2 $DIR.link1 &&
        test_must_fail flux kvs get --json $DIR.link1
'

#
# kvs reads/writes of raw data to/from content store work
#

largeval="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
largevalhash="sha1-0b22e9fecf9c832032fe976e67058df0322dcc5c"

test_expect_success 'kvs: large put stores raw data into content store' '
	flux kvs unlink -Rf $DIR &&
 	flux kvs put --json $DIR.largeval=$largeval &&
	flux kvs get --treeobj $DIR.largeval | grep -q \"valref\" &&
	flux kvs get --treeobj $DIR.largeval | grep -q ${largevalhash} &&
	flux content load ${largevalhash} | grep $largeval
'

test_expect_success 'kvs: valref that points to content store data can be read' '
        flux kvs unlink -Rf $DIR &&
	echo "$largeval" | flux content store &&
	flux kvs put --treeobj $DIR.largeval2="{\"data\":[\"${largevalhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --json $DIR.largeval2 | grep $largeval
'

test_expect_success 'kvs: valref that points to zero size content store data can be read' '
	flux kvs unlink -Rf $DIR &&
        hashval=`flux content store </dev/null` &&
	flux kvs put --treeobj $DIR.empty="{\"data\":[\"${hashval}\"],\"type\":\"valref\",\"ver\":1}" &&
	test $(flux kvs get --raw $DIR.empty|wc -c) -eq 0
'

test_expect_success 'kvs: valref can point to other treeobjs' '
	flux kvs unlink -Rf $DIR &&
        flux kvs mkdir $DIR.a.b.c &&
        dirhash=`flux kvs get --treeobj $DIR.a.b.c | grep -P "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $DIR.value="{\"data\":[\"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.value | grep dir
'

#
# multi-blobref valrefs
#

test_expect_success 'kvs: multi blob-ref valref can be read' '
        flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.multival | grep "abcdefgh" &&
        test $(flux kvs get --raw $DIR.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on left, can be read' '
        flux kvs unlink -Rf $DIR &&
	hashval1=`flux content store < /dev/null` &&
	hashval2=`echo -n "abcd" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.multival | grep "abcd" &&
        test $(flux kvs get --raw $DIR.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on right, can be read' '
        flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.multival | grep "abcd" &&
        test $(flux kvs get --raw $DIR.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref in middle, can be read' '
        flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	hashval3=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\", \"${hashval3}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.multival | grep "abcdefgh" &&
        test $(flux kvs get --raw $DIR.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with a blobref pointing to a treeobj' '
        flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
        flux kvs mkdir $DIR.a.b.c &&
        dirhash=`flux kvs get --treeobj $DIR.a.b.c | grep -P "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
        flux kvs get --raw $DIR.multival | grep dir
'

#
# test synchronization based on commit sequence no.
#

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	flux kvs put --json $DIR.xxx=99 &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && flux kvs get --json $DIR.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	flux kvs unlink -Rf $DIR.xxx &&
	VERS=$(flux kvs version) &&
	flux exec sh -c "flux kvs wait ${VERS} && ! flux kvs get --json $DIR.xxx"
'

#
# test clear of stats
#

# each store of largeval will increase the noop store count, b/c we
# know that the identical large value will be cached as raw data

test_expect_success 'kvs: clear stats locally' '
        flux kvs unlink -Rf $DIR &&
        flux module stats -c kvs &&
        flux module stats kvs | grep no-op | grep -q 0 &&
        flux kvs put --json $DIR.largeval1=$largeval &&
        flux kvs put --json $DIR.largeval2=$largeval &&
        ! flux module stats kvs | grep no-op | grep -q 0 &&
        flux module stats -c kvs &&
        flux module stats kvs | grep no-op | grep -q 0
'

test_expect_success 'kvs: clear stats globally' '
        flux kvs unlink -Rf $DIR &&
        flux module stats -C kvs &&
        flux exec sh -c "flux module stats kvs | grep no-op | grep -q 0" &&
        for i in `seq 0 $((${SIZE} - 1))`; do
            flux exec -r $i sh -c "flux kvs put --json $DIR.$i.largeval1=$largeval $DIR.$i.largeval2=$largeval"
        done &&
        ! flux exec sh -c "flux module stats kvs | grep no-op | grep -q 0" &&
        flux module stats -C kvs &&
        flux exec sh -c "flux module stats kvs | grep no-op | grep -q 0"
'

test_done
