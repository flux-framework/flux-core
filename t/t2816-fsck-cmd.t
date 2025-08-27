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
test_expect_success 'flux-fsck fails if kvs loaded' '
	test_must_fail flux fsck
'
test_expect_success 'create some kvs content' '
	flux kvs put dir.a=test &&
	flux kvs getroot -b > a.rootref &&
	flux kvs put dir.b=test1 &&
	flux kvs put --append dir.b=test2 &&
	flux kvs put --append dir.b=test3 &&
	flux kvs put --append dir.b=test4 &&
	flux kvs getroot -b > b.rootref &&
	flux kvs put dir.c=testA &&
	flux kvs put --append dir.c=testB &&
	flux kvs put --append dir.c=testC &&
	flux kvs put --append dir.c=testD &&
	flux kvs getroot -b > c.rootref &&
	flux kvs put dir.d=testE &&
	flux kvs put --append dir.d=testF &&
	flux kvs getroot -b > d.rootref &&
	flux kvs mkdir dir.adir &&
	flux kvs mkdir dir.bdir &&
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
	flux kvs get --treeobj dir.c > dirc.out &&
	flux kvs get --treeobj dir.d > dird.out
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck works (simple)' '
	flux fsck 2> simple.out &&
	grep "Checking integrity" simple.out &&
	grep "Total errors: 0" simple.out
'
test_expect_success 'flux-fsck verbose works (simple)' '
	flux fsck --verbose 2> verbose.out &&
	grep "dir$" verbose.out &&
	grep "dir\.a" verbose.out &&
	grep "dir\.b" verbose.out &&
	grep "dir\.c" verbose.out &&
	grep "dir\.d" verbose.out &&
	grep "dir\.adir" verbose.out &&
	grep "dir\.bdir" verbose.out &&
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
test_expect_success LONGTEST 'flux-fsck works (big)' '
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
test_expect_success 'make a reference invalid (dir.b)' '
	cat dirb.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > dirbbad.out &&
	flux kvs put --treeobj dir.b="$(cat dirbbad.out)" &&
	flux kvs getroot -b > bbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors (dir.b)' '
	test_must_fail flux fsck 2> fsckerrors1.out &&
	test_debug "cat fsckerrors1.out" &&
	count=$(cat fsckerrors1.out | wc -l) &&
	test $count -eq 3 &&
	grep "dir\.b" fsckerrors1.out | grep "missing blobref(s)" &&
	grep "Total errors: 1" fsckerrors1.out
'
test_expect_success 'flux-fsck --verbose outputs details (dir.b)' '
	test_must_fail flux fsck --verbose 2> fsckerrors1V.out &&
	test_debug "cat fsckerrors1V.out" &&
	grep "dir\.b" fsckerrors1V.out | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 1" fsckerrors1V.out
'
test_expect_success 'flux-fsck no output with --quiet (dir.b)' '
	test_must_fail flux fsck --quiet 2> fsckerrors2.out &&
	test_debug "cat fsckerrors2.out" &&
	count=$(cat fsckerrors2.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid (dir.c)' '
	cat dirc.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > dircbad1.out &&
	cat dircbad1.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > dircbad2.out &&
	flux kvs put --treeobj dir.c="$(cat dircbad2.out)" &&
	flux kvs getroot -b > cbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors (dir.b & c)' '
	test_must_fail flux fsck 2> fsckerrors3.out &&
	test_debug "cat fsckerrors3.out" &&
	count=$(cat fsckerrors3.out | wc -l) &&
	test $count -eq 4 &&
	grep "dir\.b" fsckerrors3.out | grep "missing blobref(s)" &&
	grep "dir\.c" fsckerrors3.out | grep "missing blobref(s)" &&
	grep "Total errors: 2" fsckerrors3.out
'
test_expect_success 'flux-fsck --verbose outputs details (dir.b & c)' '
	test_must_fail flux fsck --verbose 2> fsckerrors3V.out &&
	test_debug "cat fsckerrors3V.out" &&
	grep "dir\.b" fsckerrors3V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors3V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors3V.out | grep "missing blobref" | grep "index=2" &&
	grep "Total errors: 2" fsckerrors3V.out
'
test_expect_success 'flux-fsck no output with --quiet (dir.b & c)' '
	test_must_fail flux fsck --quiet 2> fsckerrors4.out &&
	test_debug "cat fsckerrors4.out" &&
	count=$(cat fsckerrors4.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid (dir.d)' '
	cat dird.out | jq -c .data[0]=\"sha1-1234567890123456789012345678901234567890\" > dirdbad1.out &&
	cat dirdbad1.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > dirdbad2.out &&
	flux kvs put --treeobj dir.d="$(cat dirdbad2.out)" &&
	flux kvs getroot -b > dbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors (dir.b & c & d)' '
	test_must_fail flux fsck 2> fsckerrors5.out &&
	test_debug "cat fsckerrors5.out" &&
	count=$(cat fsckerrors5.out | wc -l) &&
	test $count -eq 5 &&
	grep "dir\.b" fsckerrors5.out | grep "missing blobref(s)" &&
	grep "dir\.c" fsckerrors5.out | grep "missing blobref(s)" &&
	grep "dir\.d" fsckerrors5.out | grep "missing blobref(s)" &&
	grep "Total errors: 3" fsckerrors5.out
'
test_expect_success 'flux-fsck --verbose outputs details (dir.b & c & d)' '
	test_must_fail flux fsck --verbose 2> fsckerrors5V.out &&
	test_debug "cat fsckerrors5V.out" &&
	grep "dir\.b" fsckerrors5V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors5V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors5V.out | grep "missing blobref" | grep "index=2" &&
	grep "dir\.d" fsckerrors5V.out | grep "missing blobref" | grep "index=0" &&
	grep "dir\.d" fsckerrors5V.out | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 3" fsckerrors5V.out
'
test_expect_success 'flux-fsck no output with --quiet (dir.b & c & d)' '
	test_must_fail flux fsck --quiet 2> fsckerrors6.out &&
	test_debug "cat fsckerrors6.out" &&
	count=$(cat fsckerrors6.out | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a dirref reference invalid (dir.bdir)' '
	flux kvs get --treeobj dir > dir.out &&
	cat dir.out | jq -r .data[0] > dir.ref &&
	flux content load $(cat dir.ref) > dir.treeobj &&
	cat dir.treeobj | jq -c .data.bdir.data[0]=\"sha1-1234567890123456789012345678901234567890\" > bdirbad.out &&
	cat bdirbad.out | flux content store > bdirbad.ref &&
	cat dir.out | jq -c .data[0]=\"$(cat bdirbad.ref)\" > dirupdate.out &&
	flux kvs put --treeobj dir="$(cat dirupdate.out)" &&
	flux kvs getroot -b > bdirbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck detects errors (dir.b & c & d & bdir)' '
	test_must_fail flux fsck 2> fsckerrors7.out &&
	test_debug "cat fsckerrors7.out" &&
	count=$(cat fsckerrors7.out | wc -l) &&
	test $count -eq 6 &&
	grep "dir\.b" fsckerrors7.out | grep "missing blobref(s)" &&
	grep "dir\.c" fsckerrors7.out | grep "missing blobref(s)" &&
	grep "dir\.d" fsckerrors7.out | grep "missing blobref(s)" &&
	grep "dir\.bdir" fsckerrors7.out | grep "missing dirref blobref" &&
	grep "Total errors: 4" fsckerrors7.out
'
test_expect_success 'flux-fsck --verbose outputs details (dir.b & c & d & bdir)' '
	test_must_fail flux fsck --verbose 2> fsckerrors7V.out &&
	test_debug "cat fsckerrors7V.out" &&
	grep "dir\.b" fsckerrors7V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors7V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" fsckerrors7V.out | grep "missing blobref" | grep "index=2" &&
	grep "dir\.d" fsckerrors7V.out | grep "missing blobref" | grep "index=0" &&
	grep "dir\.d" fsckerrors7V.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.bdir" fsckerrors7V.out | grep "missing dirref blobref" &&
	grep "Total errors: 4" fsckerrors7V.out
'
test_expect_success 'flux-fsck no output with --quiet (dir.b & c & d & bdir)' '
	test_must_fail flux fsck --quiet 2> fsckerrors8.out &&
	test_debug "cat fsckerrors8.out" &&
	count=$(cat fsckerrors8.out | wc -l) &&
	test $count -eq 0
'
#
# --rootref tests
#
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck works on rootref a' '
	flux fsck --verbose --rootref=$(cat a.rootref) 2> rootref1.out &&
	test_debug "cat rootref1.out" &&
	count=$(cat rootref1.out | wc -l) &&
	test $count -eq 3 &&
	grep "dir\.a" rootref1.out &&
	grep "Total errors: 0" rootref1.out
'
test_expect_success 'flux-fsck works on rootref b' '
	flux fsck --verbose --rootref=$(cat b.rootref) 2> rootref2.out &&
	test_debug "cat rootref2.out" &&
	count=$(cat rootref2.out | wc -l) &&
	test $count -eq 4 &&
	grep "dir\.a" rootref2.out &&
	grep "dir\.b" rootref2.out &&
	grep "Total errors: 0" rootref2.out
'
test_expect_success 'flux-fsck works on rootref c' '
	flux fsck --verbose --rootref=$(cat c.rootref) 2> rootref3.out &&
	test_debug "cat rootref3.out" &&
	count=$(cat rootref3.out | wc -l) &&
	test $count -eq 5 &&
	grep "dir\.a" rootref3.out &&
	grep "dir\.b" rootref3.out &&
	grep "dir\.c" rootref3.out &&
	grep "Total errors: 0" rootref3.out
'
test_expect_success 'flux-fsck works on rootref d' '
	flux fsck --verbose --rootref=$(cat d.rootref) 2> rootref4.out &&
	test_debug "cat rootref4.out" &&
	count=$(cat rootref4.out | wc -l) &&
	test $count -eq 6 &&
	grep "dir\.a" rootref4.out &&
	grep "dir\.b" rootref4.out &&
	grep "dir\.c" rootref4.out &&
	grep "dir\.d" rootref4.out &&
	grep "Total errors: 0" rootref4.out
'
test_expect_success 'flux-fsck works on rootref w/ bad b' '
	test_must_fail flux fsck --verbose --rootref=$(cat bbad.rootref) 2> rootref5.out &&
	test_debug "cat rootref5.out" &&
	grep "dir\.b" rootref5.out | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 1" rootref5.out
'
test_expect_success 'flux-fsck works on rootref c w/ bad b and c' '
	test_must_fail flux fsck --verbose --rootref=$(cat cbad.rootref) 2> rootref6.out &&
	test_debug "cat rootref6.out" &&
	grep "dir\.b" rootref6.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref6.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref6.out | grep "missing blobref" | grep "index=2" &&
	grep "Total errors: 2" rootref6.out
'
test_expect_success 'flux-fsck works on rootref d w/ bad b and c and d' '
	test_must_fail flux fsck --verbose --rootref=$(cat dbad.rootref) 2> rootref7.out &&
	test_debug "cat rootref7.out" &&
	grep "dir\.b" rootref7.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref7.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref7.out | grep "missing blobref" | grep "index=2" &&
	grep "dir\.d" rootref7.out | grep "missing blobref" | grep "index=0" &&
	grep "dir\.d" rootref7.out | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 3" rootref7.out
'
test_expect_success 'flux-fsck works on rootref w/ bad b and c and d and bdir' '
	test_must_fail flux fsck --verbose --rootref=$(cat bdirbad.rootref) 2> rootref8.out &&
	test_debug "cat rootref8.out" &&
	grep "dir\.b" rootref8.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref8.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.c" rootref8.out | grep "missing blobref" | grep "index=2" &&
	grep "dir\.d" rootref8.out | grep "missing blobref" | grep "index=0" &&
	grep "dir\.d" rootref8.out | grep "missing blobref" | grep "index=1" &&
	grep "dir\.bdir" rootref8.out | grep "missing dirref blobref" &&
	grep "Total errors: 4" rootref8.out
'
test_expect_success 'flux-fsck --rootref fails on non-existent ref' '
	test_must_fail flux fsck --rootref=sha1-1234567890123456789012345678901234567890 2> rootref9.out &&
	grep "Total errors: 1" rootref9.out
'
test_expect_success 'flux-fsck --rootref fails on invalid ref' '
	test_must_fail flux fsck --rootref=lalalal
'
test_expect_success 'remove content & content-sqlite modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
