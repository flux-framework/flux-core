#!/bin/sh

test_description='Test resource module with system instance config'

. `dirname $0`/sharness.sh

test_expect_success 'create test R with unlikely core count' '
	flux R encode -r0-1 -c0-1048 >R.test
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

test_expect_success 'ranks can be excluded by configuration' '
	name=excluded &&
	mkdir $name &&
	flux R encode -r 0-1 --local > ${name}/R &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/${name}/R"
	exclude = "0"
	EOF
	flux start -s 2 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s up -no {nnodes} > ${name}/nnodes &&
	test "$(cat ${name}/nnodes)" = "1"
'

test_expect_success 'invalid exclude ranks cause instance failure' '
	name=bad-exclude &&
	mkdir $name &&
	flux R encode -r 0-1 --local > ${name}/R &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/${name}/R"
	exclude = "0-4"
	EOF
	test_must_fail flux start -s 2 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s up -no {nnodes} > ${name}/nnodes &&
    grep "out of range" ${name}/logfile
'

test_expect_success 'invalid exclude hosts cause instance failure' '
	name=bad-host-exclude &&
	mkdir $name &&
	flux R encode -r 0-1 --local > ${name}/R &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/${name}/R"
	exclude = "nosuchhost"
	EOF
	test_must_fail flux start -s 2 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s up -no {nnodes} > ${name}/nnodes &&
    grep "nosuchhost: Invalid argument" ${name}/logfile
'

test_expect_success HAVE_JQ 'gpu resources in configured R are not verified' '
	name=gpu-noverify &&
	mkdir $name &&
	flux R encode -r 0 --local | \
		jq .execution.R_lite[0].children.gpu=\"42-43\" > ${name}/R &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/${name}/R"
	EOF
	flux start -s 1\
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s up -no {rlist} > ${name}/rlist &&
	test_debug "cat ${name}/rlist" &&
	grep "gpu\[42-43\]" ${name}/rlist
'

test_done
