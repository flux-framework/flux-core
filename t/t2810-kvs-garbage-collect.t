#!/bin/sh

test_description='Test offline KVS garbage collection'

. $(dirname $0)/sharness.sh

test_expect_success 'create test script' '
	cat >runjobs.sh <<-EOT &&
	#!/bin/bash -e
	trap "" SIGHUP
	flux submit --cc=1-10 true >/dev/null
	flux queue drain
	backingmod=\$(flux getattr content.backing-module)
	flux module stats --type int --parse object_count \$backingmod
	EOT
	chmod +x runjobs.sh
'
test_expect_success 'run instance that leaves an auto dump' '
	mkdir -p state &&
	flux start -Sstatedir=state \
	    -Scontent.dump=auto \
	    -Slog-filename=dmesg.log \
	    ./runjobs.sh >object_count
'
test_expect_success 'broker logs report dump activity' '
	grep "dumping content to" dmesg.log
'
test_expect_success 'dump exists and RESTORE symlink is valid' '
	test -h state/dump/RESTORE &&
	readlink -f state/dump/RESTORE >archive &&
	test -f $(cat archive)
'
test_expect_success 'restart instance with auto restore' '
	flux start -Sstatedir=state \
	    -Scontent.restore=auto \
	    -Slog-filename=dmesg2.log \
	    flux module stats \
	        --type int --parse object_count content-sqlite >object_count2
'
test_expect_success 'broker logs report restore activity' '
	grep "restoring content from" dmesg2.log
'
test_expect_success 'number of stored objects was reduced by GC' '
	before=$(cat object_count) &&
	after=$(cat object_count2) &&
	test $before -gt $after
'
test_expect_success 'RESTORE symlink is gone' '
	test_must_fail test -h state/dump/RESTORE
'
test_expect_success 'archive file remains' '
	test -f $(cat archive)
'

#
# Now repeat the above test with
# - content-files backend
# - no statedir
# - explicitly named dump file (not auto)
#
test_expect_success 'run instance that leaves a named dump' '
	flux start -Slog-filename=dmesg3.log \
	    -Scontent.dump=foo.tgz \
	    -Scontent.backing-module=content-files \
	    ./runjobs.sh >object_count3
'
test_expect_success 'broker logs report dump activity' '
	grep "dumping content to" dmesg3.log
'
test_expect_success 'dump exists in current directory' '
	test -f foo.tgz
'
test_expect_success 'no RESTORE link was created because path is explicit' '
	test_must_fail test -h dump/RESTORE
'
test_expect_success 'restart instance and restore' '
	flux start -Slog-filename=dmesg4.log \
	    -Scontent.restore=foo.tgz \
	    -Scontent.backing-module=content-files \
	    flux module stats \
	        --type int --parse object_count content-files >object_count4
'
test_expect_success 'broker logs report restore activity' '
	grep "restoring content from" dmesg4.log
'
test_expect_success 'number of stored objects was reduced by GC' '
	before=$(cat object_count3) &&
	after=$(cat object_count4) &&
	test $before -gt $after
'

test_done
