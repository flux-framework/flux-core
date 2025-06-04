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
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
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
# Cover value with a very large number of appends
# N.B. from 1000 to 3000 instead of 0 to 2000, easier to debug errors
# using fold(1) (i.e. all numbers same width)
test_expect_success LONGTEST 'load kvs and create some kvs content' '
       flux module load kvs &&
       for i in `seq 1000 3000`; do
	   flux kvs put --append bigval=${i}
       done &&
       flux kvs get bigval > bigval.exp
'
test_expect_success LONGTEST 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success LONGTEST 'unload kvs' '
       flux module remove kvs
'
test_expect_success LONGTEST 'flux-fsck works' '
	flux fsck --verbose 2> bigval.out &&
	grep "Checking integrity" bigval.out &&
	grep "bigval" bigval.out &&
	grep "Total errors: 0" bigval.out
'
test_expect_success 'load kvs' '
	flux module load kvs
'
# unfortunately we don't have a `flux content remove` command, so we'll corrupt
# a valref by overwriting a treeobj with a bad reference
test_expect_success 'make a reference invalid' '
	cat dirb.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > dirbbad.out &&
	flux kvs put --treeobj dir.b="$(cat dirbbad.out)"
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors' '
	test_must_fail flux fsck 2> fsckerrors1.out &&
	test_debug "cat fsckerrors1.out" &&
	count=$(cat fsckerrors1.out | wc -l) &&
	test $count -eq 3 &&
	grep "dir\.b" fsckerrors1.out | grep "missing blobref(s)" &&
	grep "Total errors: 1" fsckerrors1.out
'
test_expect_success 'flux-fsck --verbose outputs details' '
	test_must_fail flux fsck --verbose 2> fsckerrors1V.out &&
	test_debug "cat fsckerrors1V.out" &&
	grep "dir\.b" fsckerrors1V.out | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 1" fsckerrors1V.out
'
test_expect_success 'flux-fsck no output with --quiet' '
	test_must_fail flux fsck --quiet 2> fsckerrors2.out &&
	test_debug "cat fsckerrors2.out" &&
	count=$(cat fsckerrors2.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid' '
	cat dirc.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > dircbad1.out &&
	cat dircbad1.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > dircbad2.out &&
	flux kvs put --treeobj dir.c="$(cat dircbad2.out)"
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors' '
	test_must_fail flux fsck 2> fsckerrors3.out &&
	test_debug "cat fsckerrors3.out" &&
	count=$(cat fsckerrors3.out | wc -l) &&
	test $count -eq 4 &&
	grep "dir\.b" fsckerrors3.out | grep "missing blobref(s)" &&
	grep "dir\.c" fsckerrors3.out | grep "missing blobref(s)" &&
	grep "Total errors: 2" fsckerrors3.out
'
test_expect_success 'flux-fsck --verbose outputs details' '
	test_must_fail flux fsck --verbose 2> fsckerrors3V.out &&
	test_debug "cat fsckerrors3V.out" &&
	grep "dir\.b" fsckerrors3V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors3V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors3V.out | grep "missing blobref" | grep "index=2" &&
	grep "Total errors: 2" fsckerrors3V.out
'
test_expect_success 'flux-fsck no output with --quiet' '
	test_must_fail flux fsck --quiet 2> fsckerrors4.out &&
	test_debug "cat fsckerrors4.out" &&
	count=$(cat fsckerrors4.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'remove content & content-sqlite modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
