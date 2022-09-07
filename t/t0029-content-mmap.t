#!/bin/sh

test_description='Test content-mapped files'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

LPTEST=${FLUX_BUILD_DIR}/t/shell/lptest

test_under_flux 2 minimal

test_expect_success 'flux content load handles blobrefs on stdin' '
        echo foo >foo &&
	flux content store < foo | flux content load >bar &&
	test_cmp foo bar
'
test_expect_success 'create test file' '
	${LPTEST} >testfile
'
test_expect_success 'mmap nonexistent file fails' '
	test_must_fail flux content mmap notafile
'
test_expect_success 'mmap test file' '
	flux content mmap ./testfile >testfile.blobrefs
'
test_expect_success 'mmap test file again fails' '
	test_must_fail flux content mmap ./testfile
'
test_expect_success 'test file can be read through content cache on rank 0' '
	flux content load <testfile.blobrefs >testfile.copy0 &&
	test_cmp testfile testfile.copy0
'
test_expect_success 'test file can be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile.blobrefs >testfile.copy1 &&
	test_cmp testfile testfile.copy1
'
test_expect_success 'mumnap test file' '
	flux content munmap ./testfile
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'test file cannot be read through content cache' '
	test_must_fail flux content load <testfile.blobrefs
'
test_expect_success 'mmap test file with small blobsize' '
	flux content mmap ./testfile 10 >testfile.blobrefs2
'
test_expect_success 'test file can be read through content cache on rank 0' '
	flux content load <testfile.blobrefs2 >testfile.copy0a &&
	test_cmp testfile testfile.copy0a
'
test_expect_success 'test file can be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile.blobrefs2 >testfile.copy1a &&
	test_cmp testfile testfile.copy1a
'
test_expect_success 'mumnap test file' '
	flux content munmap ./testfile
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'create test file' '
	echo abcdefghijklmnopqrstuvwxyz >testfile2
'
test_expect_success 'mmap test file' '
	flux content mmap ./testfile2 >testfile2.blobrefs
'
test_expect_success 'test file can be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile2.blobrefs >testfile2.copy &&
	test_cmp testfile2 testfile2.copy
'
test_expect_success 'store the same blob on rank 1' '
	flux exec -r 1 flux content store <testfile2 >testfile2.blobrefs2 &&
	test_cmp testfile2.blobrefs testfile2.blobrefs2
'
test_expect_success 'mumnap test file' '
	flux content munmap ./testfile2
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'test file can be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile2.blobrefs >testfile2.copy2 &&
	test_cmp testfile2 testfile2.copy2
'

test_done
