#!/bin/sh
test_description='Test job ingest pipeline'

. $(dirname $0)/sharness.sh

test_under_flux 1 full

flux setattr log-stderr-level 1

test_expect_success 'no workers are running at the start' '
	flux module stats job-ingest >stats.out &&
	jq -e ".pipeline.frobnicator.running == 0" <stats.out &&
	jq -e ".pipeline.validator.running == 0" <stats.out
'
test_expect_success 'run a job with no ingest configuration' '
	flux run true
'
test_expect_success 'one validator, no frobnicator started' '
	flux module stats job-ingest >stats2.out &&
	jq -e ".pipeline.frobnicator.running == 0" <stats2.out &&
	jq -e ".pipeline.validator.running == 1" <stats2.out
'
test_expect_success 'configure frobnicator' '
	flux config load <<-EOT
	[policy.jobspec.defaults.system]
	duration = "10s"
	[ingest.frobnicator]
	plugins = [ "defaults" ]
	EOT
'
test_expect_success 'run a job with unspecified duration' '
	flux submit true >jobid1
'
test_expect_success 'one validator, one frobnicator started' '
	flux module stats job-ingest >stats3.out &&
	jq -e ".pipeline.frobnicator.running == 1" <stats3.out &&
	jq -e ".pipeline.validator.running == 1" <stats3.out
'
test_expect_success 'job duration was assigned from default' '
	flux job info $(cat jobid1) jobspec >jobspec1 &&
	jq -e ".attributes.system.duration == 10" <jobspec1
'
test_expect_success 'force module config update' '
	flux module stats job-ingest >stats4.out &&
	jq -r ".pipeline.frobnicator.pids[0]" <stats4.out >frob.pid &&
	jq -r ".pipeline.validator.pids[0]" <stats4.out >val.pid &&
	flux config get | flux config load
'
test_expect_success 'run a job to trigger work crew with new config' '
	flux submit true
'
test_expect_success 'workers were restarted' '
	flux module stats job-ingest >stats5.out &&
	jq -r ".pipeline.frobnicator.pids[0]" <stats5.out >frob2.pid &&
	jq -r ".pipeline.validator.pids[0]" <stats5.out >val2.pid &&
	test_must_fail test_cmp frob.pid frob2.pid &&
	test_must_fail test_cmp val.pid val2.pid
'
test_expect_success 'run a job with novalidate flag' '
	jq -r ".pipeline.frobnicator.requests" <stats5.out >frob.count &&
	jq -r ".pipeline.validator.requests" <stats5.out >val.count &&
	flux run --flags novalidate true
'
test_expect_success 'job was frobbed but not validated' '
	flux module stats job-ingest >stats6.out &&
	jq -r ".pipeline.frobnicator.requests" <stats6.out >frob2.count &&
	jq -r ".pipeline.validator.requests" <stats6.out >val2.count &&
	test_must_fail test_cmp frob.count frob2.count &&
	test_cmp val.count val2.count
'
test_expect_success 'reconfig with null config' '
	flux config load </dev/null
'
test_expect_success 'run a job with novalidate flag' '
	flux run --flags novalidate true
'
test_expect_success 'job was neither frobbed nor validated' '
	flux module stats job-ingest >stats7.out &&
	jq -r ".pipeline.frobnicator.requests" <stats7.out >frob3.count &&
	jq -r ".pipeline.validator.requests" <stats7.out >val3.count &&
	test_cmp frob2.count frob3.count &&
	test_cmp val2.count val3.count
'
test_expect_success 'run a job' '
	flux run true
'
test_expect_success 'job was validated but not frobbed' '
	flux module stats job-ingest >stats8.out &&
	jq -r ".pipeline.frobnicator.requests" <stats8.out >frob4.count &&
	jq -r ".pipeline.validator.requests" <stats8.out >val4.count &&
	test_cmp frob3.count frob4.count &&
	test_must_fail test_cmp val3.count val4.count
'
test_expect_success 'stop validator 0' '
	valpid=$(jq -r ".pipeline.validator.pids[0]" <stats8.out) &&
	kill -STOP $valpid
'
test_expect_success 'remove job-ingest to trigger cleanup' '
	flux setattr log-stderr-level 7 &&
	flux module remove job-ingest &&
	flux setattr log-stderr-level 1
'

test_done
