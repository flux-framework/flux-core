#!/bin/sh

test_description='Test resource module with system instance config'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if which hwloc-bind > /dev/null; then
	NCORES=$(hwloc-bind --get | hwloc-calc --number-of core | tail -n 1)
	test $NCORES = 1 || test_set_prereq MULTICORE
fi

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
	jq --sort-keys . <R.test >R.test.normalized &&
	jq --sort-keys . <R.out >R.out.normalized &&
	test_cmp R.test.normalized R.out.normalized
'

test_expect_success 'both ranks were drained' '
	grep "draining: rank 0" logfile &&
	grep "draining: rank 1" logfile
'

test_expect_success 'invalid R causes instance to fail with error' '
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
    grep "invalid hosts: nosuchhost" ${name}/logfile
'

test_expect_success 'gpu resources in configured R are not verified' '
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

test_expect_success MULTICORE 'resource norestrict option works' '
	name=norestrict &&
	mkdir $name &&
	flux R encode -r 0 --local > ${name}/R &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	path = "$(pwd)/${name}/R"
	norestrict = true
	EOF
	hwloc-bind core:0 flux start -s1 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux run -N1 --exclusive \
		  sh -c "hwloc-bind --get | hwloc-calc --number-of core | tail -n1" \
		    >${name}/ncores &&
	test_debug "cat ${name}/ncores" &&
	test $(cat ${name}/ncores) = $NCORES
'

test_expect_success 'resources can be configured in TOML' '
	name=conftest &&
	mkdir -p $name &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	noverify = true
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-3"
	[[resource.config]]
	hosts = "foo[9,10]"
	gpus = "0-1"
	[[resource.config]]
	hosts = "foo[0-2]"
	properties = ["debug"]
	[[resource.config]]
	hosts = "foo[3-10]"
	properties = ["batch"]
	EOF
	flux start -s 1 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s all -n \
		   -o "{state} {properties} {nnodes} {ncores} {ngpus} {nodelist}" \
		   > ${name}/output &&
	test_debug "cat ${name}/output" &&
	cat <<-EOF >${name}/expected &&
	all debug 3 12 0 foo[0-2]
	all batch 8 32 4 foo[3-10]
	EOF
	test_cmp ${name}/expected ${name}/output
'
test_expect_success 'bad resource.config causes instance failure' '
	name=conftest-bad &&
	mkdir -p ${name} &&
	cat >${name}/resource.toml <<-EOF &&
	[resource]
	noverify = true
	config = []
	EOF
	test_must_fail flux start -s 1 \
		-o,--config-path=$(pwd)/${name},-Slog-filename=${name}/logfile \
		flux resource list -s up > ${name}/output 2>&1 &&
	test_debug "cat ${name}/output" &&
	grep "no hosts configured" ${name}/output
'

test_done
