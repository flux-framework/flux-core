#!/bin/sh

test_description='Test resource eventlog checkpointing and eventlog truncation'

. `dirname $0`/sharness.sh

SIZE=4
test_under_flux $SIZE full --test-hosts=fake[0-3]

has_resource_event () {
	flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}

# fake hostnames match the ones set on the broker command line
test_expect_success 'load fake resources' '
	flux module remove sched-simple &&
	flux R encode -r 0-3 -c 0-1 -H fake[0-3] >R &&
	flux resource reload R &&
	flux module load sched-simple
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

#
# first set of tests, check drain state checkpoint and truncation by
# default
#

test_expect_success 'resource module checkpointed empty drain state' '
	flux kvs get checkpoint.resource > drain_state1.out &&
	jq -e ".drain_state == {}" < drain_state1.out
'

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'drain some nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2-3 test_reason_2_3 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status1.out &&
	cat >drain_status1.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status1.exp drain_status1.out
'

test_expect_success 'resource eventlog has 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state2.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state2.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state2.out
'

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status2.out &&
	cat >drain_status2.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status2.exp drain_status2.out
'

# Default temporarily changed from 0 (no history) to 90d #7669
test_expect_failure 'resource eventlog has truncated events' '
	test $(has_resource_event drain | wc -l) -eq 0
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state3.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state3.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state3.out
'

#
# second set of tests check drain state correct if there is no
# checkpoint (i.e. first time using an updated resource module, or a
# checkpoint failed)
#

test_expect_success 'reset test state by clearing kvs resource info' '
	flux kvs unlink -f resource.eventlog &&
	flux kvs unlink -f checkpoint.resource &&
	flux module load resource noverify
'

test_expect_success 'drain some nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2-3 test_reason_2_3 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status3.out &&
	cat >drain_status3.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status3.exp drain_status3.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state4.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state4.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state4.out
'

test_expect_success 'clear checkpoint to emulate first time using resource module' '
	flux kvs unlink checkpoint.resource
'

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'drain status is identical to before the reload, no checkpoint is ok' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status4.out &&
	cat >drain_status4.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status4.exp drain_status4.out
'

test_expect_success 'resource eventlog still has 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state5.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state5.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state5.out
'

#
# third set of tests check basic history / eventlog truncation if history set to 0
#

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'invalid configuration of history results in error' '
	test_must_fail flux config load <<-EOF
	[resource]
	history = "foobar"
	EOF
'

test_expect_success 'configure truncation' '
	flux config load <<-EOF
	[resource]
	history = "0"
	EOF
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status5.out &&
	cat >drain_status5.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status5.exp drain_status5.out
'

test_expect_success 'resource eventlog truncated, drain events are now gone' '
	test $(has_resource_event drain | wc -l) -eq 0
'

#
# fourth set of tests check basic history / eventlog truncation if history set to inf
#

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink -f resource.eventlog &&
	flux kvs unlink -f checkpoint.resource &&
	flux module load resource noverify
'

test_expect_success 'drain some nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2-3 test_reason_2_3 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status6.out &&
	cat >drain_status6.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status6.exp drain_status6.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state6.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state6.out &&
	jq -e ".drain_state.\"2-3\".nodelist == \"fake[2-3]\"" < drain_state6.out
'

test_expect_success 'configure no truncation' '
	flux config load <<-EOF
	[resource]
	history = "inf"
	EOF
'

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status7.out &&
	cat >drain_status7.exp<<-EOT &&
	fake1,test_reason_1
	fake[2-3],test_reason_2_3
	EOT
	test_cmp drain_status7.exp drain_status7.out
'

test_expect_success 'resource eventlog history is preserved' '
	test $(has_resource_event drain | wc -l) -eq 2
'

#
# fifth set of tests check eventlog will replay events that are newer
# than the most recent drain state checkpoint
#

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink checkpoint.resource &&
	flux module load resource noverify
'

test_expect_success 'reconfigure truncation' '
	flux config load <<-EOF &&
	[resource]
	history = "0"
	EOF
	flux module reload resource noverify
'

test_expect_success 'drain a node' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status8.out &&
	cat >drain_status8.exp<<-EOT &&
	fake1,test_reason_1
	EOT
	test_cmp drain_status8.exp drain_status8.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state7.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state7.out
'

# to ensure timestamp is in future, pick timestamp way out
test_expect_success 'resource will replay events after checkpoint' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":4000000000.000000,"name":"drain","context":{"idset":"2","nodelist":"fake2","reason":"test_reason_2","overwrite":0}}
	EOT
	flux module load resource noverify
'

test_expect_success 'drain status now includes additional drain from resource.eventlog' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status9.out &&
	cat >drain_status9.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	EOT
	test_cmp drain_status9.exp drain_status9.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state8.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state8.out &&
	jq -e ".drain_state.\"2\".nodelist == \"fake2\"" < drain_state8.out
'

# to ensure timestamp is in future, pick timestamp way out
test_expect_success 'resource will replay events after checkpoint with bad rank' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":4000000001.000000,"name":"drain","context":{"idset":"0","nodelist":"fake3","reason":"test_reason_3","overwrite":0}}
	EOT
	flux module load resource noverify
'

test_expect_success 'drain status now includes additional drain from resource.eventlog' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status10.out &&
	cat >drain_status10.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	fake3,test_reason_3
	EOT
	test_cmp drain_status10.exp drain_status10.out
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state9.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state9.out &&
	jq -e ".drain_state.\"2\".nodelist == \"fake2\"" < drain_state9.out &&
	jq -e ".drain_state.\"3\".nodelist == \"fake3\"" < drain_state9.out
'

# to ensure timestamp is in future, pick timestamp way out
test_expect_success 'resource will ignore invalid nodes' '
	flux kvs put --raw resource.eventlog=- <<-EOT &&
	{"timestamp":4000000002.000000,"name":"drain","context":{"idset":"0","nodelist":"fake13","reason":"test_reason_13","overwrite":0}}
	EOT
	flux module load resource noverify
'

test_expect_success 'drain status stays the same despite fake node in eventlog' '
	test $(flux resource status -s drain -no {nnodes}) -eq 3 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status11.out &&
	cat >drain_status11.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	fake3,test_reason_3
	EOT
	test_cmp drain_status11.exp drain_status11.out
'

#
# sixth set of tests check that drain reason is preserved correctly
#

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink checkpoint.resource &&
	flux module load resource noverify
'

test_expect_success 'drain some nodes, atleast one without a reason' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status12.out &&
	cat >drain_status12.exp<<-EOT &&
	fake1,test_reason_1
	fake2,
	EOT
	test_cmp drain_status12.exp drain_status12.out
'

test_expect_success 'resource eventlog has 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'unload resource module' '
	flux module remove resource
'

test_expect_success 'resource module checkpointed current drain state' '
	flux kvs get checkpoint.resource > drain_state10.out &&
	jq -e ".drain_state.\"1\".nodelist == \"fake1\"" < drain_state10.out &&
	jq -e ".drain_state.\"2\".nodelist == \"fake2\"" < drain_state10.out
'

test_expect_success 'load resource module' '
	flux module load resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status13.out &&
	cat >drain_status13.exp<<-EOT &&
	fake1,test_reason_1
	fake2,
	EOT
	test_cmp drain_status13.exp drain_status13.out
'

test_expect_success 'resource eventlog has 0 drain events, eventlog truncated' '
	test $(has_resource_event drain | wc -l) -eq 0
'

test_expect_success 'update drain event messages, update of one requires force' '
	test_must_fail flux resource drain 1 test_reason_1_update &&
	flux resource drain --force 1 test_reason_1_update &&
	flux resource drain 2 test_reason_2_update
'

test_expect_success 'drain status is updated with the new reasons' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status14.out &&
	cat >drain_status14.exp<<-EOT &&
	fake1,test_reason_1_update
	fake2,test_reason_2_update
	EOT
	test_cmp drain_status14.exp drain_status14.out
'

test_expect_success 'resource eventlog has 2 drain events for the two updates' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status is identical to before the reload' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status15.out &&
	cat >drain_status15.exp<<-EOT &&
	fake1,test_reason_1_update
	fake2,test_reason_2_update
	EOT
	test_cmp drain_status15.exp drain_status15.out
'

test_expect_success 'resource eventlog has 0 drain events, eventlog truncated' '
	test $(has_resource_event drain | wc -l) -eq 0
'

#
# seventh set of tests check that history of eventlog is preserved if requested
#

test_expect_success 'configure history with non-zero value' '
	flux config load <<-EOF
	[resource]
	history = "100d"
	EOF
'

test_expect_success 'reset test and add long ago historical and recent drain data' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink checkpoint.resource &&
	now=$(date +%s) &&
	flux kvs eventlog append --timestamp=1000.0 resource.eventlog drain \
	     "{\"idset\":\"1\",\"nodelist\":\"fake1\",\"reason\":\"test_reason_1\",\"overwrite\":0}" &&
	flux kvs eventlog append --timestamp=${now} resource.eventlog drain \
	     "{\"idset\":\"2\",\"nodelist\":\"fake2\",\"reason\":\"test_reason_2\",\"overwrite\":0}" &&
	flux module load resource noverify
'

# N.B. note, we cleared the checkpoint of drain state above.  So the
# first time we reload the resource module, there was no checkpoint,
# therefore no drain events will be truncated

test_expect_success 'drain status has two drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status16.out &&
	cat >drain_status16.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	EOT
	test_cmp drain_status16.exp drain_status16.out
'

test_expect_success 'resource eventlog contains two drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status still has two drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status17.out &&
	cat >drain_status17.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	EOT
	test_cmp drain_status17.exp drain_status17.out
'

test_expect_success 'resource eventlog now contains only 1 drain event' '
	test $(has_resource_event drain | wc -l) -eq 1 &&
	flux kvs eventlog get resource.eventlog | grep drain | grep fake2
'

#
# eighth set of tests check resource.eventlog corner case ok
#

# N.B. this emulates resource.eventlog completely truncated and then
# broker is killed
test_expect_success 'reset test and set resource.eventlog to empty string' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink checkpoint.resource &&
	flux kvs put resource.eventlog= &&
	flux module load resource noverify
'

test_expect_success 'reload resource module for good measure' '
	flux module reload resource noverify
'

test_expect_success 'drain status has zero drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0
'

test_expect_success 'drain a node' '
	flux resource drain 1 test_reason_1 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status18.out &&
	cat >drain_status18.exp<<-EOT &&
	fake1,test_reason_1
	EOT
	test_cmp drain_status18.exp drain_status18.out
'

test_expect_success 'reload resource module' '
	flux module reload resource noverify
'

test_expect_success 'drain status has one drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 1 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status19.out &&
	cat >drain_status19.exp<<-EOT &&
	fake1,test_reason_1
	EOT
	test_cmp drain_status19.exp drain_status19.out
'

#
# ninth set of tests check that special notruncate option works
#

test_expect_success 'configure history with full truncation' '
	flux config load <<-EOF
	[resource]
	history = "0"
	EOF
'

test_expect_success 'reset test state by clearing kvs resource info' '
	flux module remove resource &&
	flux kvs unlink resource.eventlog &&
	flux kvs unlink checkpoint.resource &&
	flux module load resource noverify
'

test_expect_success 'drain some nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 0 &&
	flux resource drain 1 test_reason_1 &&
	flux resource drain 2 test_reason_2 &&
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status20.out &&
	cat >drain_status20.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	EOT
	test_cmp drain_status20.exp drain_status20.out
'

test_expect_success 'resource eventlog contains two drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_expect_success 'reload resource module without truncation' '
	flux module reload resource noverify notruncate
'

test_expect_success 'drain status still has two drained nodes' '
	test $(flux resource status -s drain -no {nnodes}) -eq 2 &&
	flux resource drain -no "sort:nodelist {nodelist},{reason}" > drain_status21.out &&
	cat >drain_status21.exp<<-EOT &&
	fake1,test_reason_1
	fake2,test_reason_2
	EOT
	test_cmp drain_status21.exp drain_status21.out
'

test_expect_success 'resource eventlog still contains 2 drain events' '
	test $(has_resource_event drain | wc -l) -eq 2
'

test_done
