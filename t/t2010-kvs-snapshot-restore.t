#!/bin/sh
#

test_description='Test loading KVS snapshot from earlier instance

Test recovery of KVS snapshot from persistdir.'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh


test_expect_success 'created persist-directory' '
	PERSISTDIR=$(mktemp -d --tmpdir=$(pwd))
'

test_expect_success 'run instance with persist-directory set' '
	rm -f $PERSISTDIR/kvsroot.final &&
	flux start -o,--setattr=persist-directory=$PERSISTDIR \
	    flux kvs put testkey=42
'

test_expect_success 'sqlite file exists in persist-directory' '
	test -f $PERSISTDIR/content/sqlite &&
	echo Size in bytes: $(stat --format "%s" $PERSISTDIR/content/sqlite)
'

test_expect_success 'kvsroot.final file exists in persist-directory' '
	test -f $PERSISTDIR/kvsroot.final
'

test_expect_success 'recover KVS snapshot from persist-directory in new instance' '
	run_timeout 10 \
	    flux start -o,--setattr=persist-directory=$PERSISTDIR \
	        "flux kvs put --treeobj snap=- <$PERSISTDIR/kvsroot.final && \
		    flux kvs get snap.testkey >testkey.out" &&
	echo 42 >testkey.exp &&
	test_cmp testkey.exp testkey.out
'

test_done
