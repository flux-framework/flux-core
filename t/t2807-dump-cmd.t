#!/bin/sh

test_description='Test flux dump/restore'

. $(dirname $0)/sharness.sh

test_under_flux 1 minimal -o,-Sstatedir=$(pwd)

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"

countblobs() {
	$QUERYCMD -t 100 content.sqlite \
		"select count(*) from objects;" | sed -e 's/.*= //'
}

test_expect_success 'flux-dump with no args prints Usage message' '
	test_must_fail flux dump 2>dump-noargs.out &&
	grep "Usage" dump-noargs.out
'
test_expect_success 'flux-restore with no args prints Usage message' '
	test_must_fail flux restore 2>restore-noargs.out &&
	grep "Usage" restore-noargs.out
'
test_expect_success 'flux-dump with no backing store fails' '
	test_must_fail flux dump --checkpoint foo.tar 2>nostore.err &&
	grep "Function not implemented" nostore.err
'
test_expect_success 'flux-dump with bad archive file fails' '
	test_must_fail flux dump /badfile.tar 2>badfile.err &&
	grep badfile.tar badfile.err
'
test_expect_success 'load content-sqlite' '
	flux module load content-sqlite
'
test_expect_success 'flux-dump --checkpoint with missing checkpoint fails' '
	test_must_fail flux dump --checkpoint foo.tar 2>nocheck.err &&
	grep "error fetching checkpoint" nocheck.err
'

test_expect_success 'load kvs and create some kvs content' '
	printf "%-.*d" 100 0 >x.val &&
	flux module load kvs &&
	flux kvs put --no-merge a.b.c=testkey &&
	flux kvs link linkedthing y &&
	flux kvs put --no-merge x=$(cat x.val) &&
	flux kvs link --target-namespace=smurf otherthing z &&
	flux kvs put --no-merge w= &&
	flux kvs put --no-merge --append w=foo
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'dump default=kvs-primary checkpoint' '
	flux dump --checkpoint foo.tar
'
test_expect_success 'repeat dump with -q' '
	flux dump -q --checkpoint foo.tar
'
test_expect_success 'repeat dump with -v' '
	flux dump -v --checkpoint foo.tar
'
test_expect_success 'repeat dump with --no-cache' '
	flux dump --no-cache --checkpoint foo.tar
'
test_expect_success 'unload content-sqlite' '
	flux content flush &&
	flux content dropcache &&
	flux module remove content-sqlite
'

# Small values and symlnks are contained in dirent rather than separate
# objects, so after the above kvs commands, expect a blobcount of:
# (1) rootdir 1st version
# (3) rootdir 2nd version + 'a' directory + 'b' directory (c is contained in b)
# (1) rootdir 3rd version (y is contained in root)
# (2) rootdir 4th version + 'x'
# (1) rootdir 5th version (z is contained in root)
# (1) rootdir 6th version (w probably contained in root)
# (3) rootdir 7th version ('w' empty blobref + append to 'w')
# Total: 12 blobs
#
test_expect_success 'count blobs representing those five keys' '
	echo 12 >blobcount.exp &&
	countblobs >blobcount.out &&
	test_cmp blobcount.exp blobcount.out
'
test_expect_success 'remove backing file and load content-sqlite' '
	rm -f content.sqlite &&
	flux module load content-sqlite
'
test_expect_success 'restore content' '
	flux restore --checkpoint foo.tar
'
test_expect_success 'repeat restore with -v' '
	flux restore -v --checkpoint foo.tar
'
test_expect_success 'repeat restore with -q' '
	flux restore -q --checkpoint foo.tar
'
test_expect_success 'repeat restore with --no-cache' '
	flux restore --no-cache --checkpoint foo.tar
'
test_expect_success 'unload content-sqlite' '
	flux content flush &&
	flux content dropcache &&
	flux module remove content-sqlite
'

# Intermediate rootdir versions are not preserved across dump/restore.
# Expect a blobcount of 4: rootdir 5th version + 'a' + 'b' + 'x',
#
test_expect_success 'count blobs after restore'\'s' implicit garbage collection' '
	echo 4 >blobcount2.exp &&
	countblobs >blobcount2.out &&
	test_cmp blobcount2.exp blobcount2.out
'

test_expect_success 'load content-sqlite + kvs and list content' '
	flux module load content-sqlite &&
	flux module load kvs &&
	flux kvs dir -R
'
test_expect_success 'verify that exact KVS content was restored' '
	test $(flux kvs get a.b.c) = "testkey" &&
	test $(flux kvs get x) = $(cat x.val) &&
	test $(flux kvs readlink y) = "linkedthing" &&
	test $(flux kvs readlink z) = "smurf::otherthing" &&
	test $(flux kvs get w) = "foo"
'
test_expect_success 'now restore to key and verify content' '
	flux restore -v --key zz foo.tar &&
	test $(flux kvs get zz.a.b.c) = "testkey" &&
	test $(flux kvs get zz.x) = $(cat x.val)
'
test_expect_success 'try dump - | restore - to key and verify content' '
	flux dump - | flux restore --key yy - &&
	test $(flux kvs get a.b.c) = "testkey" &&
	test $(flux kvs get zz.a.b.c) = "testkey" &&
	test $(flux kvs get yy.zz.a.b.c) = "testkey" &&
	test $(flux kvs get x) = $(cat x.val) &&
	test $(flux kvs get zz.x) = $(cat x.val) &&
	test $(flux kvs get yy.zz.x) = $(cat x.val) &&
	test $(flux kvs readlink y) = "linkedthing" &&
	test $(flux kvs readlink zz.y) = "linkedthing" &&
	test $(flux kvs readlink yy.zz.y) = "linkedthing" &&
	test $(flux kvs readlink z) = "smurf::otherthing" &&
	test $(flux kvs readlink zz.z) = "smurf::otherthing" &&
	test $(flux kvs readlink yy.zz.z) = "smurf::otherthing"
'
test_expect_success 'dump ignores empty kvs directories' '
	flux kvs mkdir emtpy &&
	flux dump -v foo3.tar &&
	tar tvf foo3.tar >toc &&
	test_must_fail grep empty toc
'
test_expect_success 'restore ignores directories added by tar of filesystem' '
	mkdir tmp &&
	(cd tmp &&
	tar xvf ../foo3.tar &&
	tar cf - . | flux restore --key test -)
'
test_expect_success 'restore without required argument fails' '
	test_must_fail flux restore foo.tar 2>restore-argmissing.err &&
	grep "Please specify a restore target" restore-argmissing.err
'
test_expect_success 'restore --checkpoint fails with kvs loaded' '
	test_must_fail flux restore --checkpoint foo.tar 2>restore-cpkvs.err &&
	grep "please unload kvs" restore-cpkvs.err
'
test_expect_success 'unload kvs' '
	flux module remove kvs
'
test_expect_success 'restore to key fails when kvs is not loaded' '
	test_must_fail flux restore --key foo foo.tar 2>restore-nokvs.err &&
	grep "error updating" restore-nokvs.err
'
test_expect_success 'unload content-sqlite' '
	flux module remove content-sqlite
'
test_expect_success 'restore --checkpoint with no backing store fails' '
	test_must_fail flux restore --checkpoint foo.tar 2>noback.err &&
	grep "error updating checkpoint" noback.err
'
test_expect_success 'dump --no-cache with no backing store fails' '
	test_must_fail flux dump --no-cache --checkpoint x.tar
'
test_expect_success 'restore --no-cache with no backing store fails' '
	test_must_fail flux restore --no-cache --checkpoint foo.tar
'
test_expect_success 'run a flux instance, preserving content.sqlite' '
	mkdir test &&
	flux start -o,-Sstatedir=$(pwd)/test /bin/true
'

reader() {
	local dbdir=$1
        flux start -o,-Sbroker.rc1_path= \
                -o,-Sbroker.rc3_path=\
                -o,-Sstatedir=$dbdir\
                bash -c "\
                        flux module load content-sqlite && \
                        flux dump --no-cache -q --checkpoint - &&\
                        flux module remove content-sqlite
                "
}

writer() {
	local dbdir=$1
        flux start -o,-Sbroker.rc1_path= \
                -o,-Sbroker.rc3_path= \
                -o,-Sstatedir=$dbdir \
                bash -c "\
                        flux module load content-sqlite && \
                        flux restore --checkpoint - && \
                        flux module remove content-sqlite
                "
}

test_expect_success 'perform offline garbage collection with dump/restore' '
	mkdir test_bak &&
	mv test/content.sqlite test_bak/ &&
	reader test_bak | writer test
'

test_expect_success 'restart flux instance and try to run a job' '
	flux start -o,-Sstatedir=test \
		flux mini run /bin/true
'

test_done
