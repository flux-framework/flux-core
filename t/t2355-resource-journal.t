#!/bin/sh

test_description='Test the resource event journal'

. $(dirname $0)/sharness.sh

test_under_flux 2

test_expect_success 'unload the scheduler' '
	flux module remove sched-simple
'

test_expect_success 'flux resource eventlog --wait works' '
	flux resource eventlog -f json --wait=resource-define | tee eventlog
'
test_expect_success '1st event: restart online=""' '
	head -1 eventlog | tee eventlog.1 &&
	jq -e ".name == \"restart\"" eventlog.1 &&
	jq -e ".context.ranks == \"0-1\"" eventlog.1 &&
	jq -e ".context.online == \"\"" eventlog.1
'
test_expect_success 'that event was not posted to the KVS' '
	flux kvs get resource.eventlog >kvs &&
	test_must_fail grep -q restart kvs
'
test_expect_success 'last event: resource-define method=dynamic-discovery' '
	tail -1 eventlog | tee eventlog.2 &&
	jq -e ".name == \"resource-define\"" eventlog.2 &&
	jq -e ".context.method == \"dynamic-discovery\"" eventlog.2
'
test_expect_success 'resource-define event was posted to the KVS' '
	flux kvs get resource.eventlog >kvs &&
	grep -q resource-define kvs
'

test_expect_success 'reload resource with monitor-force-up' '
	flux module reload resource monitor-force-up
'
test_expect_success 'flux resource eventlog works' '
	flux resource eventlog -f json --wait=resource-define >forcelog
'
test_expect_success '1st event: restart online=0-1' '
	head -1 forcelog >forcelog.1 &&
	jq -e ".name == \"restart\"" forcelog.1 &&
	jq -e ".context.ranks == \"0-1\"" forcelog.1 &&
	jq -e ".context.online == \"0-1\"" forcelog.1
'

test_expect_success '2nd event: resource-define method=kvs' '
	head -2 forcelog | tail -1 | tee forcelog.2 &&
	jq -e ".name == \"resource-define\"" forcelog.2 &&
	jq -e ".context.method == \"kvs\"" forcelog.2
'

test_expect_success 'that event WAS posted to the KVS' '
	flux kvs get resource.eventlog >kvs2 &&
	test $(grep resource-define kvs2 | wc -l) -eq 2
'
test_expect_success NO_CHAIN_LINT 'watch eventlog in the background waiting on drain' '
	flux resource eventlog -F --wait=drain >bgeventlog &
	echo $! >bgpid
'
test_expect_success NO_CHAIN_LINT 'drain rank 1' '
	flux resource drain 1
'
test_expect_success NO_CHAIN_LINT 'background watcher completed successfully' '
	wait $(cat bgpid)
'

test_expect_success 'run restartable flux instance, drain 0' '
	flux start --setattr=statedir=$(pwd) \
	    sh -c "flux resource eventlog --wait=resource-define \
	    	&& flux resource drain 0 testing"
'
test_expect_success 'restart flux instance, dump eventlog' '
	flux start --setattr=statedir=$(pwd) \
	    flux resource eventlog -f json --wait=resource-define | tee restartlog
'
test_expect_success '1st event: drain idset=0 reason=testing' '
	head -1 restartlog | tee restartlog.1 &&
	jq -e ".name == \"drain\"" restartlog.1 &&
	jq -e ".context.idset == \"0\"" restartlog.1 &&
	jq -e ".context.reason == \"testing\"" restartlog.1
'
test_expect_success '2nd event: restart' '
	head -2 restartlog | tail -1 | tee restartlog.2 &&
	jq -e ".name == \"restart\"" restartlog.2
'
test_expect_success 'last event: resource-define method=kvs' '
	tail -1 restartlog | tee restartlog.3 &&
	jq -e ".name == \"resource-define\"" restartlog.3 &&
	jq -e ".context.method == \"kvs\"" restartlog.3
'

test_expect_success 'reload the scheduler' '
	flux module load sched-simple
'

test_done
