#!/bin/sh

test_description='Test flux job info lookup service'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc
INFOLOOKUP=${FLUX_BUILD_DIR}/t/job-info/info_lookup

fj_wait_event() {
	flux job wait-event --timeout=20 "$@"
}

# Usage: submit_job
# To ensure robustness of tests despite future job manager changes,
# cancel the job, and wait for clean event.
submit_job() {
	local jobid=$(flux job submit sleeplong.json) &&
	fj_wait_event $jobid start >/dev/null &&
	flux cancel $jobid &&
	fj_wait_event $jobid clean >/dev/null &&
	echo $jobid
}

get_timestamp_field() {
	local field=$1
	local file=$2
	grep $field $file | awk '{print $1}'
}

test_expect_success 'job-info: generate jobspec for simple test job' '
	flux run --dry-run -n1 -N1 sleep 300 > sleeplong.json
'

test_expect_success 'job-info.lookup returns jobid in response' '
	jobid=$(submit_job) &&
	id=$(flux job id --to=dec ${jobid}) &&
        $jq -j -c -n  "{id:${id}, keys:[\"jobspec\"], flags:0}" \
          | $RPC job-info.lookup > job_info_lookup.out &&
        cat job_info_lookup.out | $jq -e ".id == ${id}"
'

#
# job info lookup list w/o jobid & keys
#

test_expect_success 'flux job info fails without jobid' '
	test_must_fail flux job info
'

test_expect_success 'flux job info listing of keys works' '
	test_must_fail flux job info 2>list_keys.err &&
	grep J list_keys.err &&
	grep R list_keys.err &&
	grep eventlog list_keys.err &&
	grep jobspec list_keys.err &&
	grep guest.exec.eventlog list_keys.err &&
	grep guest.input list_keys.err &&
	grep guest.output list_keys.err
'

#
# job info lookup tests
#

test_expect_success 'flux job info eventlog works' '
	jobid=$(submit_job) &&
	flux job info $jobid eventlog > eventlog_info_a.out &&
	grep submit eventlog_info_a.out
'

test_expect_success 'flux job info eventlog fails on bad id' '
	test_must_fail flux job info 12345 eventlog
'

test_expect_success 'flux job info jobspec works' '
	jobid=$(submit_job) &&
	flux job info $jobid jobspec > jobspec_a.out &&
	grep sleep jobspec_a.out
'

test_expect_success 'flux job info --original jobspec works' '
	jobid=$(flux submit --env=ORIGINALTHING=t true) &&
	flux job info --original $jobid jobspec > jobspec_original.out &&
	grep ORIGINALTHING jobspec_original.out
'

test_expect_success 'flux job info applies jobspec updates' '
	jobid=$(flux submit --urgency=hold true) &&
	echo $jobid > updated_jobspec.id &&
	flux job info $jobid jobspec | jq -e ".attributes.system.duration == 0" &&
	flux update $jobid duration=100s &&
	flux job info $jobid jobspec | jq -e ".attributes.system.duration == 100.0" &&
	flux cancel $jobid
'

test_expect_success 'flux job info --base jobspec works' '
	flux job info --base $(cat updated_jobspec.id) jobspec \
	     | jq -e ".attributes.system.duration == 0"
'

test_expect_success 'flux job info jobspec fails on bad id' '
	test_must_fail flux job info 12345 jobspec
'

# N.B. In future may wish to update expiration via `flux update` tool
# when feature is available
test_expect_success 'flux job info applies resource updates' '
	jobid=$(flux submit --wait-event=start sleep 300) &&
	echo $jobid > updated_R.id &&
	flux job info $jobid R | jq -e ".execution.expiration == 0.0" &&
	flux update $jobid duration=300 &&
	flux job info $jobid R | jq -e ".execution.expiration > 0.0" &&
	flux job info $jobid eventlog &&
	flux cancel $jobid
'

test_expect_success 'flux job info --base R works' '
	flux job info --base $(cat updated_R.id) R \
	     | jq -e ".execution.expiration == 0.0"
'

#
# job info lookup tests (via eventlog)
#

test_expect_success 'flux job eventlog works' '
	jobid=$(submit_job) &&
	flux job eventlog $jobid > eventlog_a.out &&
	grep submit eventlog_a.out
'

test_expect_success 'flux job eventlog works on multiple entries' '
	jobid=$(submit_job) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs eventlog append ${kvsdir}.eventlog foo &&
	flux job eventlog $jobid >eventlog_b.out &&
	grep -q submit eventlog_b.out &&
	grep -q foo eventlog_b.out
'

test_expect_success 'flux job eventlog fails on bad id' '
	test_must_fail flux job eventlog 12345
'

test_expect_success 'flux job eventlog --format=json works' '
	jobid=$(submit_job) &&
	flux job eventlog --format=json $jobid > eventlog_format1.out &&
	grep -q "\"name\":\"submit\"" eventlog_format1.out &&
	grep -q "\"userid\":$(id -u)" eventlog_format1.out
'

test_expect_success 'flux job eventlog --format=text works' '
	jobid=$(submit_job) &&
	flux job eventlog --format=text $jobid > eventlog_format2.out &&
	grep -q "submit" eventlog_format2.out &&
	grep -q "userid=$(id -u)" eventlog_format2.out
'

test_expect_success 'flux job eventlog --format=invalid fails' '
	jobid=$(submit_job) &&
	test_must_fail flux job eventlog --format=invalid $jobid
'

test_expect_success 'flux job eventlog --time-format=raw works' '
	jobid=$(submit_job) &&
	flux job eventlog --time-format=raw $jobid > eventlog_time_format1.out &&
	get_timestamp_field submit eventlog_time_format1.out | grep "\."
'

test_expect_success 'flux job eventlog --time-format=iso works' '
	jobid=$(submit_job) &&
	flux job eventlog --time-format=iso $jobid > eventlog_time_format2.out &&
	get_timestamp_field submit eventlog_time_format2.out | grep T | grep Z
'

test "$(TZ=America/Los_Angeles date +%z)" != "+0000" &&
  test "$(TZ=America/Los_Angeles date +%Z)" != "UTC" &&
  test_set_prereq HAVE_TZ
test_expect_success HAVE_TZ 'flux job eventlog --time-format=iso uses local time' '
	jobid=$(submit_job) &&
	TZ=America/Los_Angeles \
	    flux job eventlog --time-format=iso $jobid > tzlocal.out &&
	test_debug "cat tzlocal.out" &&
	get_timestamp_field submit tzlocal.out \
	    | grep -E \
	    "^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+-[0-9]{2}:[0-9]{2}"
'

test_expect_success 'flux job eventlog --time-format=offset works' '
	jobid=$(submit_job) &&
	flux job eventlog --time-format=offset $jobid > eventlog_time_format3.out &&
	get_timestamp_field submit eventlog_time_format3.out | grep "0.000000" &&
	get_timestamp_field exception eventlog_time_format3.out | grep -v "0.000000"
'

test_expect_success 'flux job eventlog --time-format=invalid fails works' '
	jobid=$(submit_job) &&
	test_must_fail flux job eventlog --time-format=invalid $jobid
'

test_expect_success 'flux job eventlog -p works' '
	jobid=$(submit_job) &&
	flux job eventlog -p "eventlog" $jobid > eventlog_path1.out &&
	grep submit eventlog_path1.out
'

test_expect_success 'flux job eventlog -p works (guest.exec.eventlog)' '
	jobid=$(submit_job) &&
	flux job eventlog -p "guest.exec.eventlog" $jobid > eventlog_path2.out &&
	grep done eventlog_path2.out
'
test_expect_success 'flux job eventlog -p works (exec)' '
	jobid=$(submit_job) &&
	flux job eventlog -p exec $jobid > eventlog_path3.out &&
	grep done eventlog_path3.out
'
test_expect_success 'flux job eventlog -p works (output)' '
	jobid=$(submit_job) &&
	flux job eventlog -p output $jobid > eventlog_path4.out &&
	grep encoding eventlog_path4.out
'
test_expect_success 'flux job eventlog -p works (output)' '
	jobid=$(submit_job) &&
	flux job eventlog -p input $jobid > eventlog_path5.out &&
	grep encoding eventlog_path5.out
'
test_expect_success 'flux job eventlog -p fails on invalid path' '
	jobid=$(submit_job) &&
	test_must_fail flux job eventlog -p "foobar" $jobid
'
# submit job in separate test to avoid using the form:
#  jobid=$(flux submit ..) && flux job eventlog $jobid ... &
# which will pick up the wrong $jobid since the pipeline is placed into
# the background:
test_expect_success 'submit held job for eventlog follow test' '
	jobid=$(flux submit --urgency=hold -n1 hostname)
'
test_expect_success NO_CHAIN_LINT 'flux job eventlog -F, --follow works' '
	flux job eventlog -HF $jobid >eventlog-follow.out &
	pid=$! &&
	flux job urgency $jobid default &&
	wait $pid &&
	test_debug "cat eventlog-follow.out" &&
	grep clean eventlog-follow.out
'
#
#  Color and human-readable output tests for flux job eventlog/wait-event
#  
test_expect_success 'flux job eventlog -H, --human works' '
	#
        #  Note: --human option should format first timestamp of eventlog
        #   as [MmmDD HH:MM] and second line should be an offset thereof
        #   e.g. [  +0.NNNNNN]. The following regexes attempt to verify
        #   that --human produced this pattern.
        #
        flux job eventlog --human $jobid | sed -n 1p \
            | grep "^\[[A-Z][a-z][a-z][0-3][0-9] [0-9][0-9]:[0-9][0-9]\]" &&
        flux job eventlog --human $jobid | sed -n 2p \
            | grep "^\[ *+[0-9]*\.[0-9]*\]"
'
test_expect_success 'flux job wait-event -H, --human works' '
	#
        #  As above, but here we also verify that wait-event displays
	#   the datetime form on first matching event when -v is not used
	#
        flux job wait-event -v --human $jobid depend | sed -n 1p \
            | grep "^\[[A-Z][a-z][a-z][0-3][0-9] [0-9][0-9]:[0-9][0-9]\]" &&
        flux job wait-event -v --human $jobid depend | sed -n 2p \
            | grep "^\[ *+[0-9]*\.[0-9]*\]" &&
        flux job wait-event --human $jobid depend \
            | grep "^\[[A-Z][a-z][a-z][0-3][0-9] [0-9][0-9]:[0-9][0-9]\]"
'
has_color() {
	# To grep for ansi escape we need the help of the non-shell builtin
  	# printf(1), so run under env(1) so we don't get shell builtin:
        grep "$(env printf "\x1b\[")" $1 >/dev/null
}
test_expect_success 'flux job eventlog/wait-event reject invalid --color' '
        test_must_fail flux job eventlog --color=foo $jobid &&
        test_must_fail flux job wait-event --color=foo $jobid
'
for opt in "-HL" "-L" "-Lalways" "--color" "--color=always"; do
        test_expect_success "flux job eventlog $opt forces color on" '
                name=notty${opt##--color=} &&
                outfile=color-${name:-default}.out &&
                flux job eventlog ${opt} $jobid >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
        test_expect_success "flux job wait-event $opt forces color on" '
                name=notty${opt##--color=} &&
                outfile=color-${name:-default}.wait-event.out &&
                flux job wait-event ${opt} $jobid submit >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
done
for opt in "" "--color" "--color=always" "--color=auto" "-H"; do
        test_expect_success "flux job eventlog $opt displays color on tty" '
                name=${opt##--color=} &&
                outfile=color-${name:-default}.out &&
                runpty.py flux job eventlog ${opt} $jobid >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
        test_expect_success "flux kvs eventlog wait-event $opt displays color on tty" '
                name=${opt##--color=} &&
                outfile=color-${name:-default}.wait-event.out &&
                runpty.py flux job wait-event ${opt} $jobid submit >$outfile &&
                test_debug "cat $outfile" &&
                has_color $outfile
        '
done
test_expect_success "flux job eventlog --color=never disables color on tty" '
        opt="--color=never" &&
        name=${opt##--color=} &&
        outfile=color-${name:-default}.out &&
        runpty.py flux job eventlog ${opt} $jobid >$outfile &&
        test_debug "cat $outfile" &&
        test_must_fail has_color $outfile
'

#
# job info lookup tests (multiple keys in info request)
#

test_expect_success 'job-info.lookup multiple keys works (different keys)' '
	jobid=$(submit_job) &&
	${INFOLOOKUP} $jobid eventlog jobspec J > all_info_a.out &&
	grep submit all_info_a.out &&
	grep sleep all_info_a.out
'

test_expect_success 'job-info.lookup multiple keys works (same key)' '
	jobid=$(submit_job) &&
	${INFOLOOKUP} $jobid eventlog eventlog eventlog > eventlog_3.out &&
	test $(grep submit eventlog_3.out | wc -l) -eq 3
'

test_expect_success 'job-info.lookup multiple keys fails on bad id' '
	test_must_fail ${INFOLOOKUP} 12345 eventlog jobspec J
'

test_expect_success 'job-info.lookup multiple keys fails on 1 bad entry' '
	jobid=$(submit_job) &&
	kvsdir=$(flux job id --to=kvs $jobid) &&
	flux kvs unlink ${kvsdir}.jobspec &&
	test_must_fail ${INFOLOOKUP} $jobid eventlog jobspec J > all_info_b.out
'

#
# job info json decode tests
#

test_expect_success 'job-info.lookup: decode non-json returns string' '
	jobid=$(submit_job) &&
	${INFOLOOKUP} --json-decode $jobid eventlog > info_decode_1.out &&
	grep submit info_decode_1.out
'

test_expect_success 'job-info.lookup: decode json returns object' '
	jobid=$(submit_job) &&
	${INFOLOOKUP} --json-decode $jobid jobspec > info_decode_2.out &&
	grep sleep info_decode_2.out
'

#
# stats & corner cases
#

test_expect_success 'job-info lookup stats works' '
	flux module stats --parse lookups job-info
'

test_expect_success 'lookup request with empty payload fails with EPROTO(71)' '
	${RPC} job-info.lookup 71 </dev/null
'

test_expect_success 'lookup request with keys not and array fails with EPROTO(71)' '
	$jq -j -c -n  "{id:12345, keys:1, flags:0}" \
	  | ${RPC} job-info.lookup 71
'

test_expect_success 'lookup request with invalid keys fails with EPROTO(71)' '
	$jq -j -c -n  "{id:12345, keys:[1], flags:0}" \
	  | ${RPC} job-info.lookup 71
'

test_expect_success 'lookup request with invalid flags fails with EPROTO(71)' '
	$jq -j -c -n  "{id:12345, keys:[\"jobspec\"], flags:8191}" \
	  | ${RPC} job-info.lookup 71
'

#
# issue 6325
#
test_expect_success 'flux job info on bad job id gives good error message' '
	test_must_fail flux job info fuzzybunny R 2>fuzzy.err &&
	grep "invalid job id" fuzzy.err
'
test_expect_success 'flux job info on bad key gives good error message' '
	test_must_fail flux job info $(flux job last) badkey 2>badkey.err &&
	grep "key not found" badkey.err
'

test_done
