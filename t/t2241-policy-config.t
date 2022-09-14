#!/bin/sh

test_description='Test RFC 33 policy config verification

Ensure that the broker catches violations of the configuration
specified in RFC 33 for [policy] and [queue.<name>.policy].
'

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 1 minimal -o,--config-path=$(pwd)/config

flux setattr log-stderr-level 1

test_expect_success 'unknown policy key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.jobspec key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.jobspec.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.jobspec.defaults key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.jobspec.defaults.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.jobspec.defaults.system key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.jobspec.defaults.system.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.jobspec.defaults.system.duration fails' '
	cat >config/policy.toml <<-EOT &&
	policy.jobspec.defaults.system.duration = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'wrong type policy.jobspec.defaults.system.queue fails' '
	cat >config/policy.toml <<-EOT &&
	policy.jobspec.defaults.system.queue = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.limits key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.limits.job-size key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.job-size.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.limits.job-size.max key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.job-size.max.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.limits.job-size.min key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.job-size.min.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'incorrect policy.limits.job-size.min.nnodes fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.job-size.min.nnodes = -2
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.limits.duration fails' '
	cat >config/policy.toml <<-EOT &&
	policy.limits.duration = 1.0
	EOT
	test_must_fail flux config reload
'
test_expect_success 'unknown policy.access key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.access.foo = 1.0
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.access.allow-user key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.access.allow-user = 1.0
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.access.allow-group key fails' '
	cat >config/policy.toml <<-EOT &&
	policy.access.allow-group = 1.0
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.access.allow-user entry fails' '
	cat >config/policy.toml <<-EOT &&
	policy.access.allow-user = [ 1 ]
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed policy.access.allow-group entry fails' '
	cat >config/policy.toml <<-EOT &&
	policy.access.allow-group = [ 1 ]
	EOT
	test_must_fail flux config reload
'
test_expect_success 'well formed policy.access works' '
	cat >config/policy.toml <<-EOT &&
	[policy.access]
	allow-user = [ "alice", "bob" ]
	allow-group = [ "smurfs" ]
	EOT
	flux config reload
'
test_expect_success 'unknown queues.NAME.policy.foo key fails' '
	cat >config/policy.toml <<-EOT &&
	queues.x.policy.foo = 1
	EOT
	test_must_fail flux config reload
'
test_expect_success 'malformed queues.NAME.policy.limits.duration key fails' '
	cat >config/policy.toml <<-EOT &&
	queues.x.policy.limits.duration = 1
	EOT
	test_must_fail flux config reload
'
# Example from flux-config-policy(5)
test_expect_success 'valid config passes' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	duration = "1h"
	queue = "batch"

	[policy.limits]
	duration = "4h"
	job-size.max.nnodes = 8
	job-size.max.ngpus = 4

	[queues.debug.policy.limits]
	duration = "30m"
	job-size.max.ngpus = -1  # unlimited
	EOT
	flux config reload
'
test_done
