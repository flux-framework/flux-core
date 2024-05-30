#!/bin/sh

test_description='Test flux job exec configuration'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)/conf.d
mkdir -p conf.d
test_under_flux 1 job

test_expect_success 'job-exec: can specify kill-timeout on cmdline' '
	flux dmesg -C &&
	flux module reload job-exec kill-timeout=1m &&
	flux module stats job-exec | jq ".[\"kill-timeout\"] == 60"
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
		flux module stats job-exec > ${name}.json 2>&1 &&
	cat ${name}.json | jq ".[\"kill-timeout\"] == 30"
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
	flux module stats -p bulk-exec.config.default_job_shell job-exec > stats1.out &&
	grep "/path/to/shell" stats1.out
'
test_expect_success 'job-exec: job-shell can be set in exec conf' '
	name=shellconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	job-shell = "my-flux-shell"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p bulk-exec.config.default_job_shell job-exec > ${name}.out 2>&1 &&
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
test_expect_success 'job-exec: update default shell via config reload' '
	flux module reload -f job-exec &&
	flux module stats -p bulk-exec.config.default_job_shell job-exec > reload1A.out 2>&1 &&
	grep "flux-shell" reload1A.out &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	job-shell = "/path/reload-shell"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.default_job_shell job-exec > reload1B.out 2>&1 &&
	grep "reload-shell" reload1B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: cmdline default shell takes priority' '
	flux module reload -f job-exec job-shell=/path/cmdline-shell &&
	flux module stats -p bulk-exec.config.default_job_shell job-exec > reload2A.out 2>&1 &&
	grep "cmdline-shell" reload2A.out &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	job-shell = "/path/reload-shell"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.default_job_shell job-exec > reload2B.out 2>&1 &&
	grep "cmdline-shell" reload2B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: can specify imp path on cmdline' '
	flux module reload -f job-exec imp=/path/to/imp &&
	flux module stats -p bulk-exec.config.flux_imp_path job-exec > stats2.out &&
	grep "/path/to/imp" stats2.out
'
test_expect_success 'job-exec: imp path can be set in exec conf' '
	name=impconf &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	imp = "my-flux-imp"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p bulk-exec.config.flux_imp_path job-exec > ${name}.out 2>&1 &&
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
# N.B. imp not configured by default
test_expect_success 'job-exec: update imp path via config reload' '
	flux module reload -f job-exec &&
	test_must_fail flux module stats -p bulk-exec.config.flux_imp_path job-exec > reload3A.out 2>&1 &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	imp = "/path/reload-imp"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.flux_imp_path job-exec > reload3B.out 2>&1 &&
	grep "reload-imp" reload3B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: cmdline imp path takes priority' '
	flux module reload -f job-exec imp=/path/cmdline-imp &&
	flux module stats -p bulk-exec.config.flux_imp_path job-exec > reload4A.out 2>&1 &&
	grep "cmdline-imp" reload4A.out &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	imp = "/path/reload-imp"
	EOF
	flux dmesg -C &&
	flux config reload &&
	flux module stats -p bulk-exec.config.flux_imp_path job-exec > reload4B.out 2>&1 &&
	grep "cmdline-imp" reload4B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: can specify exec service on cmdline' '
	flux module reload -f job-exec service=foo &&
	flux module stats -p bulk-exec.config.exec_service job-exec > stats3.out &&
	grep "foo" stats3.out
'
test_expect_success 'job-exec: exec service can be set in exec conf' '
	name=exec-service &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "bar"
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p bulk-exec.config.exec_service job-exec > ${name}.out 2>&1 &&
	grep "bar" ${name}.out
'
# N.B. exec service defaults to rexec
test_expect_success 'job-exec: update exec service via config reload' '
	flux module reload -f job-exec &&
	flux module stats -p bulk-exec.config.exec_service job-exec > reload5A.out 2>&1 &&
	grep "rexec" reload5A.out &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	service = "reload-exec-service"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.exec_service job-exec > reload5B.out 2>&1 &&
	grep "reload-exec-service" reload5B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: cmdline exec service takes priority' '
	flux module reload -f job-exec service=cmdline-exec-service &&
	flux module stats -p bulk-exec.config.exec_service job-exec > reload6A.out 2>&1 &&
	grep "cmdline-exec-service" reload6A.out &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	service = "reload-exec-service"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.exec_service job-exec > reload6B.out 2>&1 &&
	grep "cmdline-exec-service" reload6B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_expect_success 'job-exec: exec service override can be set in exec conf' '
	name=exec-service-override &&
	cat <<-EOF > ${name}.toml &&
	[exec]
	service = "bar"
	service-override = true
	EOF
	flux start -o,--config-path=${name}.toml -s1 \
		flux module stats -p bulk-exec.config.exec_service_override job-exec > ${name}.out 2>&1 &&
	val=$(cat ${name}.out) &&
	test $val -eq 1
'
# N.B. exec service defaults to off/disabled
test_expect_success 'job-exec: update exec service override via config reload' '
	flux module reload -f job-exec &&
	flux module stats -p bulk-exec.config.exec_service_override job-exec > reload7A.out 2>&1 &&
	val=$(cat reload7A.out) &&
	test $val -eq 0 &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	service-override = true
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.exec_service_override job-exec > reload7B.out 2>&1 &&
	val=$(cat reload7B.out) &&
	test $val -eq 1 &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
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
		flux module stats -p bulk-exec.config.sdexec_properties job-exec > ${name}.out 2>&1 &&
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
# N.B. sdexec properties defaults to not set
test_expect_success 'job-exec: update sdexec properties via config reload' '
	flux module reload -f job-exec &&
	test_must_fail flux module stats -p bulk-exec.config.sdexec_properties job-exec > reload8A.out 2>&1 &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	service = "sdexec"
	[exec.sdexec-properties]
	MemoryHigh = "200M"
	EOF
	flux config reload &&
	flux module stats -p bulk-exec.config.sdexec_properties job-exec > reload8B.out 2>&1 &&
	jq -e ".MemoryHigh == \"200M\"" < reload8B.out &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
# N.B. do config reload call before flux module reload to clear
# sdexec-properties from previous test
test_expect_success 'job-exec: bad sdexec properties leads to error on via config reload' '
	flux config reload &&
	flux module reload -f job-exec &&
	test_must_fail flux module stats -p bulk-exec.config.sdexec_properties job-exec > reload9A.out 2>&1 &&
	cat <<-EOF > ${FLUX_CONF_DIR}/exec.toml &&
	[exec]
	service = "sdexec"
	[exec.sdexec-properties]
	MemoryHigh = 42
	EOF
	test_must_fail flux config reload 2> reload9B.err &&
	grep "exec.sdexec-properties.MemoryHigh is not a string" reload9B.err &&
	rm -f ${FLUX_CONF_DIR}/exec.toml
'
test_done
