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

lastword () {
	awk '{ print $NF }'
}

test_expect_success 'run a job in persistent instance' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | lastword >id1.out
'

test_expect_success 'restart instance and run another job' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | lastword >>id2.out
'

test_expect_success 'restart instance and run another job' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux mini run -v /bin/true 2>&1 | lastword >>id3.out
'

test_expect_success 'restart instance and list inactive jobs' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux jobs --suppress-header --format={id} \
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

test_expect_success 'run a job in persistent instance (content-files)' '
	flux start \
	    -o,-Scontent.backing-module=content-files \
	    -o,-Scontent.backing-path=$(pwd)/content.files \
	    flux mini run -v /bin/true 2>&1 | lastword >files_id1.out
'
test_expect_success 'restart instance and list inactive jobs' '
	flux start \
	    -o,-Scontent.backing-module=content-files \
	    -o,-Scontent.backing-path=$(pwd)/content.files \
	    flux jobs --suppress-header --format={id} \
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
	    -o,-Scontent.backing-module=content-s3 \
	    flux mini run -v /bin/true 2>&1 | lastword >files_id2.out
'
test_expect_success S3 'restart instance and list inactive jobs' '
	flux start \
	    -o,-Scontent.backing-module=content-s3 \
	    flux jobs --suppress-header --format={id} \
	        --filter=INACTIVE >files_list2.out
'

test_expect_success S3 'inactive job list contains job from before restart' '
	grep $(cat files_id2.out) files_list2.out
'

test_done
