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

# Parameterized query tests
test_expect_success 'store test data for parameter tests' '
	echo "param test 1" | flux content store --bypass-cache >param1.out &&
	echo "param test 2" | flux content store --bypass-cache >param2.out &&
	echo "param test 3" | flux content store --bypass-cache >param3.out
'

test_expect_success 'flux sqlite query with INTEGER parameter works' '
	flux sqlite query -p 1 "SELECT COUNT(*) FROM objects WHERE size = ?" >param-int.out &&
	test -s param-int.out
'

test_expect_success 'flux sqlite query with TEXT parameter works' '
	flux sqlite query -p "objects" \
		"SELECT COUNT(*) FROM sqlite_master WHERE type = '\''table'\'' AND tbl_name = ?" >param-text.out &&
	grep -q "1" param-text.out
'

test_expect_success 'flux sqlite query with NULL parameter works' '
	flux sqlite query -p null \
		"SELECT COUNT(*) FROM objects WHERE size = ?" >param-null.out &&
	grep -q "0" param-null.out
'

test_expect_success 'flux sqlite query with REAL parameter works' '
	flux sqlite query -p 3.14 \
		"SELECT typeof(?)" >param-real.out &&
	grep -q "real" param-real.out
'

test_expect_success 'flux sqlite query with BLOB parameter works' '
	HASH=$(cat param1.out) &&
	HEXHASH=$(echo $HASH | sed "s/sha1-//") &&
	flux sqlite query -p "blob:$HEXHASH" \
		"SELECT typeof(?)" >param-blob.out &&
	grep -q "blob" param-blob.out
'

test_expect_success 'flux sqlite query with multiple parameters works' '
	flux sqlite query -p 5 -p 20 \
		"SELECT COUNT(*) FROM objects WHERE size > ? AND size < ?" >param-multi.out &&
	test -s param-multi.out
'

test_expect_success 'flux sqlite query parameter count mismatch (too few params) fails' '
	test_must_fail flux sqlite query -p 1 \
		"SELECT COUNT(*) FROM objects WHERE size = ? OR size = ?" 2>param-mismatch.err &&
	grep -q "parameter count mismatch" param-mismatch.err
'

test_expect_success 'flux sqlite query parameter count mismatch (too many params) fails' '
	test_must_fail flux sqlite query -p 1 -p 2 -p 3 \
		"SELECT COUNT(*) FROM objects WHERE size = ?" 2>param-mismatch2.err &&
	grep -q "parameter count mismatch" param-mismatch2.err
'

test_expect_success 'flux sqlite query with placeholder but no params fails' '
	test_must_fail flux sqlite query \
		"SELECT COUNT(*) FROM objects WHERE size = ?" 2>no-params.err &&
	grep -q "query has parameters but none provided" no-params.err
'

test_expect_success 'flux sqlite query with parameters to invalid column fails gracefully' '
	test_must_fail flux sqlite query -p 1 \
		"SELECT COUNT(*) FROM objects WHERE nonexistent_column = ?" 2>invalid-col.err
'

test_expect_success 'flux sqlite query DELETE with BLOB parameters works' '
	HASH=$(cat param1.out) &&
	HEXHASH=$(echo $HASH | sed "s/sha1-//") &&
	COUNT_BEFORE=$(flux sqlite query "SELECT COUNT(*) FROM objects") &&
	flux sqlite query --force -p "blob:$HEXHASH" \
		"DELETE FROM objects WHERE hash = ?" &&
	COUNT_AFTER=$(flux sqlite query "SELECT COUNT(*) FROM objects") &&
	test "$COUNT_AFTER" -le "$COUNT_BEFORE"
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
'

test_expect_success 'remove content module' '
	flux module remove content
'

test_done
