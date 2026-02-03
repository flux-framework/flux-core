#!/bin/sh
test_description='Test job ingest config file'

. $(dirname $0)/sharness.sh

#
# Setup a config dir for this test_under_flux
#
export FLUX_CONF_DIR=$(pwd)/conf.d
mkdir -p conf.d
cat <<EOF >conf.d/ingest.toml
[ingest.validator]
disable = true
EOF

test_under_flux 1 job -Slog-stderr-level=1

SUBMITBENCH="${FLUX_BUILD_DIR}/t/ingest/submitbench"
dmesg_grep=${SHARNESS_TEST_SRCDIR}/scripts/dmesg-grep.py

test_expect_success 'job-ingest: validator was disabled by config' '
	$dmesg_grep -t 5 "configuring validator with plugins=\(null\), args=\(null\) \(disabled\)"
'
test_expect_success 'job-ingest: command line overrides config' '
	rm conf.d/ingest.toml &&
	flux config reload &&
	flux dmesg -C &&
	flux module reload job-ingest disable-validator &&
	$dmesg_grep -t 10 "configuring validator with plugins=\(null\), args=\(null\) \(disabled\)"
'
test_expect_success 'job-ingest: configuration can be reloaded' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest.validator]
	plugins = [ "feasibility", "jobspec" ]
	args = [ "--require-version=1" ]
	EOF
	flux dmesg -C &&
	flux config reload &&
	$dmesg_grep -vt 10 \
	"configuring validator with plugins=feasibility,jobspec, args=--require-version=1 \(enabled\)"
'
test_expect_success 'job-ingest: verify that feasibility plugin is in effect' '
	test_must_fail flux submit -n 1024 hostname
'
test_expect_success 'job-ingest: worker buffer size can be set via config' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest]
	buffer-size = "10M"
	[ingest.validator]
	plugins = [ "feasibility", "jobspec" ]
	args = [ "--require-version=1" ]
	EOF
	flux dmesg -C &&
	flux config reload &&
	$dmesg_grep -vt 10 "worker input buffer set to 10M"
'
test_expect_success 'job-ingest: invalid buffer size is detected on reload' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest]
	buffer-size = "10f"
	[ingest.validator]
	plugins = [ "feasibility", "jobspec" ]
	args = [ "--require-version=1" ]
	EOF
	test_must_fail flux config reload >bad-buffer-size.out 2>&1 &&
	test_debug "cat bad-buffer-size.out" &&
	grep "Invalid buffer-size" bad-buffer-size.out
'
test_expect_success 'job-ingest: invalid config detected on reload' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest.validator]
	foo = 42
	args = "--require-version=1"
	EOF
	test_must_fail flux config reload &&
	flux dmesg | grep ingest
'
test_expect_success 'job-ingest: job still runs after failed config reload' '
	flux run true
'
test_expect_success 'job-ingest: invalid ingest.validator.plugins' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest.validator]
	plugins = [  42 ]
	EOF
	test_must_fail flux config reload
'
test_expect_success 'job-ingest: job still runs after failed config reload' '
	flux run true
'
test_expect_success 'job-ingest: invalid ingest.validator.plugins' '
	cat <<-EOF >conf.d/ingest.toml &&
	[ingest.validator]
	plugins = [ "feasibility" ]
	args = [ 1 ]
	EOF
	test_must_fail flux config reload
'
test_expect_success 'job-ingest: job still runs after failed config reload' '
	flux run true
'
test_done
