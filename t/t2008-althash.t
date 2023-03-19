#!/bin/sh
#

test_description='Test alternate hash config

Start a session with a non-default hash type for content/kvs.'

nil1="sha1-da39a3ee5e6b4b0d3255bfef95601890afd80709"
nil256="sha256-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if test -n "$S3_ACCESS_KEY_ID"; then
    test_set_prereq S3
    export FLUX_CONF_DIR=$(pwd)
fi

test_expect_success 'Started instance with content.hash=sha1' '
	OUT=$(flux start -o,-Scontent.hash=sha1 \
	    flux getattr content.hash) &&
	test "$OUT" = "sha1"
'

test_expect_success 'Started instance with content.hash=sha256' '
	OUT=$(flux start -o,-Scontent.hash=sha256 \
	    flux getattr content.hash) &&
	test "$OUT" = "sha256"
'

test_expect_success 'Started instance with content.hash=sha256,content-files' '
	OUT=$(flux start -o,-Scontent.hash=sha256 \
	    -o,-Scontent.backing-module=content-files \
	    -o,-Sstatedir=$(pwd) \
	    flux getattr content.hash) &&
	test "$OUT" = "sha256" &&
	ls -1 content.files | tail -1 | grep sha256
'

test_expect_success S3 'create creds.toml from env' '
	mkdir -p creds &&
	cat >creds/creds.toml <<-CREDS
	access-key-id = "$S3_ACCESS_KEY_ID"
	secret-access-key = "$S3_SECRET_ACCESS_KEY"
	CREDS
'

# N.B. don't reuse the bucket from previous tests
test_expect_success S3 'create content-s3.toml from env' '
	cat >content-s3.toml <<-TOML
	[content-s3]
	credential-file = "$(pwd)/creds/creds.toml"
	uri = "http://$S3_HOSTNAME"
	bucket = "${S3_BUCKET}althash"
	virtual-host-style = false
	TOML
'

test_expect_success S3 'Started instance with content.hash=sha256,content-s3' '
	OUT=$(flux start -o,-Scontent.hash=sha256 \
	    -o,-Scontent.backing-module=content-s3 \
	    flux getattr content.hash) &&
	test "$OUT" = "sha256"
'
test_expect_success S3 'Content store nil returns correct hash for sha256' '
	OUT=$(flux start -o,-Scontent.hash=sha256 \
	    -o,-Scontent.backing-module=content-s3 \
	    flux content store </dev/null) &&
	test "$OUT" = "$nil256"
'

test_expect_success 'Attempt to start instance with invalid hash fails hard' '
	test_must_fail flux start -o,-Scontent.hash=wronghash true
'

test_done
