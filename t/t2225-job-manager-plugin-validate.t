#!/bin/sh

test_description='Test job manager validation of jobtap plugin data'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

PLUGINPATH=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs

# Build an "attributes.system.constraints" update nested $1 levels deep,
# in the shape that caused issue #7815.
wrapped_constraints() {
	python3 -c "
import json, sys
o = {'hostlist': ['foo']}
for i in range($1):
    o = {'and': [o]}
print (json.dumps ({'attributes.system.constraints': o}))
"
}

test_expect_success 'plugin: load validate-limits plugin' '
	flux jobtap load ${PLUGINPATH}/validate-limits.so
'
#  The plugin does all of its work in the job.state.depend callback, so
#   the job only needs to reach the DEPEND state, not run.
test_expect_success 'plugin: submit a job through the plugin' '
	id=$(flux submit --urgency=hold true) &&
	flux job wait-event -t 30 $id depend &&
	echo $id >jobid
'
test_expect_success 'plugin: deeply nested jobspec update was rejected' '
	flux job eventlog $(cat jobid) >eventlog.out &&
	test_debug "cat eventlog.out" &&
	grep "deep_update_rejected=1" eventlog.out
'
test_expect_success 'plugin: normally nested jobspec update was accepted' '
	grep "shallow_update_accepted=1" eventlog.out
'
test_expect_success 'plugin: exception with invalid type was rejected' '
	grep "bad_exception_type_rejected=1" eventlog.out
'
test_expect_success 'plugin: exception with invalid severity was rejected' '
	grep "bad_exception_severity_rejected=1" eventlog.out
'
test_expect_success 'plugin: event with invalid name was rejected' '
	grep "bad_event_name_rejected=1" eventlog.out
'
test_expect_success 'plugin: event with deeply nested context was rejected' '
	grep "deep_event_context_rejected=1" eventlog.out
'
test_expect_success 'plugin: normal event was still accepted' '
	grep "normal_event_accepted=1" eventlog.out
'
test_expect_success 'plugin: empty dependency description was rejected' '
	grep "empty_dependency_rejected=1" eventlog.out
'
test_expect_success 'plugin: overlong dependency description was rejected' '
	grep "long_dependency_rejected=1" eventlog.out
'
test_expect_success 'plugin: deeply nested annotations did not appear' '
	test_must_fail grep "\"deep\"" eventlog.out
'
#  A plugin may set R from job.state.sched, so submit a job that is not
#   held and therefore reaches SCHED. The deeply nested R it returns must
#   be refused, leaving the job without an R of its own.
#
#  N.B. this instance has no resources, so the job then takes a fatal
#   alloc exception and becomes inactive on its own.
test_expect_success 'plugin: submit a job that reaches SCHED' '
	schedid=$(flux submit --wait-event=priority true) &&
	flux job wait-event -t 30 $schedid priority &&
	echo $schedid >schedid
'
test_expect_success 'plugin: deeply nested R was rejected' '
	flux dmesg | grep "R exceeds maximum nesting depth" &&
	test_must_fail flux job info $(cat schedid) R
'
test_expect_success 'plugin: unload validate-limits plugin' '
	flux jobtap remove validate-limits.so
'

#
#  The job-manager.update RPC accepts updates from users, and must apply
#   the same limits.
#
test_expect_success 'update: deeply nested update via RPC is rejected' '
	id=$(flux submit --urgency=hold true) &&
	wrapped_constraints 200 >deep.json &&
	test_must_fail flux python -c "
import flux, json
h = flux.Flux ()
updates = json.load (open (\"deep.json\"))
h.rpc (\"job-manager.update\",
       {\"id\": $(flux job id $id), \"updates\": updates}).get ()
" 2>update.err &&
	test_debug "cat update.err" &&
	grep -i "depth" update.err &&
	flux cancel $id
'
#  N.B. a jobspec key is only updatable if a plugin handles it, so use
#   duration here, which the builtin update plugin supports.
test_expect_success 'update: ordinary update via RPC still succeeds' '
	id=$(flux submit --urgency=hold true) &&
	flux update $id duration=1m &&
	flux job wait-event -t 30 $id jobspec-update &&
	flux cancel $id
'

#
#  flux_jobtap_jobspec_update_id_pack(3) updates a job asynchronously,
#   from outside a callback for that job.  The jobspec-update plugin
#   exposes it as a service, and raises a "test" exception when the call
#   fails.
#
test_expect_success 'update: load jobspec-update plugin' '
	flux jobtap load ${PLUGINPATH}/jobspec-update.so
'
test_expect_success 'update: deeply nested async update is rejected' '
	id=$(flux submit --urgency=hold true) &&
	wrapped_constraints 200 >deep2.json &&
	flux python -c "
import flux, json
h = flux.Flux ()
update = json.load (open (\"deep2.json\"))
h.rpc (\"job-manager.jobspec-update.update\",
       {\"id\": $(flux job id $id), \"update\": update}).get ()
" &&
	flux job wait-event -t 30 -m type=test $id exception &&
	flux dmesg | grep "rejecting jobspec update"
'
test_expect_success 'update: ordinary async update still succeeds' '
	id=$(flux submit --urgency=hold true) &&
	flux python -c "
import flux
h = flux.Flux ()
h.rpc (\"job-manager.jobspec-update.update\",
       {\"id\": $(flux job id $id),
        \"update\": {\"attributes.system.job.name\": \"test\"}}).get ()
" &&
	flux job wait-event -t 30 $id jobspec-update &&
	flux cancel $id
'
test_expect_success 'update: unload jobspec-update plugin' '
	flux jobtap remove jobspec-update.so
'

#
#  Memos come directly from users and must also be bounded.
#
test_expect_success 'memo: deeply nested memo is rejected' '
	id=$(flux submit --urgency=hold true) &&
	test_must_fail flux python -c "
import flux
h = flux.Flux ()
o = 1
for i in range (200):
    o = {\"a\": o}
h.rpc (\"job-manager.memo\", {\"id\": $(flux job id $id), \"memo\": {\"deep\": o}}).get ()
" 2>memo.err &&
	test_debug "cat memo.err" &&
	grep -i "depth" memo.err &&
	flux cancel $id
'
test_expect_success 'memo: memo that is not an object is rejected' '
	id=$(flux submit --urgency=hold true) &&
	test_must_fail flux python -c "
import flux
h = flux.Flux ()
h.rpc (\"job-manager.memo\", {\"id\": $(flux job id $id), \"memo\": 42}).get ()
" 2>memo2.err &&
	test_debug "cat memo2.err" &&
	grep "memo must be an object" memo2.err &&
	flux cancel $id
'
test_expect_success 'memo: normal memo still works' '
	id=$(flux submit --urgency=hold true) &&
	flux job memo $id foo=42 &&
	flux job wait-event -t 30 $id memo &&
	flux cancel $id
'

#
#  The regression test for #7815: the rejected updates must not have
#   reached job-list, which exited when it could not deserialize them.
#
test_expect_success 'job-list: module is still running' '
	flux module list | grep job-list
'
test_expect_success 'job-list: still services requests' '
	flux jobs -a >jobs.out &&
	test_debug "cat jobs.out"
'
#  The depth limit exists so that consumers which are not using jansson
#   can still deserialize the result, so read the journal back through the
#   Python bindings, which use Python's json module.
test_expect_success 'job-list: journal is readable by python json' '
	flux python -c "
import flux
from flux.job.journal import JournalConsumer
h = flux.Flux ()
c = JournalConsumer (h, full=True).start ()
n = 0
try:
    while n < 500:
        if c.poll (timeout=5.0) is None:
            break
        n += 1
except TimeoutError:
    pass
c.stop ()
print (f\"read {n} events\")
assert n > 0, \"no events read from journal\"
"
'

test_done
