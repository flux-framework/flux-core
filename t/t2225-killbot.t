#!/bin/sh
test_description='Test job manager preemption plugin'

. $(dirname $0)/sharness.sh

test_under_flux 1

flux setattr log-stderr-level 1

NCORES=$(flux resource list -no "{ncores}")

test_expect_success 'load the killbot plugin' '
	flux jobtap load killbot.so
'
test_expect_success 'query the killbot plugin' '
	flux jobtap query killbot.so >query-default.json
'
test_expect_success 'setting an unknown killbot config key fails' '
	test_must_fail flux config load <<-EOT
	job-manager.killbot.unknown = 42
	EOT
'
test_expect_success 'setting kill-after = -1 fails' '
	test_must_fail flux config load <<-EOT
	job-manager.killbot.kill-after = -1
	EOT
'
test_expect_success 'setting kill-repeat = 0 fails' '
	test_must_fail flux config load <<-EOT
	job-manager.killbot.kill-repeat = 0
	EOT
'
test_expect_success 'setting handler = "unknown" fails' '
	test_must_fail flux config load <<-EOT
	job-manager.killbot.handler = "unknown"
	EOT
'
test_expect_success 'query shows config is still the default' '
	flux jobtap query killbot.so >query-after.json &&
	test_cmp query-default.json query-after.json
'
test_expect_success 'configure overkill handler' '
	flux config load <<-EOT
	[job-manager.killbot]
	kill-after = 1
	kill-repeat = 1
	handler = "overkill"
	EOT
'
test_expect_success 'submit preemptible jobs that use all cores' '
	seq 1 $NCORES | flux bulksubmit -n1 -Spreemptible-after=0 \
		--wait-event=start sleep 3600
'
test_expect_success 'killbot query shows ncores eligible victims' '
	flux jobtap query killbot.so >overkill-victims.json &&
	jq -e ".\"eligible-victims\" == $NCORES" <overkill-victims.json
'
test_expect_success 'submit a non-preemptible job that needs 1 core' '
	flux submit -n1 true
'
test_expect_success 'preemptible job runs' '
	run_timeout 30 flux job wait-event $(flux job last) clean
'
test_expect_success 'killbot query shows expected ncores kills' '
	flux jobtap query killbot.so >overkill-kills.json &&
	jq -e ".kills == $NCORES" <overkill-kills.json
'
test_expect_success 'reload the killbot plugin to reset stats' '
	flux jobtap remove killbot.so &&
	flux jobtap load killbot.so
'
test_expect_success 'configure onekill handler' '
	flux config load <<-EOT
	[job-manager.killbot]
	kill-after = 1
	kill-repeat = 1
	handler = "onekill"
	EOT
'
test_expect_success 'submit preemptible-after=2 jobs that use all cores' '
	seq 1 $NCORES | flux bulksubmit -n1 -Spreemptible-after=2 \
		--wait-event=start sleep 3600
'
test_expect_success 'submit a non-preemptible job that needs 1 core' '
	flux submit -n1 true
'
test_expect_success 'preemptible job runs' '
	run_timeout 30 flux job wait-event $(flux job last) clean
'
test_expect_success 'killbot query shows 1 kill' '
	flux jobtap query killbot.so >onekill-kills.json &&
	jq -e ".kills == 1" <onekill-kills.json
'
test_expect_success 'remove the killbot plugin' '
	flux jobtap remove killbot.so
'
test_done
