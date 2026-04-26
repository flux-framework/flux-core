#!/bin/sh

test_description='Test flux imp_exec_helper builtin'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

test_expect_success 'imp_exec_helper: too many args fails' '
	test_must_fail flux imp_exec_helper f1 f2
'

test_expect_success 'imp_exec_helper: no jobid without FLUX_JOB_ID fails' '
	test_must_fail env -u FLUX_JOB_ID flux imp_exec_helper
'

test_expect_success 'imp_exec_helper: unparsable jobid fails' '
	test_must_fail flux imp_exec_helper notajobid
'

test_expect_success 'imp_exec_helper: submit a test job' '
	jobid=$(flux submit true) &&
	flux job wait-event -t 10 ${jobid} clean
'

test_expect_success 'imp_exec_helper: non-existent jobid fails' '
	test_must_fail flux imp_exec_helper 9999999999
'

test_expect_success 'imp_exec_helper: output is valid JSON on stdout with J key' '
	flux imp_exec_helper ${jobid} >out.json &&
	jq -e .J out.json
'

test_expect_success 'imp_exec_helper: no options key without INVOCATION_ID' '
	flux imp_exec_helper ${jobid} >out.json &&
	test "$(jq "has(\"options\")" out.json)" = "false"
'

test_expect_success 'imp_exec_helper: FLUX_JOB_ID env var fallback works' '
	FLUX_JOB_ID=${jobid} flux imp_exec_helper >out.json &&
	jq -e .J out.json
'

test_expect_success 'imp_exec_helper: INVOCATION_ID wrong length is fatal' '
	test_must_fail env INVOCATION_ID=notvalidhex \
	    flux imp_exec_helper ${jobid}
'

test_expect_success 'imp_exec_helper: INVOCATION_ID invalid hex chars is fatal' '
	test_must_fail env INVOCATION_ID=GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG \
	    flux imp_exec_helper ${jobid}
'

test_expect_success 'imp_exec_helper: INVOCATION_ID set but sdbus unavailable is fatal' '
	test_must_fail env INVOCATION_ID=deadbeefdeadbeefdeadbeefdeadbeef \
	    flux imp_exec_helper ${jobid}
'

test_expect_success 'imp_exec_helper: --test-nojob with JOBID arg fails' '
	test_must_fail flux imp_exec_helper --test-nojob ${jobid}
'

test_expect_success 'imp_exec_helper: --test-nojob without INVOCATION_ID outputs empty object' '
	flux imp_exec_helper --test-nojob >nojob.json &&
	jq -e "type == \"object\" and length == 0" nojob.json
'

test_done
