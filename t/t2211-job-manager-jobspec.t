#!/bin/sh
test_description='Checkout job manager redacted jobspec'

. $(dirname $0)/sharness.sh

test_under_flux 1 full -Slog-stderr-level=1


RPC=${FLUX_BUILD_DIR}/t/request/rpc

# Usage: job_manager_getattr ID ATTR
job_manager_getattr() {
	echo '{"id":'$1',"attrs":["'$2'"]}' | ${RPC} job-manager.getattr
}

test_expect_success 'stop the queue for this test' '
	flux queue stop
'

test_expect_success 'create simple jobspec' '
	flux submit --dry-run hostname >simple.json
'

test_expect_success 'jobspec contains environment' '
	jq -e .attributes.system.environment <simple.json >env.json
'

test_expect_success 'jobspec contains duration' '
	jq -e .attributes.system.duration <simple.json
'

test_expect_success 'submit job' '
	flux job submit simple.json | flux job id >jobid
'

test_expect_success 'job-manager getattr of unknown attr fails' '
	test_must_fail job_manager_getattr $(cat jobid) noexist
'

test_expect_success 'job-manager getattr of jobspec works' '
	job_manager_getattr $(cat jobid) jobspec >getattr.json &&
	jq -e .jobspec <getattr.json >redacted.json
'

test_expect_success 'redacted jobspec contains duration' '
	jq -e .attributes.system.duration <redacted.json
'

test_expect_success 'redacted jobspec does not contain environment' '
	test_must_fail jq -e .attributes.system.environment <redacted.json
'

test_expect_success 'reload job manager and dependent modules' '
	flux module remove sched-simple &&
	flux module reload job-manager &&
	flux module load sched-simple
'

test_expect_success 'pending job still exists with same jobspec' '
	job_manager_getattr $(cat jobid) jobspec >getattr2.json &&
	jq -e .jobspec <getattr2.json >redacted2.json &&
	test_cmp redacted.json redacted2.json
'

test_expect_success 'restart the queue' '
	flux queue start
'

test_done
