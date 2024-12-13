#!/bin/sh
#

test_description='Test instance restart'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if test -n "$S3_ACCESS_KEY_ID"; then
    test_set_prereq S3
    export FLUX_CONF_DIR=$(pwd)
fi

test_expect_success 'run a job in persistent instance' '
	flux start --setattr=statedir=$(pwd) \
	           flux submit true >id1.out
'

test_expect_success 'restart instance and run another job' '
	flux start --setattr=statedir=$(pwd) \
	           flux submit true >id2.out
'

test_expect_success 'restart instance and run another job' '
	flux start --setattr=statedir=$(pwd) \
	           flux submit true >id3.out
'

test_expect_success 'restart instance and list inactive jobs' '
	flux start --setattr=statedir=$(pwd) \
	           flux jobs --no-header --format={id} \
		   	--filter=INACTIVE >list.out
'

test_expect_success 'inactive job list contains all jobs run before' '
	grep $(cat id1.out) list.out &&
	grep $(cat id2.out) list.out &&
	grep $(cat id3.out) list.out
'

test_expect_success 'job IDs were issued in ascending order' '
	test $(cat id1.out | flux job id) -lt $(cat id2.out | flux job id) &&
	test $(cat id2.out | flux job id) -lt $(cat id3.out | flux job id)
'

test_expect_success 'restart instance and capture startlog' '
	flux start --setattr=statedir=$(pwd) \
	           flux startlog >startlog.out
'
test_expect_success 'startlog shows 5 run periods' '
	test $(wc -l <startlog.out) -eq 5
'
test_expect_success 'most recent period is still running' '
	tail -1 startlog.out | grep running
'

test_expect_success 'doctor startlog to look like a crash' '
	flux start --setattr=statedir=$(pwd) \
		-Sbroker.rc1_path=$SHARNESS_TEST_SRCDIR/rc/rc1-kvs \
		-Sbroker.rc3_path=$SHARNESS_TEST_SRCDIR/rc/rc3-kvs \
		flux startlog --post-start-event
'
test_expect_success 'run flux and capture logs on stderr' '
	flux start --setattr=statedir=$(pwd) \
		--setattr=log-stderr-level=6 \
		true 2>improper.err
'
test_expect_success 'improper shutdown was logged' '
	grep "Flux was not shut down properly" improper.err
'

test_expect_success 'run a job in persistent instance (content-files)' '
	flux start \
	    -Scontent.backing-module=content-files \
	    -Sstatedir=$(pwd) \
	    flux submit true >files_id1.out
'
test_expect_success 'restart instance and list inactive jobs' '
	flux start \
	    -Scontent.backing-module=content-files \
	    -Sstatedir=$(pwd) \
	    flux jobs --no-header --format={id} \
	        --filter=INACTIVE >files_list.out
'

test_expect_success 'inactive job list contains job from before restart' '
	grep $(cat files_id1.out) files_list.out
'

test_expect_success S3 'create creds.toml from env' '
	mkdir -p creds &&
	cat >creds/creds.toml <<-CREDS
	access-key-id = "$S3_ACCESS_KEY_ID"
	secret-access-key = "$S3_SECRET_ACCESS_KEY"
	CREDS
'

test_expect_success S3 'create content-s3.toml from env' '
	cat >content-s3.toml <<-TOML
	[content-s3]
	credential-file = "$(pwd)/creds/creds.toml"
	uri = "http://$S3_HOSTNAME"
	bucket = "$S3_BUCKET"
	virtual-host-style = false
	TOML
'

test_expect_success S3 'run a job in persistent instance (content-s3)' '
	flux start \
	    -Scontent.backing-module=content-s3 \
	    flux submit true >files_id2.out
'
test_expect_success S3 'restart instance and list inactive jobs' '
	flux start \
	    -Scontent.backing-module=content-s3 \
	    flux jobs --no-header --format={id} \
	        --filter=INACTIVE >files_list2.out
'

test_expect_success S3 'inactive job list contains job from before restart' '
	grep $(cat files_id2.out) files_list2.out
'

test_done
