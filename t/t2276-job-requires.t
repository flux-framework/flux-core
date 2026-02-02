#!/bin/sh

test_description='Test job constraints'

. $(dirname $0)/sharness.sh

test_under_flux 4 job -Slog-stderr-level=1

test_expect_success 'flux run: --requires option works' '
	flux run --dry-run \
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
test_expect_success 'reload ingest with only feasibility validator' '
	flux module reload -f job-ingest validator-plugins=feasibility
'
test_expect_success 'scheduler rejects jobs with invalid requires' '
	test_must_fail flux submit --requires=x hostname &&
	test_must_fail flux submit --requires="&x" hostname
'
test_expect_success 'scheduler rejects jobs with invalid constraints' '
	test_must_fail flux submit --setattr=system.constraints.foo=[] \
		 hostname &&
	test_must_fail flux submit --setattr=system.constraints.and={} \
		 hostname
'
test_expect_success 'reload ingest with feasibility,jobspec validators' '
	flux module reload -f job-ingest validator-plugins=jobspec,feasibility
'
test_expect_success 'scheduler rejects jobs with unsatisfiable constraints' '
	test_must_fail flux submit -N4 --requires=yy hostname
'
test_expect_success 'jobspec validator rejects invalid hostlist/ranks' '
	test_must_fail flux submit -n1 --requires=host:f[ hostname &&
	test_must_fail flux submit -n1 --requires=ranks:1-0 hostname
'
test_expect_success 'jobspec validator rejects invalid constraint operation' '
	test_must_fail flux submit -n1 --requires=foo:bar hostname
'
test_expect_success 'flux bulksubmit: --requires works with scheduler' '
	flux bulksubmit --wait --log=job.{}.id -n1 --requires={} \
		flux getattr rank \
		::: xx yy xx,yy ^xx ^yy \
		    rank:1 rank:3 -rank:0-2 \
		    host:$(hostname) &&
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
	test $result -eq 0 -o $result -eq 1 &&
	result=$(flux job attach $(cat "job.rank:1.id")) &&
	test_debug "echo rank:1: $result" &&
	test $result -eq 1 &&
	result=$(flux job attach $(cat "job.rank:3.id")) &&
	test_debug "echo rank:3: $result" &&
	test $result -eq 3 &&
	result=$(flux job attach $(cat "job.-rank:0-2.id")) &&
	test_debug "echo -rank:0-2: $result" &&
	test $result -eq 3 &&
	result=$(flux job attach $(cat "job.host:$(hostname).id")) &&
	test_debug "echo host:hostname: $result"
'
test_expect_success 'scheduler does not schedule down nodes with constraints' '
	flux resource drain 2 &&
	id=$(flux submit -N1 --requires xx,yy flux getattr rank) &&
	flux job wait-event $id priority &&
	flux jobs &&
	test $(flux jobs -no {state} $id) = "SCHED" &&
	flux resource undrain 2 &&
	flux job wait-event -vt 5 $id clean
'
test_expect_success 'scheduler does not schedule down nodes with constraints' '
	flux resource drain 0 &&
	id=$(flux submit -N3 --requires yy flux getattr rank) &&
	flux job wait-event $id priority &&
	flux jobs &&
	test $(flux jobs -no {state} $id) = "SCHED" &&
	flux resource undrain 0 &&
	flux job wait-event -vt 5 $id clean
'
test_done
