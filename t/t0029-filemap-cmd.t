#!/bin/sh

test_description='Test flux-filemap'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

LPTEST=${FLUX_BUILD_DIR}/t/shell/lptest

# SEEK_DATA support was added to the linux NFS client in kernel 3.18.
# In el7 based distros, it is defined but doesn't work on NFS.  So
# ensure SEEK_DATA returns ENXIO on file that is 100% empty.
havesparse() {
	cat >lseek.py <<-EOT &&
	#!/usr/bin/env python3
	import sys, os, errno
	fd = os.open("$1", os.O_RDONLY)
	try:
	    os.lseek(fd, 0, os.SEEK_DATA)
	except OSError as e:
	    if e.errno == errno.ENXIO:
	        sys.exit(0)
	sys.exit(1)
	EOT
	chmod +x lseek.py &&
	truncate --size 8192 $1 &&
	test $(stat --format "%b" $1) -eq 0 &&
	./lseek.py
}

if havesparse testholes; then
	test_set_prereq HAVE_SPARSE
fi

test_under_flux 2 minimal

# after test_under_flux is launched, cannot assume what umask is.  An
# unexpected umask could affect tests below.  Hard code to 022 for
# these tests.
umask 022

test_expect_success 'load content module' '
	flux exec flux module load content
'

test_expect_success 'create copy directory' '
	mkdir -p copydir
'
test_expect_success 'create test file' '
	${LPTEST} >testfile &&
	chmod u=rwx testfile &&
	chmod go=r testfile
'
test_expect_success 'map nonexistent file fails' '
	test_must_fail flux filemap map notafile
'
test_expect_success 'map test file' '
	flux filemap map ./testfile
'
test_expect_success 'file can be listed' '
	flux filemap list
'
test_expect_success 'file can be listed in long form' '
	flux filemap list --long
'
test_expect_success 'test file can be read through content cache on rank 0' '
	flux filemap get -C copydir
'
test_expect_success 'file content is correct' '
	test_cmp testfile copydir/testfile
'
test_expect_success 'file permissions are correct' '
	stat --format="%a" testfile >access.exp &&
	stat --format="%a" copydir/testfile >access.out &&
	test_cmp access.exp access.out
'
test_expect_success 'test file can be read through content cache on rank 1' '
	rm -f copydir/testfile &&
	flux exec -r 1 flux filemap get -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'test file is not extracted' '
	rm -f copydir/testfile &&
	flux filemap get -C copydir &&
	test_must_fail test -f copydir/testfile
'
test_expect_success 'map test file with small blobsize' '
	flux filemap map --chunksize=10 ./testfile
'
test_expect_success 'test file can be read through content cache on rank 0' '
	rm -f copydir/testfile &&
	flux filemap get -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'test file can be read through content cache on rank 1' '
	rm -f copydir/testfile &&
	flux exec -r 1 flux filemap get -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'create test file' '
	echo abcdefghijklmnopqrstuvwxyz >testfile2
'
test_expect_success 'map test file and get its blobref' '
	flux filemap map ./testfile2 &&
	flux filemap list --blobref >testfile2.blobref
'
test_expect_success 'show raw object' '
	flux filemap list --raw | jq .
'
test_expect_success 'test file can be read through content cache on rank 1' '
	flux exec -r 1 flux filemap get -C copydir &&
	test_cmp testfile2 copydir/testfile2
'
test_expect_success 'store the duplicate blob on rank 1' '
	flux content load <testfile2.blobref >testfile2.fileref &&
	flux exec -r 1 flux content store <testfile2.fileref \
	    >testfile2.blobref2  &&
	test_cmp testfile2.blobref testfile2.blobref2
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'blob can still be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile2.blobref2 \
		>testfile2.fileref2 &&
	test_cmp testfile2.fileref testfile2.fileref2
'
test_expect_success HAVE_SPARSE 'create sparse test file' '
	truncate --size=8192 testfile3
'
test_expect_success HAVE_SPARSE 'map test file' '
	flux filemap map ./testfile3
'
test_expect_success HAVE_SPARSE 'test file can be read through content cache' '
	flux filemap get -C copydir &&
	test_cmp testfile3 copydir/testfile3
'
test_expect_success HAVE_SPARSE 'holes were preserved' '
	stat --format="%b" testfile3 >blocks.exp &&
	stat --format="%b" copydir/testfile3 >blocks.out &&
	test_cmp blocks.exp blocks.out
'
test_expect_success HAVE_SPARSE 'unmap test file' '
	flux filemap unmap
'
test_expect_success HAVE_SPARSE 'create sparse test file with data' '
	truncate --size=8192 testfile3b &&
	echo more-data >>testfile3b
'
test_expect_success HAVE_SPARSE 'map test file' '
	flux filemap map ./testfile3b
'
test_expect_success HAVE_SPARSE 'test file can be read through content cache' '
	flux filemap get -C copydir &&
	test_cmp testfile3b copydir/testfile3b
'
test_expect_success HAVE_SPARSE 'holes were preserved' '
	stat --format="%b" testfile3b >blocks3b.exp &&
	stat --format="%b" copydir/testfile3b >blocks3b.out &&
	test_cmp blocks3b.exp blocks3b.out
'
test_expect_success HAVE_SPARSE 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'create test symlink' '
	ln -s /a/b/c/d testfile4
'
test_expect_success 'map test file' '
	flux filemap map ./testfile4
'
test_expect_success 'show raw object' '
	flux filemap list --raw | jq .
'
test_expect_success 'test file can be read through content cache' '
	flux filemap get -vv -C copydir
'
test_expect_success 'copy is a symlink with expected target' '
	readlink testfile4 >link.exp &&
	readlink copydir/testfile4 >link.out &&
	test_cmp link.exp link.out
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'map file by absolute path' '
	flux filemap map /etc/group
'
test_expect_success 'test file lists with relative path' '
	cat >list.exp <<-EOT &&
	etc/group
	EOT
	flux filemap list >list.out &&
	test_cmp list.exp list.out
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'create test directory' '
	mkdir testfile5
'
test_expect_success 'map test file' '
	flux filemap map ./testfile5
'
test_expect_success 'test file can be read through content cache' '
	flux filemap get -vv -C copydir
'
test_expect_success 'copy is a directory' '
	test -d copydir/testfile5
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'map testfile with tags=red,blue' '
	flux filemap map --tags=red,blue ./testfile
'
test_expect_success 'map testfile with tags=green' '
	flux filemap map --tags=green ./testfile
'
test_expect_success 'map testfile with tags=green (again)' '
	flux filemap map --tags=green ./testfile
'
test_expect_success 'map testfile2 with tags=green' '
	flux filemap map --tags=green ./testfile2
'
test_expect_success 'list tags=red,blue,green reports two files' '
	test $(flux filemap list --tags=red,blue,green | wc -l) -eq 2
'
test_expect_success 'unmap red tag' '
	flux filemap unmap --tags=red
'
test_expect_success 'list tags=red,blue,green reports two files' '
	test $(flux filemap list --tags=red,blue,green | wc -l) -eq 2
'
test_expect_success 'unmap tags=blue,green' '
	flux filemap unmap --tags=blue,green
'
test_expect_success 'flux filemap list reports no files' '
	test $(flux filemap list --tags red,blue,green | wc -l) -eq 0
'
test_expect_success 'map test file without mmap' '
	rm -f copydir/testfile &&
	flux filemap map --disable-mmap ./testfile
'
test_expect_success 'test file did not use blobvec encoding' '
	flux filemap list --raw | jq -e ".encoding != \"blobvec\""
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'
test_expect_success 'map small test file with reduced small file threshold' '
	flux filemap map --small-file-threshold=0 ./testfile2
'
test_expect_success 'test file used blobvec encoding' '
	flux filemap list --raw | jq -e ".encoding = \"blobvec\""
'
test_expect_success 'unmap test file' '
	flux filemap unmap
'

test_expect_success 'map test file' '
	rm -f copydir/testfile &&
	flux filemap map ./testfile
'
test_expect_success 'modify mapped test file without reducing its size' '
	dd if=/dev/zero of=testfile bs=4096 count=1 conv=notrunc
'
test_expect_success 'content change should cause an error' '
	rm -f copydir/testfile &&
	test_must_fail flux filemap get -C copydir 2>changed.err &&
	grep changed changed.err
'
test_expect_success 'drop cache and unmap test file' '
	flux content dropcache &&
	flux filemap unmap
'
test_expect_success 'map test file' '
	rm -f copydir/testfile &&
	flux filemap map ./testfile
'
test_expect_success 'truncate mapped test file' '
	cp /dev/null testfile
'
test_expect_success 'size reduction should cause an error' '
	rm -f copydir/testfile &&
	test_must_fail flux filemap get -C copydir 2>reduced.err &&
	grep changed reduced.err
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
