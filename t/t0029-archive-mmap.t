#!/bin/sh

test_description='Test flux-archive'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

LPTEST="flux lptest"

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

test_under_flux 2 kvs

# after test_under_flux is launched, cannot assume what umask is.  An
# unexpected umask could affect tests below.  Hard code to 022 for
# these tests.
umask 022

# Usage: list_mapped_files tag
list_mapped_files() {
	flux module stats content | jq -r ".mmap.tags[\"$1\"][]"
}

test_expect_success 'create copy directory' '
	mkdir -p copydir
'
test_expect_success 'create test file' '
	${LPTEST} >testfile &&
	chmod u=rwx testfile &&
	chmod go=r testfile
'
test_expect_success 'map nonexistent file fails' '
	test_must_fail flux archive create --mmap notafile
'
test_expect_success 'map fails on rank != 0' '
	test_must_fail flux exec -r 1 flux archive create --mmap testfile
'
test_expect_success 'map fails with --preserve' '
	test_must_fail flux archive create --mmap --preserve testfile
'
test_expect_success 'map fails with --no-force-primary' '
	test_must_fail flux archive create --mmap --no-force-primary testfile
'
test_expect_success 'map unreadable file fails with appropriate error' '
	touch unreadable &&
	chmod ugo-r unreadable &&
	test_must_fail flux archive create --mmap unreadable 2>unreadable.err &&
	grep "Permission denied" unreadable.err
'
test_expect_success 'map test file' '
	flux archive create --mmap ./testfile &&
	flux kvs get --raw archive.main | jq
'
test_expect_success 'content stats show mapped file' '
	list_mapped_files main >stats.out &&
	realpath ./testfile >stats.exp &&
	test_cmp stats.exp stats.out
'
test_expect_success 'file can be listed' '
	flux archive list
'
test_expect_success 'file can be listed in long form' '
	flux archive list --long
'
test_expect_success 'test file can be read through content cache on rank 0' '
	flux archive extract -C copydir
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
	flux exec -r 1 flux archive extract -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'content stats no longer show mapped file' '
	test_must_fail list_mapped_files archive.main
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'test file is not extracted' '
	rm -f copydir/testfile &&
	test_must_fail flux archive extract
'
test_expect_success 'map test file with small chunksize' '
	flux archive create --mmap --chunksize=10 ./testfile
'
test_expect_success 'test file can be read through content cache on rank 0' '
	rm -f copydir/testfile &&
	flux archive extract -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'test file can be read through content cache on rank 1' '
	rm -f copydir/testfile &&
	flux exec -r 1 flux archive extract -C copydir &&
	test_cmp testfile copydir/testfile
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'create test file' '
	echo abcdefghijklmnopqrstuvwxyz >testfile2
'
test_expect_success 'map test file with small small-file-threshold' '
	flux archive create --mmap --small-file-threshold=10 ./testfile2
'
test_expect_success 'extract blobref from mapped file' "
	flux kvs get --raw archive.main >testfile2.json &&
	jq -r '.[0].data[0][2]' <testfile2.json >testfile2.blobref
"
test_expect_success 'store the duplicate blob to the mapped one on rank 1' '
	flux content load <testfile2.blobref >testfile2.data &&
	flux exec -r 1 flux content store <testfile2.data \
	    >testfile2.blobref2  &&
	test_cmp testfile2.blobref testfile2.blobref2
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'drop the cache' '
	flux exec -r 1 flux content dropcache &&
	flux content dropcache
'
test_expect_success 'blob can still be read through content cache on rank 1' '
	flux exec -r 1 flux content load <testfile2.blobref2 \
		>testfile2.data2 &&
	test_cmp testfile2.data testfile2.data2
'
test_expect_success HAVE_SPARSE 'create sparse test file' '
	truncate --size=8192 testfile3
'
test_expect_success HAVE_SPARSE 'map test file' '
	flux archive create --mmap ./testfile3
'
test_expect_success HAVE_SPARSE 'test file can be read through content cache' '
	flux archive extract -C copydir &&
	test_cmp testfile3 copydir/testfile3
'
test_expect_success HAVE_SPARSE 'holes were preserved' '
	stat --format="%b" testfile3 >blocks.exp &&
	stat --format="%b" copydir/testfile3 >blocks.out &&
	test_cmp blocks.exp blocks.out
'
test_expect_success HAVE_SPARSE 'unmap test file' '
	flux archive remove
'
test_expect_success HAVE_SPARSE 'create sparse test file with data' '
	truncate --size=8192 testfile3b &&
	echo more-data >>testfile3b
'
test_expect_success HAVE_SPARSE 'map test file' '
	flux archive create --mmap ./testfile3b
'
test_expect_success HAVE_SPARSE 'test file can be read through content cache' '
	flux archive extract -C copydir &&
	test_cmp testfile3b copydir/testfile3b
'
test_expect_success HAVE_SPARSE 'holes were preserved' '
	stat --format="%b" testfile3b >blocks3b.exp &&
	stat --format="%b" copydir/testfile3b >blocks3b.out &&
	test_cmp blocks3b.exp blocks3b.out
'
test_expect_success HAVE_SPARSE 'unmap test file' '
	flux archive remove
'
test_expect_success 'create test symlink' '
	ln -s /a/b/c/d testfile4
'
test_expect_success 'map test file' '
	flux archive create --mmap ./testfile4
'
test_expect_success 'show raw object' '
	flux kvs get --raw archive.main | jq .
'
test_expect_success 'test file can be extracted' '
	flux archive extract -v -C copydir
'
test_expect_success 'copy is a symlink with expected target' '
	readlink testfile4 >link.exp &&
	readlink copydir/testfile4 >link.out &&
	test_cmp link.exp link.out
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'map file by absolute path' '
	flux archive create --mmap /etc/group
'
test_expect_success 'test file lists with relative path' '
	cat >list.exp <<-EOT &&
	etc/group
	EOT
	flux archive list >list.out &&
	test_cmp list.exp list.out
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'create test directory' '
	mkdir testfile5
'
test_expect_success 'map test file' '
	flux archive create --mmap ./testfile5
'
test_expect_success 'test file can be read through content cache' '
	flux archive extract -v -C copydir
'
test_expect_success 'copy is a directory' '
	test -d copydir/testfile5
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'map small test file with reduced small file threshold' '
	flux archive create --mmap --small-file-threshold=0 ./testfile2
'
test_expect_success 'test file used blobvec encoding' '
	flux kvs get --raw archive.main | jq -e ".[0].encoding == \"blobvec\""
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 'map test file' '
	rm -f copydir/testfile &&
	flux archive create --mmap ./testfile
'
test_expect_success 'modify mapped test file without reducing its size' '
	dd if=/dev/zero of=testfile bs=4096 count=1 conv=notrunc
'
test_expect_success 'content change should cause an error' '
	rm -f copydir/testfile &&
	test_must_fail flux archive extract -C copydir 2>changed.err &&
	grep changed changed.err
'
test_expect_success 'drop cache and unmap test file' '
	flux content dropcache &&
	flux archive remove
'
test_expect_success 'map test file' '
	rm -f copydir/testfile &&
	flux archive create --mmap ./testfile
'
test_expect_success 'truncate mapped test file' '
	cp /dev/null testfile
'
test_expect_success 'size reduction should cause an error' '
	rm -f copydir/testfile &&
	test_must_fail flux archive extract -C copydir 2>reduced.err &&
	grep changed reduced.err
'
test_expect_success 'unmap test file' '
	flux archive remove
'
test_expect_success 're-create and map test file' '
	${LPTEST} >testfile &&
	flux archive create --mmap ./testfile
'
test_expect_success 'extract copydir/testfile' '
	rm -f copydir/testfile &&
	flux archive extract -C copydir
'
test_expect_success 'extract --overwrite works' '
	flux archive extract --overwrite -C copydir
'
test_expect_success 'extract refuses to overwrite without explicit option' '
	test_must_fail flux archive extract -C copydir
'
test_expect_success 'unmap test file' '
	flux archive remove
'

test_done
