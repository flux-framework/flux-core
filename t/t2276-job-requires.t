#!/bin/sh

test_description='Test job constraints'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

test_expect_success HAVE_JQ 'flux-mini: --requires option works' '
	flux mini run --dry-run \
		--env=-* \
		--requires=foo,bar \
		--requires=^baz \
		 true | \
		jq '.attributes.system.constraints' > constraints.json &&
	test_debug "jq -S . < constraints.json" &&
	jq -e ".properties[0] == \"foo\"" < constraints.json &&
	jq -e ".properties[1] == \"bar\"" < constraints.json &&
	jq -e ".properties[2] == \"^baz\"" < constraints.json
'
test_expect_success 'reload scheduler with properties set' '
	flux kvs put resource.R="$(flux kvs get resource.R | \
		flux R set-property xx:2-3 yy:0-2)" &&
	flux module unload sched-simple &&
	flux module reload resource noverify &&
	flux module load sched-simple
'
test_expect_success 'reload ingest with feasibility validator' '
	flux module reload -f job-ingest validator-plugins=jobspec,feasibility
'
test_expect_success 'scheduler rejects jobs with invalid requires' '
	test_must_fail flux mini submit --requires=x hostname &&
	test_must_fail flux mini submit --requires="&x" hostname
'
test_expect_success 'scheduler rejects jobs with invalid constraints' '
	test_must_fail flux mini submit --setattr=system.constraints.foo=[] \
		 hostname &&
	test_must_fail flux mini submit --setattr=system.constraints.and={} \
		 hostname
'
test_expect_success 'flux-mini: --requires works with scheduler' '
	flux mini bulksubmit --wait --log=job.{}.id -n1 --requires={} \
		flux getattr rank \
		::: xx yy xx,yy ^xx ^yy &&
	result=$(flux job attach $(cat job.xx.id)) &&
	test_debug "echo xx: $result" &&
	test $result -eq 2 -o $result -eq 3 &&
	result=$(flux job attach $(cat job.yy.id)) &&
	test_debug "echo yy: $result" &&
	test $result -eq 0 -o $result -eq 1 &&
	result=$(flux job attach $(cat job.xx,yy.id)) &&
	test_debug "echo xx,yy: $result" &&
	test $result -eq 2 &&
	result=$(flux job attach $(cat "job.^yy.id")) &&
	test_debug "echo ^yy: $result" &&
	test $result -eq 3 &&
	result=$(flux job attach $(cat "job.^xx.id")) &&
	test_debug "echo ^xx: $result" &&
	test $result -eq 0 -o $result -eq 1
'
test_done
