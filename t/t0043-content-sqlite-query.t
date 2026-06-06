#!/bin/sh

test_description='Test flux sqlite query and backup commands'

. `dirname $0`/sharness.sh

test_under_flux 1 minimal

command -v sqlite3 >/dev/null 2>&1 && test_set_prereq SQLITE3

test_expect_success 'load content and content-sqlite modules' '
	flux module load content &&
	flux module load content-sqlite
'

test_expect_success 'store some test data' '
	echo "test data 1" | flux content store --bypass-cache >hash1.out &&
	echo "test data 2" | flux content store --bypass-cache >hash2.out &&
	echo "test data 3" | flux content store --bypass-cache >hash3.out
'

test_expect_success 'flux sqlite query SELECT works' '
	flux sqlite query "SELECT COUNT(*) FROM objects" >count.out &&
	grep -q "3" count.out
'

test_expect_success 'flux sqlite query with --header flag works' '
	flux sqlite query -H "SELECT COUNT(*) FROM objects" >header.out &&
	grep -q "COUNT" header.out
'

test_expect_success 'flux sqlite query with --column flag works' '
	flux sqlite query -c "SELECT COUNT(*) FROM objects" >column.out &&
	test -s column.out
'

test_expect_success 'flux sqlite query PRAGMA works' '
	flux sqlite query "PRAGMA journal_mode" >pragma.out &&
	test -s pragma.out
'

test_expect_success 'flux sqlite query multi-line query with leading whitespace works' '
	flux sqlite query "
		SELECT COUNT(*)
		FROM objects
	" >multiline.out &&
	grep -q "3" multiline.out
'

test_expect_success 'flux sqlite query DELETE without --force fails' '
	test_must_fail flux sqlite query "DELETE FROM objects WHERE rowid = 1" 2>delete-noforce.err &&
	grep -q "force" delete-noforce.err
'

test_expect_success 'flux sqlite query DELETE with --force works' '
	flux sqlite query --force "DELETE FROM objects WHERE rowid = 1" &&
	flux sqlite query "SELECT COUNT(*) FROM objects" >count3.out &&
	grep -q "2" count3.out
'

test_expect_success 'flux sqlite query VACUUM without --force fails' '
	test_must_fail flux sqlite query "VACUUM" 2>vacuum-noforce.err &&
	grep -q "force" vacuum-noforce.err
'

test_expect_success 'flux sqlite query VACUUM with --force works' '
	flux sqlite query --force "VACUUM"
'

test_expect_success 'flux sqlite query rejects INSERT' '
	test_must_fail flux sqlite query "INSERT INTO objects VALUES (1,2,3,4,5)" 2>insert.err &&
	grep -q "only SELECT, PRAGMA, DELETE, and VACUUM" insert.err
'

test_expect_success 'flux sqlite query rejects UPDATE' '
	test_must_fail flux sqlite query "UPDATE objects SET hash = NULL" 2>update.err &&
	grep -q "only SELECT, PRAGMA, DELETE, and VACUUM" update.err
'

test_expect_success 'flux sqlite query rejects DROP' '
	test_must_fail flux sqlite query "DROP TABLE objects" 2>drop.err &&
	grep -q "only SELECT, PRAGMA, DELETE, and VACUUM" drop.err
'

test_expect_success 'flux sqlite backup creates backup file' '
	BACKUP=$(pwd)/backup.db &&
	flux sqlite backup $BACKUP &&
	test -f $BACKUP
'

test_expect_success SQLITE3 'backup file contains data' '
	BACKUP=$(pwd)/backup.db &&
	test -s $BACKUP &&
	sqlite3 $BACKUP "SELECT COUNT(*) FROM objects" >backup-count.out &&
	grep -q "2" backup-count.out
'

test_expect_success 'flux sqlite backup rejects relative path' '
	test_must_fail flux sqlite backup backup-relative.db \
		2>backup-relative.err &&
	grep -q "absolute" backup-relative.err
'

test_expect_success 'flux sqlite backup rejects same path as source' '
	DBPATH=$(flux getattr statedir)/content.sqlite &&
	test_must_fail flux sqlite backup $DBPATH 2>backup-same.err &&
	grep -q "same as source" backup-same.err
'

test_expect_success 'flux sqlite query fails as guest' '
	test_must_fail env FLUX_HANDLE_ROLEMASK=0x2 \
		flux sqlite query "SELECT COUNT(*) FROM objects" 2>guest.err &&
	grep -q "owner credentials" guest.err
'

test_expect_success 'flux sqlite backup fails as guest' '
	BACKUP=$(pwd)/backup-guest.db &&
	test_must_fail env FLUX_HANDLE_ROLEMASK=0x2 \
		flux sqlite backup $BACKUP 2>backup-guest.err &&
	grep -q "owner credentials" backup-guest.err
'

test_expect_success 'flux sqlite query handles FLOAT type with expression' '
	flux sqlite query "SELECT 3.14 as float_col" >float.out &&
	grep -q "3.14" float.out
'

test_expect_success 'flux sqlite query handles NULL type with expression' '
	flux sqlite query "SELECT NULL as null_col" >null.out &&
	grep -q "None" null.out
'

test_expect_success 'flux sqlite query handles BLOB type from objects table' '
	flux sqlite query "SELECT object FROM objects LIMIT 1" >blob.out &&
	test -s blob.out
'

test_expect_success 'flux sqlite query handles mixed types in one query' '
	flux sqlite query "SELECT 42 as int_val, 3.14 as float_val, \"text\" as text_val, NULL as null_val" >mixed.out &&
	grep -q "42" mixed.out &&
	grep -q "3.14" mixed.out &&
	grep -q "text" mixed.out &&
	grep -q "None" mixed.out
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
'

test_expect_success 'remove content module' '
	flux module remove content
'

test_done
