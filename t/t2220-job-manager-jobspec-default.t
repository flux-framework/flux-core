#!/bin/sh

test_description='Test flux job manager jobspec-default plugin'

. $(dirname $0)/sharness.sh

mkdir -p config

test_under_flux 1 full -o,--config-path=$(pwd)/config

RPC=${FLUX_BUILD_DIR}/t/request/rpc

# Usage: job_manager_getattr ID ATTR
jm_getattr() {
	local id=$(flux job id --to=dec $1)
        echo '{"id":'$id',"attrs":["'$2'"]}' | ${RPC} job-manager.getattr
}

flux setattr log-stderr-level 1

test_expect_success 'confirm builtin jobspec-default plugin is loaded' '
	flux jobtap list -a | grep jobspec-default
'
test_expect_success 'reload config with default policy' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	test = 42
	EOT
	flux config reload
'
test_expect_success 'submit a job' '
	flux mini submit /bin/true >id
'
test_expect_success 'jobspec-update is posted to the job' '
	run_timeout 10 flux job wait-event -v $(cat id) jobspec-update
'
test_expect_success HAVE_JQ 'the redacted jobspec contains the default' '
	jm_getattr $(cat id) jobspec >getattr.json &&
	jq -e ".jobspec.attributes.system.test == 42" <getattr.json
'
test_expect_success 'submit a job that overrides the configured default' '
	flux mini submit --setattr=system.test=66 /bin/true >id2
'
test_expect_success HAVE_JQ 'the redacted jobspec contains the override' '
	jm_getattr $(cat id2) jobspec >getattr2.json &&
	jq -e ".jobspec.attributes.system.test == 66" <getattr2.json
'
test_expect_success 'update the default policy without restarting flux' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	test = 43
	EOT
	flux config reload
'
test_expect_success 'submit a job' '
	flux mini submit /bin/true >id3
'
test_expect_success HAVE_JQ 'the redacted jobspec contains the new default' '
	jm_getattr $(cat id3) jobspec >getattr3.json &&
	jq -e ".jobspec.attributes.system.test == 43" <getattr3.json
'
test_expect_success 'configure a default duration' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	duration = 3600
	EOT
	flux config reload
'
test_expect_success 'submit a job' '
	flux mini submit /bin/true >id4
'
test_expect_success HAVE_JQ 'the redacted jobspec contains default duration' '
	jm_getattr $(cat id4) jobspec >getattr4.json &&
	jq -e ".jobspec.attributes.system.duration == 3600" <getattr4.json
'
test_expect_success 'submit a job that overrides the default duration' '
	flux mini submit -t5s /bin/true >id5
'
test_expect_success HAVE_JQ 'the redacted jobspec contains the override' '
	jm_getattr $(cat id5) jobspec >getattr5.json &&
	jq -e ".jobspec.attributes.system.duration == 5" <getattr5.json
'
test_expect_success 'configure a default duration as an FSD' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	duration = "15m"
	EOT
	flux config reload
'
test_expect_success 'submit a job' '
	flux mini submit /bin/true >id6
'
test_expect_success HAVE_JQ 'the redacted jobspec contains default duration' '
	jm_getattr $(cat id6) jobspec >getattr6.json &&
	jq -e ".jobspec.attributes.system.duration == 900" <getattr6.json
'
test_expect_success 'configuring a malformed policy fails' '
	cat >config/policy.toml <<-EOT &&
	policy = 42
	EOT
	test_must_fail flux config reload 2>malformed.err
'
test_expect_success 'and with a human readable error message' '
	grep -i "expected object, got integer" malformed.err
'
test_expect_success 'new instance refuses to start with malformed policy' '
	test_must_fail flux start -o,--config-path=$(pwd)/config \
		/bin/true 2>start.err
'
test_expect_success 'and with a human readable error message' '
	grep -i "expected object, got integer" start.err
'
test_expect_success 'configure a default queue with no [queues] entry' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	queue = "pdebug"
	[queues]
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure a queue with malformed [queues] entry' '
	cat >config/policy.toml <<-EOT &&
	[queues]
	pdebug = 42
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure a queue that is not a table' '
	cat >config/policy.toml <<-EOT &&
	queues = 42
	EOT
	test_must_fail flux config reload
'
test_expect_success 'configure duration with queue override' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	duration = 88
	[queues.pdebug.policy.jobspec.defaults.system]
	duration = 99
	EOT
	flux config reload
'
test_expect_success 'reload plugin' '
	flux jobtap load --remove=all .jobspec-default
'
test_expect_success 'submit a job with no queue or duration' '
	flux mini submit /bin/true >id7
'
test_expect_success HAVE_JQ 'the redacted jobspec contains default duration' '
	jm_getattr $(cat id7) jobspec >getattr7.json &&
	jq -e ".jobspec.attributes.system.duration == 88" <getattr7.json
'
test_expect_success 'submit a job with with queue but no duration' '
	flux mini submit --setattr=system.queue=pdebug /bin/true >id8
'
test_expect_success HAVE_JQ 'the redacted jobspec contains queue duration' '
	jm_getattr $(cat id8) jobspec >getattr8.json &&
	jq -e ".jobspec.attributes.system.duration == 99" <getattr8.json
'
test_expect_success 'submit a job with with queue and duration' '
	flux mini submit --setattr=system.queue=pdebug -t 111s /bin/true >id9
'
test_expect_success HAVE_JQ 'the redacted jobspec contains user duration' '
	jm_getattr $(cat id9) jobspec >getattr9.json &&
	jq -e ".jobspec.attributes.system.duration == 111" <getattr9.json
'
test_expect_failure 'submit a job to unknown queue fails' '
	test_must_fail flux mini submit \
		--setattr=system.queue=pbatch /bin/true 2>submit.err &&
	grep "Error parsing pbatch queue policy" submit.err
'
test_expect_success 'configure default queue with queue defined duration' '
	cat >config/policy.toml <<-EOT &&
	[policy.jobspec.defaults.system]
	queue = "pbatch"
	[queues.pbatch.policy.jobspec.defaults.system]
	duration = 123
	[queues.pdebug.policy.jobspec.defaults.system]
	duration = 456
	EOT
	flux config reload
'
test_expect_success 'submit a job' '
	flux mini submit /bin/true >id10
'
test_expect_success HAVE_JQ 'default queue duration was used' '
	jm_getattr $(cat id10) jobspec >getattr10.json &&
	jq -e ".jobspec.attributes.system.duration == 123" <getattr10.json
'

test_done
