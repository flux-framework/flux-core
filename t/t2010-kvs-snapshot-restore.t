#!/bin/sh
#

test_description='Test KVS snapshot/restore'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_under_flux 1

CHANGECHECKPOINT=${FLUX_SOURCE_DIR}/t/kvs/change-checkpoint.py

test_expect_success 'run instance with content.backing-path set' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux kvs put testkey=42
'

test_expect_success 'content.sqlite file exists after instance exited' '
	test -f content.sqlite &&
	echo Size in bytes: $(stat --format "%s" content.sqlite)
'

test_expect_success 're-run instance with content.backing-path set' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux kvs get testkey >get.out
'

test_expect_success 'content from previous instance survived' '
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

test_expect_success 're-run instance, verify checkpoint date saved' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux dmesg >dmesg1.out
'

# just check for todays date, not time for obvious reasons
test_expect_success 'verify date in flux logs' '
	today=`date --iso-8601` &&
	grep checkpoint dmesg1.out | grep ${today}
'

test_expect_success 're-run instance, get rootref' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux kvs getroot -b > getroot.out
'

test_expect_success 'write rootref to checkpoint path, emulating original checkpoint' '
        rootref=$(cat getroot.out) &&
        ${CHANGECHECKPOINT} $(pwd)/content.sqlite "kvs-primary" ${rootref}
'

test_expect_success 're-run instance, verify checkpoint correctly loaded' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux dmesg >dmesg2.out
'

test_expect_success 'verify checkpoint loaded with no date' '
	grep checkpoint dmesg2.out | grep "N\/A"
'

test_done
