#!/bin/sh

test_description='Test flux job manager limit-job-size plugin'

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 2 full -o,--config-path=$(pwd)/config

flux setattr log-stderr-level 1

test_expect_success 'configure an invalid job-size limit' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.nnodes = -42
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure valid job-size.*.nnodes limits' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.nnodes = 2
	job-size.min.nnodes = 2
	EOT
	flux config reload
'
test_expect_success 'a job that exceeds job-size.max.nnodes is rejected' '
	test_must_fail flux mini submit -N 3 /bin/true 2>max-nnodes.err &&
	grep "exceeds policy limit of 2" max-nnodes.err
'
test_expect_success 'a job that is under job-size.min.nnodes is rejected' '
	test_must_fail flux mini submit -N 1 /bin/true 2>min-nnodes.err &&
	grep "is under policy limit of 2" min-nnodes.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux mini submit -N 2 /bin/true
'
test_expect_success 'configure job-size.*.ncores limits' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.ncores = 2
	job-size.min.ncores = 2
	EOT
	flux config reload
'
test_expect_success 'a job that exceeds job-size.max.ncores is rejected' '
	test_must_fail flux mini submit -n 3 /bin/true 2>max-ncores.err &&
	grep "exceeds policy limit of 2" max-ncores.err
'
test_expect_success 'a job that is under job-size.min.ncores is rejected' '
	test_must_fail flux mini submit -n 1 /bin/true 2>min-ncores.err &&
	grep "is under policy limit of 2" min-ncores.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux mini submit -n 2 /bin/true
'
test_expect_success 'configure job-size.*.ngpus limits' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.ngpus = 2
	job-size.min.ngpus = 2
	EOT
	flux config reload
'
test_expect_success 'a job that exceeds job-size.max.ngpus is rejected' '
	test_must_fail flux mini submit -g 3 /bin/true 2>max-ngpus.err &&
	grep "exceeds policy limit of 2" max-ngpus.err
'
test_expect_success 'a job that is under job-size.min.ngpus is rejected' '
	test_must_fail flux mini submit -g 1 /bin/true 2>min-ngpus.err &&
	grep "is under policy limit of 2" min-ngpus.err
'
test_expect_success 'a job that is between both of those is accepted' '
	flux mini submit -g 2 /bin/true
'
test_expect_success 'configure job-size.max.ngpus and queue with unlimited' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.ngpus = 0
	[queues.debug.policy.limits]
	job-size.max.ngpus = -1
	EOT
	flux config reload
'
test_expect_success 'a job with no queue is accepted if under gpu limit' '
	flux mini submit -n1 /bin/true
'
test_expect_success 'a job with no queue is rejected if over gpu limit' '
	test_must_fail flux mini submit -n1 -g1 /bin/true
'
test_expect_success 'same job is accepted with unlimited queue override' '
	flux mini submit --queue=debug -n1 -g1 /bin/true
'
test_expect_success 'configure an invalid job-size object' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size = "wrong"
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure an out of bounds job-size.max.nnodes object' '
	cat >config/policy.toml <<-EOT &&
	[policy.limits]
	job-size.max.nnodes = -42
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure an invalid queue job-size.min.nnodes object' '
	cat >config/policy.toml <<-EOT &&
	[queues.debug.policy.limits]
	job-size.min.nnodes = "xyz"
	EOT
	test_must_fail flux config reload
'
test_done
