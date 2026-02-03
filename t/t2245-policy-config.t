#!/bin/sh

test_description='Test RFC 33 policy config verification

Ensure that the broker catches violations of the configuration
specified in RFC 33 for [policy] and [queue.<name>.policy].
'

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 1 job -Slog-stderr-level=1

test_expect_success 'unknown policy key fails' '
	test_must_fail flux config load <<-EOT
	policy.foo = 1
	EOT
'
test_expect_success 'unknown policy.jobspec key fails' '
	test_must_fail flux config load <<-EOT
	policy.jobspec.foo = 1
	EOT
'
test_expect_success 'unknown policy.jobspec.defaults key fails' '
	test_must_fail flux config load <<-EOT
	policy.jobspec.defaults.foo = 1
	EOT
'
test_expect_success 'unknown policy.jobspec.defaults.system key fails' '
	test_must_fail flux config load <<-EOT
	policy.jobspec.defaults.system.foo = 1
	EOT
'
test_expect_success 'malformed policy.jobspec.defaults.system.duration fails' '
	test_must_fail flux config load <<-EOT
	policy.jobspec.defaults.system.duration = 1
	EOT
'
test_expect_success 'wrong type policy.jobspec.defaults.system.queue fails' '
	test_must_fail flux config load <<-EOT
	policy.jobspec.defaults.system.queue = 1
	EOT
'
test_expect_success 'unknown policy.limits key fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.foo = 1
	EOT
'
test_expect_success 'unknown policy.limits.job-size key fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.job-size.foo = 1
	EOT
'
test_expect_success 'unknown policy.limits.job-size.max key fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.job-size.max.foo = 1
	EOT
'
test_expect_success 'unknown policy.limits.job-size.min key fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.job-size.min.foo = 1
	EOT
'
test_expect_success 'incorrect policy.limits.job-size.min.nnodes fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.job-size.min.nnodes = -2
	EOT
'
test_expect_success 'malformed policy.limits.duration fails' '
	test_must_fail flux config load <<-EOT
	policy.limits.duration = 1.0
	EOT
'
test_expect_success 'unknown policy.access key fails' '
	test_must_fail flux config load <<-EOT
	policy.access.foo = 1.0
	EOT
'
test_expect_success 'malformed policy.access.allow-user key fails' '
	test_must_fail flux config load <<-EOT
	policy.access.allow-user = 1.0
	EOT
'
test_expect_success 'malformed policy.access.allow-group key fails' '
	test_must_fail flux config load <<-EOT
	policy.access.allow-group = 1.0
	EOT
'
test_expect_success 'malformed policy.access.allow-user entry fails' '
	test_must_fail flux config load <<-EOT
	policy.access.allow-user = [ 1 ]
	EOT
'
test_expect_success 'malformed policy.access.allow-group entry fails' '
	test_must_fail flux config load <<-EOT
	policy.access.allow-group = [ 1 ]
	EOT
'
test_expect_success 'well formed policy.access works' '
	flux config load <<-EOT
	[policy.access]
	allow-user = [ "alice", "bob" ]
	allow-group = [ "smurfs" ]
	EOT
'
test_expect_success 'malformed queues table fails' '
	test_must_fail flux config load <<-EOT
	queues = 1
	EOT
'
test_expect_success 'unknown queues.NAME.policy.foo key fails' '
	test_must_fail flux config load <<-EOT
	queues.x.policy.foo = 1
	EOT
'
test_expect_success 'malformed queues.NAME.policy.limits.duration key fails' '
	test_must_fail flux config load <<-EOT
	queues.x.policy.limits.duration = 1
	EOT
'
test_expect_success 'default queue as queue policy fails' '
	test_must_fail flux config load <<-EOT
	queues.x.policy.jobspec.defaults.system.queue = "x"
	EOT
'
test_expect_success 'unknown default queue fails' '
	test_must_fail flux config load <<-EOT
	[queues.foo]
	[policy]
	jobspec.defaults.system.queue = "bar"
	EOT
'
test_expect_success 'unknown queues.NAME.requires.foo key fails' '
	test_must_fail flux config load <<-EOT
	queues.x.requires = 1
	EOT
'
test_expect_success 'malformed queues.NAME.requires fails' '
	test_must_fail flux config load <<-EOT
	queues.x.requires = [ 1 ]
	EOT
'
test_expect_success 'malformed queues.NAME.requires property string' '
	test_must_fail flux config load <<-EOT
	queues.x.requires = [ "foo|bar" ]
	EOT
'
test_expect_success 'well formed queues.NAME.requires works' '
	flux config load <<-EOT
	[queues.x]
	requires = [ "batch" ]
	[queues.z]
	requires = [ "debug", "foo" ]
	EOT
'
# Example from flux-config-policy(5)
test_expect_success 'valid config passes' '
	flux config load <<-EOT
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

	[queues.batch]
	EOT
'

test_expect_success 'a bad config is detected at initialization too' '
	cat >badconf.toml <<-EOT &&
	policy.foo = 1
	EOT
	test_must_fail flux start --config-path=badconf.toml true
'

test_done
