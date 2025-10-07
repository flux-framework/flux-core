#!/bin/sh

test_description='Test flux fsck command'

. `dirname $0`/content/content-helper.sh

. $(dirname $0)/sharness.sh

test_under_flux 1 minimal

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer"

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
	flux kvs put testdir.a=test &&
	flux kvs getroot -b > a.rootref &&
	flux kvs put testdir.b=test1 &&
	flux kvs put --append testdir.b=test2 &&
	flux kvs put --append testdir.b=test3 &&
	flux kvs put --append testdir.b=test4 &&
	flux kvs getroot -b > b.rootref &&
	flux kvs put testdir.c=testA &&
	flux kvs put --append testdir.c=testB &&
	flux kvs put --append testdir.c=testC &&
	flux kvs put --append testdir.c=testD &&
	flux kvs getroot -b > c.rootref &&
	flux kvs put testdir.d=testE &&
	flux kvs put --append testdir.d=testF &&
	flux kvs getroot -b > d.rootref &&
	flux kvs mkdir testdir.adir &&
	flux kvs mkdir testdir.bdir &&
	flux kvs link testdir alink &&
	flux kvs namespace create testns &&
	flux kvs put --namespace=testns testdir.a=testns
'
# N.B. startlog commands in rc scripts normally ensures a checkpoint
# exists but we do this just to be extra sure
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'save some treeobjs for later' '
	flux kvs get --treeobj testdir.b > testdirb.out &&
	flux kvs get --treeobj testdir.c > testdirc.out &&
	flux kvs get --treeobj testdir.d > testdird.out
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck works (simple)' '
	flux fsck > simple.out 2> simple.err &&
	grep "Checking integrity" simple.out &&
	grep "Total errors: 0" simple.err
'
test_expect_success 'flux-fsck verbose works (simple)' '
	flux fsck --verbose > verbose.out 2> verbose.err &&
	grep "testdir$" verbose.err &&
	grep "testdir\.a" verbose.err &&
	grep "testdir\.b" verbose.err &&
	grep "testdir\.c" verbose.err &&
	grep "testdir\.d" verbose.err &&
	grep "testdir\.adir" verbose.err &&
	grep "testdir\.bdir" verbose.err &&
	grep "alink" verbose.err
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
	flux fsck --verbose > bigval.out 2> bigval.err &&
	grep "Checking integrity" bigval.err &&
	grep "bigval" bigval.err &&
	grep "Total errors: 0" bigval.err
'
test_expect_success 'load kvs' '
	flux module load kvs
'
# unfortunately we don't have a `flux content remove` command, so we'll corrupt
# a treeobj of type "valref" by overwriting one of the references within it with
# a bad reference
test_expect_success 'make a reference invalid (testdir.b)' '
	cat testdirb.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > testdirbbad.out &&
	flux kvs put --treeobj testdir.b="$(cat testdirbbad.out)" &&
	flux kvs getroot -b > bbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck detects errors (testdir.b)' '
	test_must_fail flux fsck > fsckerrors1.out 2> fsckerrors1.err &&
	test_debug "cat fsckerrors1.err" &&
	count=$(cat fsckerrors1.err | wc -l) &&
	test $count -eq 2 &&
	grep "testdir\.b" fsckerrors1.err | grep "missing blobref(s)" &&
	grep "Total errors: 1" fsckerrors1.err
'
test_expect_success 'flux-fsck --verbose outputs details (testdir.b)' '
	test_must_fail flux fsck --verbose > fsckerrors1V.out 2> fsckerrors1V.err &&
	test_debug "cat fsckerrors1V.err" &&
	grep "testdir\.b" fsckerrors1V.err | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 1" fsckerrors1V.err
'
test_expect_success 'flux-fsck does not prefix error messages on non-tty runs' '
	grep "flux-fsck" fsckerrors1.err
'
test_expect_success 'flux-fsck prefixes error messages on tty runs' '
	test_must_fail $runpty flux fsck > fsckerrors1PTY.out &&
	test_must_fail grep "flux-fsck" fsckerrors1PTY.err
'
test_expect_success 'flux-fsck no output with --quiet (testdir.b)' '
	test_must_fail flux fsck --quiet > fsckerrors2.out 2> fsckerrors2.err &&
	test_debug "cat fsckerrors2.err" &&
	count=$(cat fsckerrors2.err | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid (testdir.c)' '
	cat testdirc.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > testdircbad1.out &&
	cat testdircbad1.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > testdircbad2.out &&
	flux kvs put --treeobj testdir.c="$(cat testdircbad2.out)" &&
	flux kvs getroot -b > cbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck detects errors (testdir.b & c)' '
	test_must_fail flux fsck > fsckerrors3.out 2> fsckerrors3.err &&
	test_debug "cat fsckerrors3.err" &&
	count=$(cat fsckerrors3.err | wc -l) &&
	test $count -eq 3 &&
	grep "testdir\.b" fsckerrors3.err | grep "missing blobref(s)" &&
	grep "testdir\.c" fsckerrors3.err | grep "missing blobref(s)" &&
	grep "Total errors: 2" fsckerrors3.err
'
test_expect_success 'flux-fsck --verbose outputs details (testdir.b & c)' '
	test_must_fail flux fsck --verbose > fsckerrors3V.out 2> fsckerrors3V.err &&
	test_debug "cat fsckerrors3V.err" &&
	grep "testdir\.b" fsckerrors3V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors3V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors3V.err | grep "missing blobref" | grep "index=2" &&
	grep "Total errors: 2" fsckerrors3V.err
'
test_expect_success 'flux-fsck no output with --quiet (testdir.b & c)' '
	test_must_fail flux fsck --quiet > fsckerrors4.out 2> fsckerrors4.err &&
	test_debug "cat fsckerrors4.err" &&
	count=$(cat fsckerrors4.err | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'make a reference invalid (testdir.d)' '
	cat testdird.out | jq -c .data[0]=\"sha1-1234567890123456789012345678901234567890\" > testdirdbad1.out &&
	cat testdirdbad1.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > testdirdbad2.out &&
	flux kvs put --treeobj testdir.d="$(cat testdirdbad2.out)" &&
	flux kvs getroot -b > dbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck detects errors (testdir.b & c & d)' '
	test_must_fail flux fsck > fsckerrors5.out 2> fsckerrors5.err &&
	test_debug "cat fsckerrors5.err" &&
	count=$(cat fsckerrors5.err | wc -l) &&
	test $count -eq 4 &&
	grep "testdir\.b" fsckerrors5.err | grep "missing blobref(s)" &&
	grep "testdir\.c" fsckerrors5.err | grep "missing blobref(s)" &&
	grep "testdir\.d" fsckerrors5.err | grep "missing blobref(s)" &&
	grep "Total errors: 3" fsckerrors5.err
'
test_expect_success 'flux-fsck --verbose outputs details (testdir.b & c & d)' '
	test_must_fail flux fsck --verbose >fsckerrors5V.out 2> fsckerrors5V.err &&
	test_debug "cat fsckerrors5V.err" &&
	grep "testdir\.b" fsckerrors5V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors5V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors5V.err | grep "missing blobref" | grep "index=2" &&
	grep "testdir\.d" fsckerrors5V.err | grep "missing blobref" | grep "index=0" &&
	grep "testdir\.d" fsckerrors5V.err | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 3" fsckerrors5V.err
'
test_expect_success 'flux-fsck no output with --quiet (testdir.b & c & d)' '
	test_must_fail flux fsck --quiet > fsckerrors6.out 2> fsckerrors6.err &&
	test_debug "cat fsckerrors6.err" &&
	count=$(cat fsckerrors6.err | wc -l) &&
	test $count -eq 0
'
test_expect_success 'load kvs' '
	flux module load kvs
'
# N.B. this can be a bit confusing how to corrupt a dirref
# 1) get treeobj of `testdir`, this treeobj should be of type "dirref"
# 2) get the dirref reference from the `testdir` treeobj.
# 3) get the content of this reference from content store, it should be a treeobj of type "dir"
# 4) within this treeobj is the key "bdir", it is a treeobj of type "dirref"
# 5) corrupt the "dirref" for "bdir"
# 6) write this corrupted treeobj back to the content store, saving this new reference
# 7) update the original treeobj of `testdir` (it's a "dirref") to point to the bad "dir" treeobj
test_expect_success 'make a dirref reference invalid (testdir.bdir)' '
	flux kvs get --treeobj testdir > testdir.out &&
	cat testdir.out | jq -r .data[0] > testdir.ref &&
	flux content load $(cat testdir.ref) > testdir.treeobj &&
	cat testdir.treeobj | jq -c .data.bdir.data[0]=\"sha1-1234567890123456789012345678901234567890\" > bdirbad.out &&
	cat bdirbad.out | flux content store > bdirbad.ref &&
	cat testdir.out | jq -c .data[0]=\"$(cat bdirbad.ref)\" > testdirupdate.out &&
	flux kvs put --treeobj testdir="$(cat testdirupdate.out)" &&
	flux kvs getroot -b > bdirbad.rootref
'
test_expect_success 'call sync to ensure we have checkpointed' '
	flux kvs sync
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck detects errors (testdir.b & c & d & bdir)' '
	test_must_fail flux fsck > fsckerrors7.out 2> fsckerrors7.err &&
	test_debug "cat fsckerrors7.err" &&
	count=$(cat fsckerrors7.err | wc -l) &&
	test $count -eq 5 &&
	grep "testdir\.b" fsckerrors7.err | grep "missing blobref(s)" &&
	grep "testdir\.c" fsckerrors7.err | grep "missing blobref(s)" &&
	grep "testdir\.d" fsckerrors7.err | grep "missing blobref(s)" &&
	grep "testdir\.bdir" fsckerrors7.err | grep "missing dirref blobref" &&
	grep "Total errors: 4" fsckerrors7.err
'
test_expect_success 'flux-fsck --verbose outputs details (testdir.b & c & d & bdir)' '
	test_must_fail flux fsck --verbose > fsckerrors7V.out 2> fsckerrors7V.err &&
	test_debug "cat fsckerrors7V.err" &&
	grep "testdir\.b" fsckerrors7V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors7V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" fsckerrors7V.err | grep "missing blobref" | grep "index=2" &&
	grep "testdir\.d" fsckerrors7V.err | grep "missing blobref" | grep "index=0" &&
	grep "testdir\.d" fsckerrors7V.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.bdir" fsckerrors7V.err | grep "missing dirref blobref" &&
	grep "Total errors: 4" fsckerrors7V.err
'
test_expect_success 'flux-fsck no output with --quiet (testdir.b & c & d & bdir)' '
	test_must_fail flux fsck --quiet > fsckerrors8.out 2> fsckerrors8.err &&
	test_debug "cat fsckerrors8.err" &&
	count=$(cat fsckerrors8.err | wc -l) &&
	test $count -eq 0
'
#
# --rootref tests
#
# line count includes extra diagnostic messages
test_expect_success 'flux-fsck works on rootref a' '
	flux fsck --verbose --rootref=$(cat a.rootref) > rootref1.out 2> rootref1.err &&
	test_debug "cat rootref1.err" &&
	count=$(cat rootref1.err | wc -l) &&
	test $count -eq 3 &&
	grep "testdir\.a" rootref1.err &&
	grep "Total errors: 0" rootref1.err
'
test_expect_success 'flux-fsck works on rootref b' '
	flux fsck --verbose --rootref=$(cat b.rootref) > rootref2.out 2> rootref2.err &&
	test_debug "cat rootref2.err" &&
	count=$(cat rootref2.err | wc -l) &&
	test $count -eq 4 &&
	grep "testdir\.a" rootref2.err &&
	grep "testdir\.b" rootref2.err &&
	grep "Total errors: 0" rootref2.err
'
test_expect_success 'flux-fsck works on rootref c' '
	flux fsck --verbose --rootref=$(cat c.rootref) > rootref3.out 2> rootref3.err &&
	test_debug "cat rootref3.err" &&
	count=$(cat rootref3.err | wc -l) &&
	test $count -eq 5 &&
	grep "testdir\.a" rootref3.err &&
	grep "testdir\.b" rootref3.err &&
	grep "testdir\.c" rootref3.err &&
	grep "Total errors: 0" rootref3.err
'
test_expect_success 'flux-fsck works on rootref d' '
	flux fsck --verbose --rootref=$(cat d.rootref) > rootref4.out 2> rootref4.err &&
	test_debug "cat rootref4.err" &&
	count=$(cat rootref4.err | wc -l) &&
	test $count -eq 6 &&
	grep "testdir\.a" rootref4.err &&
	grep "testdir\.b" rootref4.err &&
	grep "testdir\.c" rootref4.err &&
	grep "testdir\.d" rootref4.err &&
	grep "Total errors: 0" rootref4.err
'
test_expect_success 'flux-fsck works on rootref w/ bad b' '
	test_must_fail flux fsck --verbose --rootref=$(cat bbad.rootref) \
		       > rootref5.out 2> rootref5.err &&
	test_debug "cat rootref5.err" &&
	grep "testdir\.b" rootref5.err | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 1" rootref5.err
'
test_expect_success 'flux-fsck works on rootref c w/ bad b and c' '
	test_must_fail flux fsck --verbose --rootref=$(cat cbad.rootref) \
		       > rootref6.out 2> rootref6.err &&
	test_debug "cat rootref6.err" &&
	grep "testdir\.b" rootref6.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref6.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref6.err | grep "missing blobref" | grep "index=2" &&
	grep "Total errors: 2" rootref6.err
'
test_expect_success 'flux-fsck works on rootref d w/ bad b and c and d' '
	test_must_fail flux fsck --verbose --rootref=$(cat dbad.rootref) \
		       > rootref7.out 2> rootref7.err &&
	test_debug "cat rootref7.err" &&
	grep "testdir\.b" rootref7.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref7.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref7.err | grep "missing blobref" | grep "index=2" &&
	grep "testdir\.d" rootref7.err | grep "missing blobref" | grep "index=0" &&
	grep "testdir\.d" rootref7.err | grep "missing blobref" | grep "index=1" &&
	grep "Total errors: 3" rootref7.err
'
test_expect_success 'flux-fsck works on rootref w/ bad b and c and d and bdir' '
	test_must_fail flux fsck --verbose --rootref=$(cat bdirbad.rootref) \
		       > rootref8.out 2> rootref8.err &&
	test_debug "cat rootref8.err" &&
	grep "testdir\.b" rootref8.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref8.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.c" rootref8.err | grep "missing blobref" | grep "index=2" &&
	grep "testdir\.d" rootref8.err | grep "missing blobref" | grep "index=0" &&
	grep "testdir\.d" rootref8.err | grep "missing blobref" | grep "index=1" &&
	grep "testdir\.bdir" rootref8.err | grep "missing dirref blobref" &&
	grep "Total errors: 4" rootref8.err
'
test_expect_success 'flux-fsck --rootref fails on non-existent ref' '
	test_must_fail flux fsck --rootref=sha1-1234567890123456789012345678901234567890 \
		       > rootref9.out 2> rootref9.err &&
	grep "Total errors: 1" rootref9.err
'
test_expect_success 'flux-fsck --rootref fails on invalid ref' '
	test_must_fail flux fsck --rootref=lalalal
'
#
# --repair
#
test_expect_success 'checkpoint-get returned final expected rootref' '
	checkpoint_get | jq -r .value[0].rootref >checkpointbad.out &&
	test_cmp checkpointbad.out bdirbad.rootref
'
test_expect_success 'flux-fsck --repair works (1)' '
	test_must_fail flux fsck --repair > repair1.out 2> repair1.err &&
	grep "testdir\.b" repair1.err | grep "repaired" &&
	grep "testdir\.c" repair1.err | grep "repaired" &&
	grep "testdir\.d" repair1.err | grep "repaired" &&
	grep "testdir\.bdir" repair1.err | grep "unlinked" &&
	grep "Total errors: 4" repair1.err
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'flux-fsck --repair recovers data to lost+found' '
	flux kvs get lost+found.testdir.b > losttestdirb1.out &&
	echo "test1test3test4" > losttestdirb1.exp &&
	test_cmp losttestdirb1.exp losttestdirb1.out &&
	flux kvs get lost+found.testdir.c > losttestdirc1.out &&
	echo "testAtestD" > losttestdirc1.exp &&
	test_cmp losttestdirc1.exp losttestdirc1.out &&
	flux kvs get lost+found.testdir.d > losttestdird1.out &&
	! test -s losttestdird1.out
'
test_expect_success 'flux-fsck --repair unlinks bad entries' '
	test_must_fail flux kvs get testdir.b &&
	test_must_fail flux kvs get testdir.c &&
	test_must_fail flux kvs get testdir.d &&
	test_must_fail flux kvs ls testdir.bdir
'
test_expect_success 'flux-fsck --repair leaves good entries' '
	flux kvs get testdir.a &&
	flux kvs ls testdir.adir &&
	flux kvs get --treeobj alink
'
test_expect_success 'flux-fsck --repair converted testdir.d to a val treeobj' '
	flux kvs get --treeobj lost+found.testdir.d | jq -e ".type == \"val\""
'
test_expect_success 'checkpoint-get returns expected updated rootref' '
	flux kvs getroot -b > afterrepair.rootref &&
	checkpoint_get | jq -r .value[0].rootref > afterrepair.out &&
	test_cmp afterrepair.out afterrepair.rootref
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck passes now' '
	flux fsck > postrepair1.out 2> postrepair1.err &&
	grep "Total errors: 0" postrepair1.err
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'fix testdir.c' '
	flux kvs copy lost+found.testdir.c testdir.c
'
test_expect_success 'overwrite testdir.d' '
	flux kvs put testdir.d.e=test1 &&
	flux kvs put --append testdir.d.e=test2 &&
	flux kvs put --append testdir.d.e=test3 &&
	flux kvs put --append testdir.d.e=test4 &&
	flux kvs get --treeobj testdir.d.e > testdire.out &&
	flux kvs getroot -b > e.rootref
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck --repair does nothing now (2)' '
	flux fsck --repair > repair2.out 2> repair2.err &&
	grep "Total errors: 0" repair2.err
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'flux-fsck --repair leaves prior recoveries' '
	flux kvs get lost+found.testdir.c > losttestdirc2.out &&
	echo "testAtestD" > losttestdirc2.exp &&
	test_cmp losttestdirc2.exp losttestdirc2.out &&
	flux kvs get lost+found.testdir.d > losttestdird2.out &&
	! test -s losttestdird2.out
'
test_expect_success 'make a reference invalid (testdir.d.e)' '
	cat testdire.out | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > testdirebad1.out &&
	cat testdirebad1.out | jq -c .data[2]=\"sha1-1234567890123456789012345678901234567890\" > testdirebad2.out &&
	flux kvs put --treeobj testdir.d.e="$(cat testdirebad2.out)" &&
	flux kvs getroot -b > ebad.rootref
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'flux-fsck --repair works (3)' '
	test_must_fail flux fsck --repair > repair3.out 2> repair3.err &&
	grep "testdir\.d\.e" repair3.err | grep "repaired" &&
	grep "Total errors: 1" repair3.err
'
test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'flux-fsck --repair overwrites older recoveries' '
	flux kvs get lost+found.testdir.d.e > losttestdire3.out &&
	echo "test1test4" > losttestdire3.exp &&
	test_cmp losttestdire3.exp losttestdire3.out
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'remove content & content-sqlite modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
