#!/bin/sh

test_description='Test flux job info lookup service'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

RPC=${FLUX_BUILD_DIR}/t/request/rpc

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

test_expect_success 'flux job eventlog -p fails on invalid path' '
	jobid=$(submit_job) &&
	test_must_fail flux job eventlog -p "foobar" $jobid
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

test_done
