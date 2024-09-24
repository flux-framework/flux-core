#!/bin/sh

test_description='Test flux-filemap'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

LPTEST="flux lptest"

test_under_flux 2 kvs

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
test_expect_success 'flux filemap map works' '
	flux filemap map ./testfile &&
	flux kvs get --raw archive.main | jq
'
test_expect_success 'content stats show mapped file' '
	list_mapped_files main >stats.out &&
	realpath ./testfile >stats.exp &&
	test_cmp stats.exp stats.out
'
test_expect_success 'flux filemap list works' '
	flux filemap list
'
test_expect_success 'flux filemap list --long works' '
	flux filemap list --long
'
test_expect_success 'flux filemap get works' '
	flux filemap get -C copydir
'
test_expect_success 'file content is correct' '
	test_cmp testfile copydir/testfile
'
test_expect_success 'flux filemap umap works' '
	flux filemap unmap
'
test_expect_success 'content stats no longer show mapped file' '
	test_must_fail list_mapped_files archive.main
'
test_expect_success 'flux filemap map --disable-mmap -Too works' '
	flux filemap map --disable-mmap -Tfoo ./testfile &&
	flux kvs get --raw archive.foo | jq
'
test_expect_success 'flux filemap list -Tfoo works' '
	flux filemap list -Tfoo
'
test_expect_success 'flux filemap get -Tfoo works' '
	rm -f copydir/* &&
	flux filemap get -C copydir -Tfoo 2>warn.err
'
test_expect_success 'with warning printed on stderr' '
	grep deprecated warn.err
'
test_expect_success 'file content is correct' '
	test_cmp testfile copydir/testfile
'
test_expect_success 'flux filemap unmap -Tfoo works' '
	flux filemap unmap -Tfoo
'
test_expect_success 'flux filemap list no longer works' '
	test_must_fail flux filemap list -Tfoo
'
test_expect_success 'flux filemap create does not accept -T' '
	test_must_fail flux archive create -T bar ./testfile
'
test_expect_success 'flux filemap create does not accept --disable-mmap' '
	test_must_fail flux archive create --disable-mmap ./testfile
'
test_expect_success 'flux archive create --name=bar works' '
	flux archive create --name=bar ./testfile
'
test_expect_success 'flux filemap extract does not accept -T' '
	test_must_fail flux archive extract -T bar
'
test_expect_success 'flux filemap remove does not accept -T' '
	test_must_fail flux archive remove -T bar
'
test_done
