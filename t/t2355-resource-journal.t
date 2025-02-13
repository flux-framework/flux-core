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
test_expect_success 'ensure all ranks are undrained' '
	ranks=$(flux resource status -no {ranks} -s drain) &&
	if test -n "$ranks"; then
	    flux resource undrain $ranks
	fi
'
test_expect_success 'capture eventlog before truncation' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog.pre.out
'
test_expect_success 'set a journal size limit 1 less than current entries' '
	limit=$(($(flux resource eventlog | wc -l) - 1)) &&
	echo resource.journal-max=$limit | flux config load
'
test_expect_success 'eventlog is now truncated' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog.trunc
'
test_expect_success '1st event is a truncate event with expected context' '
	head -1 eventlog.trunc > eventlog.trunc.1 &&
	jq -e ".name == \"truncate\"" eventlog.trunc.1 &&
	jq -e ".context.online == \"0-1\"" eventlog.trunc.1 &&
	jq -e ".context.online == \"0-1\"" eventlog.trunc.1 &&
	jq -e ".context.torpid == \"\"" eventlog.trunc.1 &&
	jq -e ".context.drain == {}" eventlog.trunc.1
'
test_expect_success 'cause another event to be posted to the eventlog' '
	flux resource drain 0
'
test_expect_success '1st event is still a truncate event' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog2.trunc &&
	head -1 eventlog.trunc | jq -e ".name == \"truncate\""
'
test_expect_success 'cause another event to be posted to the eventlog' '
	flux resource undrain 0
'
test_expect_success '1st event is still a truncate event' '
	flux resource eventlog -H &&
	flux resource eventlog -f json > eventlog2.trunc &&
	head -1 eventlog.trunc | jq -e ".name == \"truncate\""
'
test_expect_success 'reload the scheduler' '
	flux module load sched-simple
'

test_done
