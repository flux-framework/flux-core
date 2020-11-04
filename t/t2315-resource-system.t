#!/bin/sh

test_description='Test resource module with system instance config'

. `dirname $0`/sharness.sh

test_expect_success 'create test R with unlikely core count' '
	flux R encode -r0-1 -c0-36 >R.test
'

test_expect_success 'create config file pointing to test R' '
	cat >resource.toml <<-EOT
	[resource]
	path="$(pwd)/R.test"
	EOT
'

test_expect_success 'start instance with config file and dump R' '
	flux start -s2 \
		-o,--config-path=$(pwd),-Slog-filename=logfile \
		flux kvs get resource.R >R.out
'

test_expect_success 'dumped R matches configured R' '
	test_cmp R.test R.out
'

test_expect_success 'both ranks were drained' '
	grep "draining: rank 0" logfile &&
	grep "draining: rank 1" logfile
'

test_expect_success HAVE_JQ 'invalid R causes instance to fail with error' '
	mkdir bad &&
	flux R encode -r 0-1 -c 0-1 | jq ".version = 42" > bad/R &&
	cat >bad/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/bad/R"
	EOF
	test_must_fail flux start -s2 \
		-o,--config-path=$(pwd)/bad,-Slog-filename=bad/logfile \
		flux kvs get resource.R >bad/R.out &&
	grep "error parsing R: invalid version=42" bad/logfile
'

test_expect_success 'missing ranks in R are drained' '
	mkdir missing &&
	flux R encode -r 0-1 --local > missing/R &&
	cat >missing/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/missing/R"
	EOF
	flux start -s3 \
		-o,--config-path=$(pwd)/missing,-Slog-filename=missing/logfile \
		flux kvs get resource.R >missing/R.out &&
	grep "draining: rank 2 not found in expected ranks" missing/logfile
'

test_done
