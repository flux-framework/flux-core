#!/bin/sh
#
# ci=asan

test_description='Test flux job manager limit-job-size plugin'

. $(dirname $0)/sharness.sh

test_under_flux 2 full -Slog-stderr-level=1

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

test_expect_success 'configure an invalid job-size limit' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	job-size.max.nnodes = -42
	EOT
'
test_expect_success 'configure valid job-size.*.nnodes limits' '
	flux config load <<-EOT
	[policy.limits]
	job-size.max.nnodes = 2
	job-size.min.nnodes = 2
	EOT
'
test_expect_success 'a job that exceeds job-size.max.nnodes is rejected' '
	test_must_fail flux submit -N 3 true 2>max-nnodes.err &&
	grep "exceeds policy limit of 2" max-nnodes.err
'
test_expect_success 'a job that is under job-size.min.nnodes is rejected' '
	test_must_fail flux submit -N 1 true 2>min-nnodes.err &&
	grep "is under policy limit of 2" min-nnodes.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux submit -N 2 true
'
test_expect_success 'configure job-size.*.ncores limits' '
	flux config load <<-EOT
	[policy.limits]
	job-size.max.ncores = 2
	job-size.min.ncores = 2
	EOT
'
test_expect_success 'a job that exceeds job-size.max.ncores is rejected' '
	test_must_fail flux submit -n 3 true 2>max-ncores.err &&
	grep "exceeds policy limit of 2" max-ncores.err
'
test_expect_success 'a job that is under job-size.min.ncores is rejected' '
	test_must_fail flux submit -n 1 true 2>min-ncores.err &&
	grep "is under policy limit of 2" min-ncores.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux submit -n 2 true
'
test_expect_success 'configure job-size.*.ngpus limits' '
	flux config load <<-EOT
	[policy.limits]
	job-size.max.ngpus = 2
	job-size.min.ngpus = 2
	EOT
'
test_expect_success 'a job that exceeds job-size.max.ngpus is rejected' '
	test_must_fail flux submit -g 3 true 2>max-ngpus.err &&
	grep "exceeds policy limit of 2" max-ngpus.err
'
test_expect_success 'a job that is under job-size.min.ngpus is rejected' '
	test_must_fail flux submit -g 1 true 2>min-ngpus.err &&
	grep "is under policy limit of 2" min-ngpus.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux submit -g 2 true
'
test_expect_success 'configure job-size.max.ngpus and queue with unlimited' '
	flux config load <<-EOT &&
	[policy.limits]
	job-size.max.ngpus = 0
	[queues.debug]
	[queues.batch.policy.limits]
	job-size.max.ngpus = -1
	EOT
	flux queue start --all
'
test_expect_success 'a job is accepted if under general gpu limit' '
	flux submit --queue=debug -n1 true
'
test_expect_success 'a job is rejected if over gpu limit' '
	test_must_fail flux submit --queue=debug -n1 -g1 true 2>debug.err &&
	test_debug "cat debug.err"
'
test_expect_success 'error message contains queue name' '
	grep "for queue debug" debug.err
'
test_expect_success 'same job is accepted with unlimited queue override' '
	flux submit --queue=batch -n1 -g1 true
'
test_expect_success 'configure an invalid job-size object' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	job-size = "wrong"
	EOT
'
test_expect_success 'configure an out of bounds job-size.max.nnodes object' '
	test_must_fail flux config load <<-EOT
	[policy.limits]
	job-size.max.nnodes = -42
	EOT
'
test_expect_success 'configure an invalid queue job-size.min.nnodes object' '
	test_must_fail flux config load <<-EOT
	[queues.debug.policy.limits]
	job-size.min.nnodes = "xyz"
	EOT
'
test_expect_success 'configure an RFC 33 vqueue that inherits a parent limit' '
	flux config load <<-EOT &&
	[queues.batch.policy.limits]
	job-size.max.nnodes = 1
	[queues.debug.policy.limits]
	job-size.max.nnodes = 2
	[queues.expedite]
	parent = "batch"
	EOT
	flux queue start --all
'
test_expect_success 'vqueue enforces the inherited effective limit' '
	flux submit --queue=expedite -N1 true &&
	test_must_fail flux submit --queue=expedite -N2 true 2>vq.err &&
	grep "for queue expedite" vq.err
'
test_expect_success 'vqueue does not inherit a non-parent queue limit' '
	flux submit --queue=debug -N2 true &&
	test_must_fail flux submit --queue=expedite -N2 true
'
test_expect_success 'vqueue own limit overrides the inherited parent limit' '
	flux config load <<-EOT &&
	[queues.batch.policy.limits]
	job-size.max.nnodes = 1
	[queues.expedite]
	parent = "batch"
	policy.limits.job-size.max.nnodes = 2
	EOT
	flux queue start --all &&
	flux submit --queue=expedite -N2 true &&
	test_must_fail flux submit --queue=expedite -N3 true
'
test_expect_success 'per-queue limit in the initial config is enforced at startup' '
	mkdir -p prime.d &&
	cat >prime.d/queues.toml <<-EOT &&
	[queues.batch.policy.limits]
	job-size.max.nnodes = 1
	EOT
	cat >prime.sh <<-EOT &&
	#!/bin/sh
	flux queue start --all || exit 1
	flux submit --queue=batch -N1 true || exit 1
	flux submit --queue=batch -N2 true && exit 1
	exit 0
	EOT
	chmod +x ./prime.sh &&
	flux start --config-path=$(pwd)/prime.d ./prime.sh
'
#
# The job manager always delivers a configuration object to queues.update,
# never a JSON null and never a missing key: "no queues configured" is an
# object with an empty "queues" array. The queues-update test plugin fails
# the callback if that contract is broken, so loading it and reconfiguring
# is what exercises the contract.
#
test_expect_success 'clear queue configuration' '
	flux config load </dev/null
'
test_expect_success 'queues.update delivers an object with no queues configured' '
	flux jobtap load ${PLUGINPATH}/queues-update.so
'
test_expect_success 'queues.update delivers an object when queues are added' '
	flux config load <<-EOT &&
	[queues.batch.policy.limits]
	job-size.max.nnodes = 1
	[queues.expedite]
	parent = "batch"
	EOT
	flux queue start --all
'
test_expect_success 'queues.update delivers an object when queues are removed' '
	flux config load </dev/null
'
test_expect_success 'plugin is primed with an object at load time' '
	flux config load <<-EOT &&
	[queues.batch.policy.limits]
	job-size.max.nnodes = 1
	EOT
	flux queue start --all &&
	flux jobtap remove queues-update.so &&
	flux jobtap load ${PLUGINPATH}/queues-update.so
'
test_expect_success 'remove queues-update test plugin' '
	flux jobtap remove queues-update.so
'
test_done
