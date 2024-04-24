#!/bin/sh

test_description='Test flux job exec configuration'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

test_expect_success 'job-exec: can specify kill-timeout on cmdline' '
	flux dmesg -C &&
	flux module reload job-exec kill-timeout=1m &&
	flux dmesg | grep "using kill-timeout of 60s"
'
test_expect_success 'job-exec: bad kill-timeout value causes module failure' '
	flux dmesg -C &&
	test_expect_code 1 flux module reload job-exec kill-timeout=1f &&
	flux dmesg | grep "invalid kill-timeout: 1f"
'
test_expect_success 'job-exec: kill-timeout can be set in exec conf' '
	name=killconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	kill-timeout = ".5m"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "using kill-timeout of 30s" ${name}.log
'
test_expect_success 'job-exec: bad kill-timeout config causes module failure' '
	name=bad-killconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	kill-timeout = "foo"
	EOF
	test_must_fail flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "invalid kill-timeout: foo" ${name}.log
'
test_expect_success 'job-exec: can specify default-shell on cmdline' '
	flux module reload -f job-exec job-shell=/path/to/shell &&
	flux module stats -p impl.bulk-exec.config.default_job_shell job-exec > stats1.out &&
	grep "/path/to/shell" stats1.out
'
test_expect_success 'job-exec: job-shell can be set in exec conf' '
	name=shellconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	job-shell = "my-flux-shell"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p impl.bulk-exec.config.default_job_shell job-exec > ${name}.out 2>&1 &&
	grep "my-flux-shell" ${name}.out
'
test_expect_success 'job-exec: bad job-shell config causes module failure' '
	name=bad-shellconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	job-shell = 42
	EOF
	test_must_fail flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "error reading config value exec.job-shell" ${name}.log
'
test_expect_success 'job-exec: can specify imp path on cmdline' '
	flux module reload -f job-exec imp=/path/to/imp &&
	flux module stats -p impl.bulk-exec.config.flux_imp_path job-exec > stats2.out &&
	grep "/path/to/imp" stats2.out
'
test_expect_success 'job-exec: imp path can be set in exec conf' '
	name=impconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	imp = "my-flux-imp"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p impl.bulk-exec.config.flux_imp_path job-exec > ${name}.out 2>&1 &&
	grep "my-flux-imp" ${name}.out
'
test_expect_success 'job-exec: bad imp config causes module failure' '
	name=bad-impconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	imp = 42
	EOF
	test_must_fail flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "error reading config value exec.imp" ${name}.log
'
test_expect_success 'job-exec: can specify exec service on cmdline' '
	flux module reload -f job-exec service=foo &&
	flux module stats -p impl.bulk-exec.config.exec_service job-exec > stats3.out &&
	grep "foo" stats3.out
'
test_expect_success 'job-exec: exec service can be set in exec conf' '
	name=exec-service &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "bar"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p impl.bulk-exec.config.exec_service job-exec > ${name}.out 2>&1 &&
	grep "bar" ${name}.out
'
test_expect_success 'job-exec: exec service override can be set in exec conf' '
	name=exec-service-override &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "bar"
	service-override = true
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p impl.bulk-exec.config.exec_service_override job-exec > ${name}.out 2>&1 &&
	val=$(cat ${name}.out) &&
	test $val -eq 1
'
test_expect_success 'job-exec: sdexex properties can be set in exec conf' '
	name=sdexec-properties &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "sdexec"
	[exec.sdexec-properties]
	MemoryHigh = "200M"
	MemoryMax = "100M"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p impl.bulk-exec.config.sdexec_properties job-exec > ${name}.out 2>&1 &&
	jq -e ".MemoryHigh == \"200M\"" < ${name}.out &&
	jq -e ".MemoryMax == \"100M\"" < ${name}.out
'
test_expect_success 'job-exec: bad sdexec properties causes module failure (type 1)' '
	name=bad-sdexec-properties1 &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "sdexec"
	sdexec-properties = 42
	EOF
	test_must_fail flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "exec.sdexec-properties is not a table" ${name}.log
'
test_expect_success 'job-exec: bad sdexec properties causes module failure (type 2)' '
	name=bad-sdexec-properties2 &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "sdexec"
	[exec.sdexec-properties]
	MemoryHigh = 42
	EOF
	test_must_fail flux start -o,--config-path=${name}.toml -s1 \
		flux dmesg > ${name}.log 2>&1 &&
	grep "exec.sdexec-properties.MemoryHigh is not a string" ${name}.log
'
test_done
