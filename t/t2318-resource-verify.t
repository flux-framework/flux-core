#!/bin/sh

test_description='Test resource module verify configuration'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_under_flux 1

# check if we have more than one CPU in our affinity mask
cores_allowed=$(flux R encode --local | flux R decode --count=core)
test $cores_allowed -gt 1 && test_set_prereq MULTICORE

undrain_all() {
        ranks="$(drained_ranks)"
        if test -n "$ranks"; then
            flux resource undrain $ranks
        fi
}

drained_ranks() { flux resource status -no {ranks} -s drain; }

no_drained_ranks() {
	if test "$(drained_ranks)" != ""; then
	    echo >&2 "expected no drained ranks, got: $(drained_ranks)"
	    return 1
	fi
}

is_drained() {
	if test "$(drained_ranks)" != "$1"; then
	    echo >&2 "drained ranks: expected $1, got $(drained_ranks)"
	    return 1
	fi
}

drain_reason_matches() {
	is_drained $1 &&
	flux resource drain -i $1 -no {reason} > reason.out &&
	if ! grep "$2" reason.out; then
	    echo >&2 "$1: expected reason: '$2'"
	    echo >&2 "               got: '$(cat reason.out)'"
	    return 1
        fi
}

test_expect_success 'load test R with unlikely core, GPU count' '
	flux R encode -r 0 -c 0-1047 -g 0-127 >R.test &&
	flux kvs put resource.R="$(cat R.test)"
'

# Test default behavior with implicit config
# Expected R has 1048 cores, 128 GPUs - actual system (likely!) has fewer
# With default config (allow-extra cores, strict hostname, ignore GPUs),
# missing cores will drain since allow-extra only allows EXTRA, not missing
test_expect_success 'default config drains on missing cores' '
	test_when_finished undrain_all &&
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

# Test explicit default configuration (allow-extra cores, ignore GPUs)
test_expect_success 'explicit default config drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-extra"
	gpu = "ignore"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

# To pass verification with test R, need allow-missing for cores
test_expect_success 'allow-missing cores allows missing cores' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test verify=true for strict verification
test_expect_success 'verify=true enables strict verification' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource]
	verify = true
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core.*gpu"
'

# Test verify=false disables all verification
test_expect_success 'verify=false disables all verification' '
	flux config load <<-EOF &&
	[resource]
	verify = false
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test noverify escape hatch
test_expect_success 'noverify=true disables all verification' '
	flux config load <<-EOF &&
	[resource]
	noverify = true
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test that noverify overrides verify table
test_expect_success 'noverify=true overrides verify table with default=strict' '
	flux config load <<-EOF &&
	[resource]
	noverify = true
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'noverify=true overrides verify.gpu=strict' '
	flux config load <<-EOF &&
	[resource]
	noverify = true
	[resource.verify]
	gpu = "strict"
	core = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'noverify=true overrides all strict settings' '
	flux config load <<-EOF &&
	[resource]
	noverify = true
	[resource.verify]
	core = "strict"
	gpu = "strict"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'noverify=false allows verify table to take effect' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource]
	noverify = false
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

# Test verify.default modes
test_expect_success 'verify.default=ignore disables all verification' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.default=allow-missing allows missing resources' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.default=allow-extra drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-extra"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

test_expect_success 'verify.default=strict drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

# Test empty verify table (should use strict default)
test_expect_success 'empty verify table uses strict default' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core.*gpu"
'

# Test selective verification: GPU modes
test_expect_success 'verify.gpu=strict drains on missing GPUs' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

test_expect_success 'verify.gpu=ignore allows missing GPUs' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.gpu=allow-missing allows missing GPUs' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.gpu=allow-extra drains on missing GPUs' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "allow-extra"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

# Test selective verification: core modes
test_expect_success 'verify.core=strict drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

test_expect_success 'verify.core=ignore allows missing cores' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.core=allow-missing allows missing cores' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.core=allow-extra drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-extra"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

# Test combinations of core and GPU settings
test_expect_success 'strict cores and GPUs drain on both missing' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core.*gpu"
'

test_expect_success 'allow-missing cores, strict GPUs drains on GPUs only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu" &&
	test_must_fail grep "core" reason.out
'

test_expect_success 'strict cores, allow-missing GPUs drains on cores only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "allow-missing"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core" &&
	test_must_fail grep "gpu" reason.out
'

test_expect_success 'ignore cores, strict GPUs drains on GPUs only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu" &&
	test_must_fail grep "core" reason.out
'

test_expect_success 'strict cores, ignore GPUs drains on cores only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core" &&
	test_must_fail grep "gpu" reason.out
'

test_expect_success 'allow-missing cores and GPUs passes' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'ignore cores and GPUs passes' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test default with overrides
test_expect_success 'default=strict with gpu=ignore drains on cores only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core" &&
	test_must_fail grep "gpu" reason.out
'

test_expect_success 'default=ignore with core=strict drains on cores only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "ignore"
	core = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core" &&
	test_must_fail grep "gpu" reason.out
'

test_expect_success 'default=ignore with gpu=strict drains on GPUs only' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "ignore"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu" &&
	test_must_fail grep "core" reason.out
'

test_expect_success 'default=allow-missing with gpu=strict drains on GPU' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu" &&
	test_must_fail grep "core" reason.out
'

test_expect_success 'default=allow-missing with core=strict drains on cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	core = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core" &&
	test_must_fail grep "gpu" reason.out
'

# Test hostname verification
test_expect_success 'create R with wrong hostname' '
	flux kvs get resource.R | \
		jq ".execution.nodelist[0] = \"wronghost\"" \
		>R.bad_host &&
	flux kvs put resource.R="$(cat R.bad_host)"
'

test_expect_success 'default hostname=strict drains on hostname mismatch' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "hostname.*wronghost"
'

test_expect_success 'verify.hostname=ignore allows hostname mismatch' '
	flux config load <<-EOF &&
	[resource.verify]
	hostname = "ignore"
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.hostname=allow-missing allows hostname mismatch' '
	flux config load <<-EOF &&
	[resource.verify]
	hostname = "allow-missing"
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify.hostname=allow-extra allows hostname mismatch' '
	flux config load <<-EOF &&
	[resource.verify]
	hostname = "allow-extra"
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'verify=true with hostname mismatch drains' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource]
	verify = true
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "hostname.*wronghost"
'

# Test with matching hostname
test_expect_success 'restore R with correct hostname' '
	flux kvs put resource.R="$(cat R.test)"
'

test_expect_success 'correct hostname does not drain with strict' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "allow-missing"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test all four modes for each resource type with test R
test_expect_success 'core=strict drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "ignore"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

test_expect_success 'core=allow-extra drains on missing cores' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-extra"
	gpu = "ignore"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

test_expect_success 'core=allow-missing allows missing cores' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'core=ignore allows missing cores' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "ignore"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'gpu=strict drains on missing GPUs' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "strict"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

test_expect_success 'gpu=allow-extra drains on missing GPUs' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "allow-extra"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

test_expect_success 'gpu=allow-missing allows missing GPUs' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "allow-missing"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'gpu=ignore allows missing GPUs' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "ignore"
	gpu = "ignore"
	hostname = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test error cases - config load fails when module is loaded
test_expect_success 'invalid verify mode fails config load' '
	test_must_fail flux config load <<-EOF 2>invalid.err &&
	[resource.verify]
	default = "invalid-mode"
	EOF
	grep "unknown verify mode" invalid.err
'

test_expect_success 'invalid resource type fails config load' '
	test_must_fail flux config load <<-EOF 2>invalid_type.err &&
	[resource.verify]
	memory = "strict"
	EOF
	grep "unsupported resource type" invalid_type.err
'

test_expect_success 'plural cores rejected' '
	test_must_fail flux config load <<-EOF 2>plural_cores.err &&
	[resource.verify]
	cores = "strict"
	EOF
	grep "unsupported resource type.*cores" plural_cores.err
'

test_expect_success 'plural gpus rejected' '
	test_must_fail flux config load <<-EOF 2>plural_gpus.err &&
	[resource.verify]
	gpus = "strict"
	EOF
	grep "unsupported resource type.*gpus" plural_gpus.err
'

test_expect_success 'verify as string fails config load' '
	test_must_fail flux config load <<-EOF 2>verify_string.err &&
	[resource]
	verify = "strict"
	EOF
	grep "must be a table" verify_string.err
'

test_expect_success 'verify as number fails config load' '
	test_must_fail flux config load <<-EOF 2>verify_number.err &&
	[resource]
	verify = 42
	EOF
	grep "must be a table" verify_number.err
'

test_expect_success 'verify as array fails config load' '
	test_must_fail flux config load <<-EOF 2>verify_array.err &&
	[resource]
	verify = ["core", "gpu"]
	EOF
	grep "must be a table" verify_array.err
'

test_expect_success 'verify.core as boolean fails config load' '
	test_must_fail flux config load <<-EOF 2>core_bool.err &&
	[resource.verify]
	core = true
	EOF
	grep "must be a string" core_bool.err
'

test_expect_success 'verify.core as number fails config load' '
	test_must_fail flux config load <<-EOF 2>core_num.err &&
	[resource.verify]
	core = 123
	EOF
	grep "must be a string" core_num.err
'

test_expect_success 'verify.core as array fails config load' '
	test_must_fail flux config load <<-EOF 2>core_array.err &&
	[resource.verify]
	core = ["strict"]
	EOF
	grep "must be a string" core_array.err
'

test_expect_success 'verify.default as boolean fails config load' '
	test_must_fail flux config load <<-EOF 2>default_bool.err &&
	[resource.verify]
	default = true
	EOF
	grep "must be a string" default_bool.err
'

# Test complex scenarios
test_expect_success 'default=strict with selective allow-missing' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	core = "allow-missing"
	gpu = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'default=allow-extra with selective strict' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-extra"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

test_expect_success 'default=allow-missing with selective strict' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	core = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*core"
'

test_expect_success 'default=ignore with all explicit strict' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "ignore"
	core = "strict"
	gpu = "strict"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

# Test realistic use cases
test_expect_success 'use case: GPU detection issues (ignore GPUs)' '
	flux config load <<-EOF &&
	[resource.verify]
	gpu = "ignore"
	core = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'use case: strict GPU inventory control' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "strict"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

test_expect_success 'use case: variable core counts allowed' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "allow-missing"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'use case: strict everything for production' '
	test_when_finished undrain_all &&
	flux config load <<-EOF &&
	[resource]
	verify = true
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources"
'

# Test config reload with verify config changes
test_expect_success 'config reload from strict to allow-missing' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	flux resource undrain 0 &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'config reload from allow-missing to strict' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "allow-missing"
	EOF
	flux module reload -f resource &&
	no_drained_ranks &&
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0
'

test_expect_success 'undrain for next test' '
	flux resource undrain 0
'

test_expect_success 'config reload changing individual resource mode' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "ignore"
	EOF
	flux module reload -f resource &&
	no_drained_ranks &&
	flux config load <<-EOF &&
	[resource.verify]
	core = "allow-missing"
	gpu = "strict"
	EOF
	flux module reload -f resource &&
	is_drained 0 &&
	drain_reason_matches 0 "missing resources.*gpu"
'

test_expect_success 'undrain for next test' '
	flux resource undrain 0
'

# Test that noverify overrides verify table (not verify boolean)
test_expect_success 'noverify=true overrides verify table' '
	flux config load <<-EOF &&
	[resource]
	noverify = true
	[resource.verify]
	default = "strict"
	core = "strict"
	gpu = "strict"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test with R that matches actual resources (if possible)
test_expect_success 'create R matching actual system resources' '
	flux R encode -r 0 --local >R.actual &&
	flux kvs put resource.R="$(cat R.actual)"
'

test_expect_success 'matching R: strict verification passes' '
	flux config load <<-EOF &&
	[resource]
	verify = true
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'matching R: all strict modes pass' '
	flux config load <<-EOF &&
	[resource.verify]
	default = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'matching R: explicit all strict passes' '
	flux config load <<-EOF &&
	[resource.verify]
	core = "strict"
	gpu = "strict"
	hostname = "strict"
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'matching R: default config passes' '
	flux config load <<-EOF &&
	# Default config
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

test_expect_success 'matching R: empty verify table passes' '
	flux config load <<-EOF &&
	[resource.verify]
	EOF
	flux module reload -f resource &&
	no_drained_ranks
'

# Test extra cores on multicore systems by running Flux as a job with 1 core
# and # disabling cpu-affinity. This will cause 1 core to be expected, but
# more available
test_expect_success 'ensure sched-simple is loaded' '
	flux module reload -f sched-simple
'

test_expect_success MULTICORE 'fewer cores: strict drains on extra cores' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify=true \
		flux resource drain -no {reason} > drain1.out &&
	test_debug "cat drain1.out" &&
	grep "extra resources.*core" drain1.out
'

test_expect_success MULTICORE 'fewer cores: allow-extra allows extra cores' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify.core=allow-extra \
		--conf=resource.verify.gpu=ignore \
		flux resource drain -no {reason} > drain2.out &&
	test_debug "cat drain2.out" &&
	test_must_be_empty drain2.out
'

test_expect_success MULTICORE 'fewer cores: allow-missing drains on extra cores' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify.core=allow-missing \
		--conf=resource.verify.gpu=ignore \
		flux resource drain -no {reason} > drain3.out &&
	test_debug "cat drain3.out" &&
	grep "extra resources.*core" drain3.out
'

test_expect_success MULTICORE 'fewer cores: ignore allows extra cores' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify.core=ignore \
		--conf=resource.verify.gpu=ignore \
		flux resource drain -no {reason} > drain4.out &&
	test_debug "cat drain4.out" &&
	test_must_be_empty drain4.out
'
test_expect_success MULTICORE 'fewer cores: verify=false allows extra cores' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify=false \
		flux resource drain -no {reason} > drain4.out &&
	test_debug "cat drain4.out" &&
	test_must_be_empty drain4.out
'
test_expect_success MULTICORE 'fewer cores: core=allow-extra allows extra' '
	flux alloc -n1 -o cpu-affinity=off \
		--conf=resource.verify.core=allow-extra \
		--conf=resource.verify.gpu=ignore \
		flux resource drain -no {reason} > drain5.out &&
	test_debug "cat drain5.out" &&
	test_must_be_empty drain5.out
'
test_done
