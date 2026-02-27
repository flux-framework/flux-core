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
test_expect_success 'flux resource eventlog --include=BADARG raises error' '
	test_must_fail flux resource eventlog --include=foo
'
test_expect_success 'flux resource eventlog --include=BADRANK raises error' '
	test_must_fail flux resource eventlog --include=42
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
test_expect_success 'flux resource eventlog formats as text by default' '
	flux resource eventlog | grep method=
'
test_expect_success 'flux resource eventlog --time-format=raw works' '
	flux resource eventlog --time-format=raw > out.raw &&
	test_debug "cat out.raw" &&
	grep resource-define out.raw | awk "{print \$1}" \
		| grep "^[^.]*\.[^.]*$"
'
test_expect_success 'flux resource eventlog --time-format=iso works' '
	TZ=UTC flux resource eventlog --time-format=iso > out.iso &&
	test_debug "cat out.iso" &&
	grep resource-define out.iso | awk "{print \$1}" | grep "Z$"
'
test_expect_success 'flux resource eventlog --time-format=offset works' '
	TZ=UTC flux resource eventlog --time-format=offset > out.offset &&
	test_debug "cat out.offset" &&
	head -n1 out.offset | awk "{print \$1}" | grep "^ *0\.0"
'
test_expect_success 'flux resource eventlog --time-format=human works' '
	TZ=UTC flux resource eventlog --time-format=human > out.Thuman &&
	test_debug "cat out.Thuman" &&
	head -n1 out.Thuman | grep "^\[...[0-9][0-9] [0-9][0-9]:[0-9][0-9]\]"
'
test_expect_success 'flux resource eventlog --human works' '
	TZ=UTC flux resource eventlog --human > out.human &&
	test_debug "cat out.human" &&
	head -n1 out.human | grep "^\[...[0-9][0-9] [0-9][0-9]:[0-9][0-9]\]"
'
has_color() {
        # To grep for ansi escape we need the help of the non-shell builtin
        # printf(1), so run under env(1) so we don't get shell builtin:
        grep "$(env printf "\x1b\[")" $1 >/dev/null
}
for opt in "-HL" "-L" "-Lalways" "--color" "--color=always"; do
        test_expect_success "flux resource eventlog $opt forces color on" '
                name=notty${opt##--color=} &&
                outfile=color-${name:-default}.out &&
                flux resource eventlog ${opt} >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
done
for opt in "" "--color" "--color=always" "--color=auto" "-H"; do
        test_expect_success "flux resource eventlog $opt shows color on tty" '
                name=${opt##--color=} &&
                outfile=color-${name:-default}.out &&
                runpty.py flux resource eventlog ${opt} $jobid >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
done
for opt in "-HLnever" "-Lnever" "--color=never"; do
	test_expect_success "flux resource eventlog $opt disables color on tty" '
		name=${opt##--color=} &&
		outfile=color-${name:-default}.out &&
		runpty.py flux resource eventlog ${opt} >$outfile &&
		test_debug "cat $outfile" &&
		test_must_fail has_color $outfile
	'
done

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
	flux resource eventlog -HF --wait=drain >bgeventlog &
	echo $! >bgpid &&
	# ensure RPC is established before moving on
	waitfile.lua -vt 15 bgeventlog
'
test_expect_success NO_CHAIN_LINT 'drain rank 1' '
	flux resource drain 1
'
test_expect_success NO_CHAIN_LINT 'background watcher completed successfully' '
	wait $(cat bgpid)
'
test_expect_success NO_CHAIN_LINT 'background watcher waited for drain event' '
	test_debug "cat bgeventlog" &&
	tail -1 bgeventlog | grep drain
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
test_expect_success 'ensure all ranks are undrained' '
	ranks=$(flux resource status -no {ranks} -s drain) &&
	if test -n "$ranks"; then
	    flux resource undrain $ranks
	fi
'
test_idset_includes_rank1()
{
	echo "$1" \
	| jq -e 'any(.context | .idset // .ranks; . == "1" or . == "0-1")'
}
test_expect_success 'flux resource eventlog --include works' '
	flux resource eventlog --include=1 -f json > include1.json &&
	# Note: we assume size=2 here such that matching ranks are only
	# "0-1" or "1", not "0". If the test size is changed this test
	# may need to be updated
	cat include1.json | while read line; do
		test_idset_includes_rank1 "$line"
	done
'
# Note: all brokers running on same host so this feature
# can't be fully tested here. Just make sure there's no errors and
# the result is not empty:
test_expect_success 'flux resource eventlog --include works with hostlist' '
	flux resource eventlog -H --include="$(flux hostlist -ln1)" \
		> include-host.out &&
	test_must_fail test_must_be_empty include-host.out
'
test_expect_success '--match-context=BADARG raises error' '
	test_must_fail flux resource eventlog --match-context=noequals
'
test_expect_success '--match-context=BADARG with empty key raises error' '
	test_must_fail flux resource eventlog --match-context==val
'
test_expect_success '--match-context matches a single key/value pair' '
	flux resource eventlog -f json \
		--wait=resource-define \
		--match-context=method=kvs \
		--timeout=10 > tmp.out &&
	tail -1 tmp.out | tee mc.out &&
	jq -e ".name == \"resource-define\"" mc.out &&
	jq -e ".context.method == \"kvs\"" mc.out
'
test_expect_success '--match-context matches multiple key/value pairs' '
	flux resource drain 0 testing &&
	flux resource undrain 0 &&
	flux resource eventlog -f json \
		--wait=drain \
		--match-context=idset=\"0\" \
		--match-context=reason=testing > tmp.out &&
	tail -1 tmp.out | tee mc2.out &&
	jq -e ".name == \"drain\"" mc2.out &&
	jq -e ".context.idset == \"0\"" mc2.out &&
	jq -e ".context.reason == \"testing\"" mc2.out
'
update_expiration() {
	flux python -c "import flux; flux.Flux().rpc(\"resource.expiration-update\",{\"expiration\": $1},nodeid=0).get()"
}
test_expect_success '--match-context matches floats with some tolerance' '
	# Add two resource-update events, one that wont match and one that will
	update_expiration 0. &&
	ts=$(( $(date +%s) + 3600)) &&
	update_expiration $ts &&
	flux resource eventlog -f json \
		--wait=resource-update \
		--match-context=expiration=${ts}.000001 > tmp.out &&
	tail -1 tmp.out | tee mc3.out &&
	jq -e ".name == \"resource-update\"" mc3.out &&
	jq -e ".context.expiration == $ts" mc3.out
'
test_expect_success '--timeout works when wait condition is never met' '
	test_must_fail flux resource eventlog \
		--wait=drain \
		--match-context=reason=nonexistent \
		--timeout=0.5
'
test_expect_success '--timeout does not fire if event is found in time' '
	flux resource eventlog \
		--wait=resource-define \
		--match-context=method=kvs \
		--timeout=30
'
test_expect_success '--timeout=BADARG raises error' '
	test_must_fail flux resource eventlog --timeout=notanumber
'
# Truncation tests follow:
test_expect_success 'capture eventlog before truncation' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog.pre.out
'
test_expect_success 'set a journal size limit 1 less than current entries' '
	limit=$(($(flux resource eventlog | wc -l) - 1)) &&
	test_debug "echo limiting resource.journal to $limit entries" &&
	echo resource.journal-max=$limit | flux config load
'
test_expect_success 'eventlog is now truncated' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog.trunc
'
test_expect_success 'truncated eventlog has expected number of entries' '
	test_debug "wc -l eventlog.trunc" &&
	test $(wc -l < eventlog.trunc) -eq $limit
'
test_expect_success '1st event is a truncate event' '
	head -1 eventlog.trunc > eventlog.trunc.1 &&
	jq -e ".name == \"truncate\"" eventlog.trunc.1
'
test_expect_success 'cause another event to be posted to the eventlog' '
	flux resource drain 0
'
test_expect_success '1st event is still a truncate event' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog2.trunc &&
	head -1 eventlog2.trunc | jq -e ".name == \"truncate\""
'
test_expect_success 'truncated eventlog has expected number of entries' '
	test $(wc -l < eventlog2.trunc) -eq $limit
'
test_expect_success 'cause another event to be posted to the eventlog' '
	flux resource undrain 0
'
test_expect_success '1st event is still a truncate event' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog3.trunc &&
	head -1 eventlog3.trunc | jq -e ".name == \"truncate\""
'
test_expect_success 'truncated eventlog has expected number of entries' '
	test $(wc -l < eventlog3.trunc) -eq $limit
'
test_expect_success 'reload the scheduler' '
	flux module load sched-simple
'

test_done
