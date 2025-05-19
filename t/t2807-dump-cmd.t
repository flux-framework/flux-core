#!/bin/sh

test_description='Test flux dump/restore'

. $(dirname $0)/sharness.sh

test_under_flux 1 minimal -Sstatedir=$(pwd)

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"

countblobs() {
	$QUERYCMD -t 100 content.sqlite \
		"select count(*) from objects;" | sed -e 's/.*= //'
}

test_expect_success 'load content module' '
	flux module load content
'

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
	grep "checkpoint get unavailable, no backing store" nostore.err
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
test_expect_success 'load content-sqlite and truncate old file to start fresh' '
	flux module load content-sqlite truncate
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
# Expect a blobcount of 4: rootdir 7th version + 'a' + 'b' + 'x',
# N.B. 'w' is now contained in the root
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
	flux kvs mkdir empty &&
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
test_expect_success 'restore --checkpoint with no backing store cant checkpoint' '
	test_must_fail flux restore --checkpoint foo.tar 2>noback.err &&
	grep "checkpoint put unavailable, no backing store" noback.err
'
test_expect_success 'dump --no-cache with no backing store fails' '
	test_must_fail flux dump --no-cache --checkpoint x.tar
'
test_expect_success 'restore --no-cache with no backing store fails' '
	test_must_fail flux restore --no-cache --checkpoint foo.tar
'
test_expect_success 'run a flux instance, preserving content.sqlite' '
	mkdir test &&
	flux start -Sstatedir=$(pwd)/test true
'

reader() {
	local dbdir=$1
        flux start -Sbroker.rc1_path= \
                -Sbroker.rc3_path=\
                -Sstatedir=$dbdir\
                bash -c "\
                        flux module load content && \
                        flux module load content-sqlite && \
                        flux dump --no-cache -q --checkpoint - &&\
                        flux module remove content-sqlite && \
                        flux module remove content
                "
}

writer() {
	local dbdir=$1
        flux start -Sbroker.rc1_path= \
                -Sbroker.rc3_path= \
                -Sstatedir=$dbdir \
                bash -c "\
                        flux module load content && \
                        flux module load content-sqlite && \
                        flux restore --checkpoint - && \
                        flux module remove content-sqlite && \
                        flux module remove content
                "
}

test_expect_success 'perform offline garbage collection with dump/restore' '
	mkdir test_bak &&
	mv test/content.sqlite test_bak/ &&
	reader test_bak | writer test
'

test_expect_success 'restart flux instance and try to run a job' '
	flux start -Sstatedir=test \
		flux run true
'

# Cover UTF-8 keys

UTF8_LOCALE=$(locale -a | grep 'UTF-8\|utf8' | head -n1)
if flux version | grep +ascii-only; then
        UTF8_LOCALE=""
fi
test -n "$UTF8_LOCALE" && test_set_prereq UTF8_LOCALE

test_expect_success UTF8_LOCALE 'create a dump with a UTF-8 key in it' '
	(LC_ALL=${UTF8_LOCALE} flux start -Scontent.dump=widedump.tar \
	    flux kvs put ƒuzzybunny=42)
'
test_expect_success UTF8_LOCALE 'list UTF-8 dump' '
	(LC_ALL=${UTF8_LOCALE} tar tvf widedump.tar)
'
test_expect_success UTF8_LOCALE 'restore UTF-8 dump' '
	(LC_ALL=${UTF8_LOCALE} flux start -Scontent.restore=widedump.tar \
	    flux kvs get ƒuzzybunny)
'

# Cover value with a very large number of appends

test_expect_success LONGTEST 'load content-sqlite and truncate old file to start fresh' '
	flux module load content-sqlite truncate
'
# N.B. from 1000 to 3000 instead of 0 to 2000, easier to debug errors
# using fold(1) (i.e. all numbers same width)
test_expect_success LONGTEST 'load kvs and create some kvs content' '
	flux module load kvs &&
	for i in `seq 1000 3000`; do
	    flux kvs put --append bigval=${i}
	done &&
	flux kvs get bigval > bigval.exp
'
test_expect_success LONGTEST 'unload kvs' '
	flux module remove kvs
'
test_expect_success LONGTEST 'dump default=kvs-primary checkpoint' '
	flux dump --checkpoint bigval.tar
'
test_expect_success LONGTEST 'reload content-sqlite, truncate old file to start fresh' '
	flux module reload content-sqlite truncate
'
test_expect_success LONGTEST 'restore content' '
	flux restore --checkpoint bigval.tar
'
test_expect_success LONGTEST 'load kvs and check bigval value' '
	flux module load kvs &&
	flux kvs get bigval > bigval.out &&
	test_cmp bigval.out bigval.exp
'
test_expect_success LONGTEST 'unload kvs' '
	flux module remove kvs
'
test_expect_success LONGTEST 'unload content-sqlite' '
	flux module remove content-sqlite
'

# Cover --size-limit

test_expect_success 'create bigdump.tar with a 12M blob in it' '
	mkdir -p big &&
	dd if=/dev/zero of=big/tinyblob bs=1048576 count=1 &&
	dd if=/dev/zero of=big/bigblob bs=1048576 count=12 &&
	dd if=/dev/zero of=big/smallblob bs=1048576 count=3 &&
	dd if=/dev/zero of=big/medblob bs=1048576 count=6 &&
	dd if=/dev/zero of=big/med2blob bs=1048576 count=6 &&
	tar cvf bigdump.tar big
'
test_expect_success 'restore bigdump.tar and verify blob count' '
	flux start flux restore \
		--key=foo bigdump.tar 2>bigdump.err &&
	grep "restored 5 keys (7 blobs)" bigdump.err
'
test_expect_success 'restore bigdump.tar with size limit' '
	flux start flux restore --size-limit=10485760 \
		--key=foo bigdump.tar 2>bigdump2.err &&
	grep "exceeds" bigdump2.err &&
	grep "restored 4 keys (6 blobs)" bigdump2.err
'
test_expect_success 'rc1 skips blob that exceeds 100M limit' '
	dd if=/dev/zero of=big/hugeblob bs=1048576 count=120 &&
	tar cvf bigdump2.tar big &&
	flux start -Scontent.restore=bigdump2.tar \
		true 2>bigdump3.err &&
	grep "exceeds" bigdump3.err
'

# Cover dump/restore KVS key pointing to nonexistent blobref

test_expect_success 'load kvs' '
	flux module load kvs
'
test_expect_success 'take a dump and count the keys' '
	flux dump -v origdump.tar &&
	tar tvf origdump.tar | wc -l >origdump.count
'

# Usage: mkbad dirref|valref
blobref="sha1-996324fa537cd312e564be576bafc21a3f63910d"
mkbad() {
	echo '{"data":["'$blobref'"],"type":"'$1'","ver":1}'
}

test_expect_success 'create a KVS valref with a dangling blobref' '
	mkbad valref | flux kvs put --treeobj bad.a=- &&
	test_must_fail flux kvs get bad.a
'
test_expect_success 'flux dump fails on bad key' '
	test_must_fail flux dump baddump.tar
'
test_expect_success 'flux dump works with --ignore-failed-read' '
	flux dump -q --ignore-failed-read baddump.tar
'
test_expect_success 'archive contains expected number of keys' '
	tar tvf baddump.tar | wc -l >baddump.count &&
	test_cmp origdump.count baddump.count
'
test_expect_success 'create a KVS dirref with a dangling blobref' '
	mkbad dirref | flux kvs put --treeobj bad.b=- &&
	test_must_fail flux kvs get bad.b
'
test_expect_success 'flux dump works with --ignore-failed-read' '
	flux dump -q --ignore-failed-read baddump2.tar
'
test_expect_success 'archive contains expected number of keys' '
	tar tvf baddump2.tar | wc -l >baddump2.count &&
	test_cmp origdump.count baddump2.count
'
test_expect_success 'remove kvs module' '
	flux module remove kvs
'
test_expect_success 'remove content module' '
	flux module remove content
'

test_done
