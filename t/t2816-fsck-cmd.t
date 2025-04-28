#!/bin/sh

test_description='Test flux fsck command'

. $(dirname $0)/sharness.sh

test_under_flux 1 minimal

test_expect_success 'load content and content-sqlite module' '
	flux module load content &&
	flux module load content-sqlite
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'create some kvs content' '
	flux kvs put dir.a=test &&
	flux kvs put dir.b=test1 &&
	flux kvs put --append dir.b=test2 &&
	flux kvs put --append dir.b=test3 &&
	flux kvs put --append dir.b=test4 &&
	flux kvs put dir.c=testA &&
	flux kvs put --append dir.c=testB &&
	flux kvs put --append dir.c=testC &&
	flux kvs put --append dir.c=testD &&
	flux kvs link dir alink &&
	flux kvs namespace create testns &&
	flux kvs put --namespace=testns dir.a=testns
'
# N.B. startlog commands in rc scripts normally ensures a checkpoint
# exists but we do this just to be extra sure
test_expect_success 'call --sync to ensure we have checkpointed' '
	flux kvs put --sync dir.sync=foo
'
test_expect_success 'save some treeobjs for later' '
	flux kvs get --treeobj dir.b > dirb.out &&
	flux kvs get --treeobj dir.c > dirc.out
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck works' '
	flux fsck 2> simple.out &&
        grep "Checking integrity" simple.out &&
        grep "Total errors: 0" simple.out
'
test_expect_success 'flux-fsck verbose works' '
	flux fsck --verbose 2> verbose.out &&
	grep "dir\.a" verbose.out &&
	grep "dir\.b" verbose.out &&
	grep "alink" verbose.out
'
test_expect_success 'load kvs' '
	flux module load kvs
'
# unfortunately we don't have a `flux content remove` command, so we'll corrupt
# a valref by overwriting a treeobj with a bad reference
test_expect_success 'make a reference invalid' '
	cat dirb.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > dirbbad.out &&
	flux kvs put --treeobj dir.b="$(cat dirbbad.out)"
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors' '
	test_must_fail flux fsck 2> fsckerrors1.out &&
	count=$(cat fsckerrors1.out | wc -l) &&
	test $count -eq 3 &&
	grep "dir\.b" fsckerrors1.out | grep "missing blobref" &&
        grep "Total errors: 1" fsckerrors1.out
'
test_expect_success 'flux-fsck no output with --quiet' '
	test_must_fail flux fsck --quiet 2> fsckerrors2.out &&
	count=$(cat fsckerrors2.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid' '
	cat dirc.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > dircbad.out &&
	flux kvs put --treeobj dir.c="$(cat dircbad.out)"
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors' '
	test_must_fail flux fsck 2> fsckerrors3.out &&
	count=$(cat fsckerrors3.out | wc -l) &&
	test $count -eq 4 &&
	grep "dir\.b" fsckerrors3.out | grep "missing blobref" &&
	grep "dir\.c" fsckerrors3.out | grep "missing blobref" &&
        grep "Total errors: 2" fsckerrors3.out
'
test_expect_success 'flux-fsck no output with --quiet' '
	test_must_fail flux fsck --quiet 2> fsckerrors4.out &&
	count=$(cat fsckerrors4.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'remove content & content-sqlite modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
