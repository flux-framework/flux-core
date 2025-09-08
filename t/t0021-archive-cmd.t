#!/bin/sh

test_description='Test flux-archive'

. `dirname $0`/sharness.sh

test_under_flux 2

# after test_under_flux is launched, cannot assume what umask is.  An
# unexpected umask could affect tests below.  Hard code to 022 for
# these tests.
umask 022

# Usage: randbytes bytes
randbytes() {
	dd if=/dev/urandom bs=$1 count=1
}

test_expect_success 'flux archive create --badopt prints unrecognized option' '
	test_must_fail flux archive create --badopt 2>badopt.err &&
	grep "unrecognized option" badopt.err
'
test_expect_success 'flux archive create with no PATHs fails' '
	test_must_fail flux archive create
'
test_expect_success 'flux archive create with bad FLUX_URI fails' '
	(FLUX_URI=local:///noexist test_must_fail flux archive create .)
'
test_expect_success 'flux archive create fails with --overwrite --append' '
	test_must_fail flux archive create --overwrite --append .
'
test_expect_success 'flux archive create fails with -C baddir bad ' '
	test_must_fail flux archive create -C baddir .
'
test_expect_success 'flux archive create fails with FIFO in input' '
	mkfifo imafifo &&
	test_must_fail flux archive create imafifo
'
test_expect_success 'flux archive remove fails if archive doesnt exist' '
	test_must_fail flux archive remove
'
test_expect_success 'but it works with -f' '
	flux archive remove -f
'
# Set the small file threshold to 1K in these tests.
# It's currently the default but just in case that changes.
test_expect_success 'flux archive create works (small file)' '
	randbytes 128 >testfile &&
	flux archive create --small-file-threshold=1K -v testfile &&
	flux kvs get archive.main >archive.out
'
test_expect_success 'archive.main contains a base64-encoded file' '
	jq -e -r <archive.out ".[0].encoding == \"base64\""
'
test_expect_success 'flux archive create fails if archive exists' '
	test_must_fail flux archive create testfile
'
test_expect_success 'flux archive create --overwrite works (large file)' '
	randbytes 2048 >testfile2 &&
	flux archive create --overwrite --preserve \
		--small-file-threshold=1K -v testfile2 &&
	flux kvs get archive.main >archive2.out
'
test_expect_success 'and archive.main contains a blobvec-encoded file' '
	jq -e -r <archive2.out ".[0].encoding == \"blobvec\""
'
test_expect_success 'and archive.main_blobs exists due to --preserve' '
	flux kvs ls archive.main_blobs
'
test_expect_success 'flux archive create --append works (directory)' '
	mkdir -p testdir &&
	flux archive create --append testdir &&
	flux kvs get archive.main >archive3.out
'
test_expect_success 'and archive.main added an entry' '
	jq -e -r <archive3.out ".[1].path == \"testdir\""
'
test_expect_success 'flux archive list works' '
	flux archive list >list.out &&
	cat >list.exp <<-EOT &&
	testfile2
	testdir
	EOT
	test_cmp list.exp list.out
'
test_expect_success 'flux archive list -l works' '
	flux archive list -l >list_l.out &&
	cat >list_l.exp <<-EOT &&
	f 0644     2048 testfile2
	d 0755        0 testdir
	EOT
	test_cmp list_l.exp list_l.out
'
test_expect_success 'flux archive list with matching pattern works' '
	flux archive list testdir
'
test_expect_success 'flux archive list with non-matching pattern works' '
	flux archive list notinthere
'
test_expect_success 'flux archive extract -C baddir fails' '
	test_must_fail flux archive extract -C baddir
'
test_expect_success 'flux archive extract -n main -C gooddir works' '
	mkdir -p gooddir &&
	flux archive extract -n main -v -C gooddir
'
test_expect_success 'goodir/testfile2 was extracted faithfully' '
	test_cmp testfile2 gooddir/testfile2
'
test_expect_success 'goodir/testdir was extracted as a directory' '
	test -d gooddir/testdir
'
test_expect_success 'flux archive extract works with a non-matching pattern' '
	flux archive extract nomatch
'
test_expect_success 'flux archive extract works with a matching pattern' '
	mkdir -p gooddir2 &&
	flux archive extract -C gooddir2 testfile2 &&
	test_cmp testfile2 gooddir2/testfile2
'
test_expect_success 'flux archive remove works' '
	flux archive remove
'
test_expect_success 'and KVS keys are gone' '
	test_must_fail flux kvs ls archive.main_blobs &&
	test_must_fail flux kvs ls archive.main
'
test_expect_success 'flux archive list fails on nonexistent archive' '
	test_must_fail flux archive list &&
	test_must_fail flux archive list -n noexist
'
test_expect_success 'flux archive extract fails on nonexistent archive' '
	test_must_fail flux archive extract &&
	test_must_fail flux archive extract -n noexist
'
# This works because we use the primary namespace by default
test_expect_success 'create an archive, then access it from a job' '
	flux archive create testfile &&
	flux run --label-io -N2 flux archive list
'
# but --no-force-primary allows FLUX_KVS_NAMESPACE to be used
test_expect_success 'archive cannot be accessed from job with --no-force-primary' '
	test_must_fail flux run flux archive list --no-force-primary
'
# both producer and consumer use --no-force-primary within a job
test_expect_success 'copy private archive from rank 0 to 1 of a job' '
	cat >job.sh <<-EOT &&
	#!/bin/sh
	opts="--no-force-primary -n mystuff"
	if test \$FLUX_TASK_RANK -eq 0; then
	    flux archive create \$opts testfile2
	else
	    mkdir jobdir
	    flux archive extract -v --waitcreate \$opts -C jobdir
	fi
	EOT
	chmod +x job.sh &&
	flux run --label-io -N2 ./job.sh 2>job.err
'
# Note: the current primary ns archive contains testfile not testfile2
test_expect_success 'output references the private archive not primary ns' '
	grep "1: testfile2" job.err
'
test_expect_success 'jobdir/testfile2 was extracted faithfully' '
	test_cmp testfile2 jobdir/testfile2
'
test_expect_success 'archive exists in job KVS guest directory' '
	jobdir=$(flux job last | flux job id --to=kvs) &&
	flux kvs get $jobdir.guest.archive.mystuff >/dev/null
'
test_expect_success 'remove main archive' '
	flux archive remove
'
test_expect_success 'Create files for example 1 of flux-archive(1)' '
	mkdir -p project/dataset1 &&
	echo foo >project/dataset1/testfile &&
	echo bar >project/dataset1/testfile2
'
test_expect_success 'Ensure example 1 of flux-archive(1) works' '
	flux archive create -C project dataset1 &&
	flux exec -r 1 mkdir -p tmp.project &&
	flux exec -r 1 flux archive extract --waitcreate -C tmp.project &&
	flux exec -r 1 rm -r tmp.project &&
	flux archive remove
'

test_done
