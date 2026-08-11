#!/bin/sh
test_description='Test RFC 33 virtual queues (vqueues)'

. $(dirname $0)/sharness.sh

test_under_flux 4 full -Slog-stderr-level=1

test_expect_success 'config queues, resources, and a vqueue' '
	flux R encode -r 0-3 -p batch:0-2 -p debug:3 \
	   | flux kvs put -r resource.R=- &&
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]

	[queues.expedite]
	parent = "batch"
	policy.jobspec.defaults.system.duration = "5m"

	[queues.debug]
	requires = [ "debug" ]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	flux queue start --all &&
	flux module unload sched-simple &&
	flux module reload resource &&
	flux module load sched-simple &&
	flux queue list &&
	flux resource list -o rlist
'

test_expect_success 'flux queue list shows PARENT column when a vqueue is configured' '
	flux queue list >list.out &&
	head -1 list.out | grep PARENT &&
	test "$(flux queue list -q expedite -no "{parent}")" = "batch" &&
	test "$(flux queue list -q batch -no "{parent}")" = "" &&
	test "$(flux queue list -q debug -no "{parent}")" = ""
'

test_expect_success 'flux resource list does not show vqueue in QUEUE column' '
	flux resource list >rlist-noqueue.out &&
	test_must_fail grep expedite rlist-noqueue.out
'

test_expect_success 'flux resource list -q vqueue selects the parent ranks' '
	flux resource list -no "{ranks}" -q expedite >vq-ranks.out &&
	flux resource list -no "{ranks}" -q batch >pq-ranks.out &&
	test_cmp pq-ranks.out vq-ranks.out
'

test_expect_success 'flux resource list -q vqueue shows vqueue in QUEUE column' '
	test "$(flux resource list -s up -no "{queue}" -q expedite)" = "expedite"
'

test_expect_success 'invalid config: vqueue parent missing is rejected' '
	test_must_fail flux config load 2>orphan.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.orphan]
	parent = "nosuchqueue"
	EOT
	grep "parent queue .nosuchqueue. is not configured" orphan.err
'

test_expect_success 'invalid config: vqueue parent is the queue itself' '
	test_must_fail flux config load 2>selfparent.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.selfq]
	parent = "selfq"
	EOT
	grep "parent queue is itself" selfparent.err
'

test_expect_success 'invalid config: vqueue parent is itself virtual' '
	test_must_fail flux config load 2>subexpedite.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.expedite]
	parent = "batch"
	[queues.subexpedite]
	parent = "expedite"
	EOT
	grep "parent queue .expedite. is itself a virtual queue" subexpedite.err
'

test_expect_success 'invalid config: vqueue with requires is rejected' '
	test_must_fail flux config load 2>requires.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.expedite]
	parent = "batch"
	requires = [ "expedite" ]
	EOT
	grep "a virtual queue must not set .requires." requires.err
'

test_expect_success 'invalid config: vqueue with policy.scheduler is rejected' '
	test_must_fail flux config load 2>scheduler.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.expedite]
	parent = "batch"
	policy.scheduler.foo = 1
	EOT
	grep "a virtual queue must not set .policy.scheduler." scheduler.err
'

test_expect_success 'invalid config: vqueue parent is not a string is rejected' '
	test_must_fail flux config load 2>parenttype.err <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.expedite]
	parent = 42
	EOT
	grep -i "must be a string" parenttype.err
'

test_expect_success 'invalid vqueue config fails the instance at startup' '
	cat >startup-badvq.toml <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.orphan]
	parent = "nosuchqueue"
	EOT
	test_must_fail flux start --config-path=startup-badvq.toml \
	  true 2>startup-badvq.err &&
	grep "parent queue .nosuchqueue. is not configured" startup-badvq.err
'

test_expect_success 'valid config: vqueue as default queue is accepted' '
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	[queues.expedite]
	parent = "batch"
	[policy.jobspec.defaults.system]
	queue = "expedite"
	EOT
	flux config load <<-EOT
	[queues.batch]
	requires = [ "batch" ]

	[queues.expedite]
	parent = "batch"
	policy.jobspec.defaults.system.duration = "5m"

	[queues.debug]
	requires = [ "debug" ]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
'

test_expect_success 'submit to vqueue succeeds and job keeps vqueue name' '
	jobid=$(flux submit -q expedite --urgency=hold hostname) &&
	echo $jobid >expedite.jobid &&
	test "$(flux jobs -no {queue} $jobid)" = "expedite"
'

test_expect_success 'vqueue job jobspec carries parent property constraint' '
	jobid=$(cat expedite.jobid) &&
	flux job info $jobid jobspec | jq -e \
	  ".attributes.system.constraints.properties == [\"batch\"]"
'

test_expect_success 'cleanup held vqueue job' '
	flux cancel $(cat expedite.jobid) &&
	flux job wait-event $(cat expedite.jobid) clean
'

test_expect_success 'AND rule: parent stopped + vqueue started holds the job' '
	flux queue stop batch &&
	flux queue start expedite &&
	jobid=$(flux submit -q expedite --wait-event=priority hostname) &&
	echo $jobid >and-rule.jobid &&
	flux queue status -v >and-rule.out &&
	grep "^0 alloc requests queued" and-rule.out &&
	grep "^0 alloc requests pending to scheduler" and-rule.out
'

test_expect_success 'starting the parent releases the vqueue job' '
	jobid=$(cat and-rule.jobid) &&
	flux queue start batch &&
	flux job wait-event -t 20 $jobid clean
'

test_expect_success 'vqueue holds new jobs while parent jobs still run' '
	flux queue start --all &&
	flux queue stop expedite &&
	vqid=$(flux submit -q expedite --wait-event=priority hostname) &&
	pqid=$(flux submit -q batch hostname) &&
	echo $vqid >held.jobid &&
	flux job wait-event -t 20 $pqid clean &&
	flux queue status -v >held.out &&
	grep "^0 alloc requests queued" held.out &&
	grep "^0 alloc requests pending to scheduler" held.out
'

test_expect_success 'starting the vqueue releases its held job' '
	flux queue start expedite &&
	flux job wait-event -t 20 $(cat held.jobid) clean
'

test_expect_success 'flux queue status shows expected output for the vqueue' '
	flux queue start --all &&
	flux queue status expedite >vqstatus.out &&
	grep "submission is enabled" vqstatus.out &&
	grep "Scheduling is started" vqstatus.out
'

test_expect_success 'flux queue status shows vqueue blocked by stopped parent' '
	flux queue stop batch &&
	flux queue start expedite &&
	flux queue status expedite >vqstatus2.out &&
	grep "Scheduling is stopped: parent queue .batch. is stopped" \
	  vqstatus2.out &&
	flux queue start --all
'

test_expect_success 'flux queue list shows three-state SCHED/ST for a blocked vqueue' '
	flux queue stop batch &&
	flux queue start expedite &&
	test "$(flux queue list -q expedite -no "{scheduling}")" \
	  = "stopped (parent)" &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "p" &&
	test "$(flux queue list -q batch -no "{scheduling}")" = "stopped" &&
	test "$(flux queue list -q batch -no "{started.ascii}")" = "n" &&
	flux queue start --all &&
	test "$(flux queue list -q expedite -no "{scheduling}")" = "started" &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "y"
'

test_expect_success 'flux queue list renders the paused glyph in yellow' '
	flux queue stop batch &&
	flux queue start expedite &&
	test "$(flux queue list -q expedite -no "{color_started}")" \
	  = "$(printf "\033[01;33m")" &&
	test "$(flux queue list -q batch -no "{color_started}")" \
	  = "$(printf "\033[01;31m")" &&
	flux queue start --all &&
	test "$(flux queue list -q expedite -no "{color_started}")" \
	  = "$(printf "\033[01;32m")"
'

test_expect_success 'scheduler offline renders vqueue as stopped, not paused' '
	flux queue start --all &&
	flux module remove sched-simple &&
	test_when_finished "flux module load sched-simple && flux queue start --all" &&
	test "$(flux queue list -q expedite -no "{scheduling}")" = "stopped" &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "n" &&
	test "$(flux queue list -q expedite -no "{color_started}")" \
	  = "$(printf "\033[01;31m")"
'

test_expect_success 'flux job update into vqueue picks up parent constraint' '
	jobid=$(flux submit --urgency=hold -q debug hostname) &&
	flux job info $jobid jobspec | jq -e \
	  ".attributes.system.constraints.properties == [\"debug\"]" &&
	flux update --wait $jobid queue=expedite &&
	flux job info $jobid jobspec | jq -e \
	  ".attributes.system.constraints.properties == [\"batch\"]" &&
	test "$(flux jobs -no {queue} $jobid)" = "expedite" &&
	flux cancel $jobid &&
	flux job wait-event $jobid clean
'

test_expect_success 'configure parent duration and job-size limits' '
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	policy.limits.duration = "1h"
	policy.limits.job-size.max.nnodes = 2

	[queues.expedite]
	parent = "batch"
	policy.limits.duration = "30m"

	[queues.debug]
	requires = [ "debug" ]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	flux queue start --all
'

test_expect_success 'vqueue duration override rejects jobs over its own limit' '
	test_must_fail flux submit -q expedite -t 45m hostname \
	  2>expedite-duration.err &&
	grep "duration (45m) exceeds.*limit of 30m for queue expedite" \
	  expedite-duration.err
'

test_expect_success 'vqueue inherits parent job-size limit' '
	test_must_fail flux submit -q expedite -N 3 hostname \
	  2>expedite-jobsize.err &&
	grep "exceeds policy limit of 2 for queue expedite" \
	  expedite-jobsize.err
'

test_expect_success 'vqueue job within both inherited and own limits passes' '
	flux submit -q expedite -N 2 -t 20m hostname
'

test_expect_success 'parent queue job still honors parent duration limit' '
	test_must_fail flux submit -q batch -t 2h hostname \
	  2>batch-duration.err &&
	grep "duration (2h) exceeds.*limit of 1h for queue batch" \
	  batch-duration.err
'

test_expect_success 'parent queue job still honors parent job-size limit' '
	test_must_fail flux submit -q batch -N 3 hostname \
	  2>batch-jobsize.err &&
	grep "exceeds policy limit of 2 for queue batch" \
	  batch-jobsize.err
'

test_expect_success 'config reload updating parent limit is picked up' '
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]
	policy.limits.duration = "1h"
	policy.limits.job-size.max.nnodes = 1

	[queues.expedite]
	parent = "batch"
	policy.limits.duration = "30m"

	[queues.debug]
	requires = [ "debug" ]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	test_must_fail flux submit -q expedite -N 2 hostname \
	  2>expedite-reload.err &&
	grep "exceeds policy limit of 1 for queue expedite" \
	  expedite-reload.err
'

test_expect_success 'job-list: submit jobs to parent and vqueue for listing tests' '
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]

	[queues.expedite]
	parent = "batch"

	[queues.debug]
	requires = [ "debug" ]

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	flux queue start --all &&
	flux bulksubmit -q {} --urgency=hold hostname \
	  ::: batch expedite >joblist-parent.ids &&
	sed -n 1p joblist-parent.ids >joblist-parent.jobid &&
	sed -n 2p joblist-parent.ids >joblist-vqueue.jobid
'

test_expect_success 'job-list: flux jobs -q parent includes vqueue jobs' '
	flux jobs -no {id} -q batch | sort >joblist-parent.out &&
	sort joblist-parent.ids >joblist-parent.exp &&
	test_cmp joblist-parent.exp joblist-parent.out
'

test_expect_success 'job-list: flux jobs -q vqueue lists only vqueue job' '
	flux jobs -no {id} -q expedite >joblist-vqueue.out &&
	test_cmp joblist-vqueue.jobid joblist-vqueue.out
'

test_expect_success 'job-list: RPC queue constraint on parent includes vqueue job' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {states:[\"active\"]}, \
	  {queue:[\"batch\"]}] }" &&
	jq -j -c -n "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | ${FLUX_BUILD_DIR}/t/request/rpc_stream job-list.list \
	  | jq .jobs[].id | flux job id -t f58 | sort >joblist-rpc-parent.out &&
	sort joblist-parent.ids >joblist-rpc-parent.exp &&
	test_cmp joblist-rpc-parent.exp joblist-rpc-parent.out
'

test_expect_success 'job-list: RPC queue constraint on vqueue is exact' '
	id=$(id -u) &&
	constraint="{ and: [ {userid:[${id}]}, {states:[\"active\"]}, \
	  {queue:[\"expedite\"]}] }" &&
	jq -j -c -n "{max_entries:1000, attrs:[], constraint:${constraint}}" \
	  | ${FLUX_BUILD_DIR}/t/request/rpc_stream job-list.list \
	  | jq .jobs[].id | flux job id -t f58 >joblist-rpc-vqueue.out &&
	test_cmp joblist-vqueue.jobid joblist-rpc-vqueue.out
'

test_expect_success 'cleanup vqueue job listing jobs' '
	flux cancel $(cat joblist-parent.jobid) &&
	flux cancel $(cat joblist-vqueue.jobid) &&
	flux job wait-event $(cat joblist-parent.jobid) clean &&
	flux job wait-event $(cat joblist-vqueue.jobid) clean
'

test_expect_success 'configure two virtual queues sharing a parent' '
	flux config load <<-EOT &&
	[queues.batch]
	requires = [ "batch" ]

	[queues.expedite]
	parent = "batch"

	[queues.standby]
	parent = "batch"

	[policy.jobspec.defaults.system]
	queue = "batch"
	EOT
	flux queue enable --all &&
	flux queue start --all
'

test_expect_success 'stopping one vqueue leaves its sibling and parent started' '
	flux queue stop expedite &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "n" &&
	test "$(flux queue list -q standby -no "{started.ascii}")" = "y" &&
	test "$(flux queue list -q batch -no "{started.ascii}")" = "y"
'

test_expect_success 'starting one vqueue leaves its sibling stopped' '
	flux queue stop --all &&
	flux queue start expedite &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "p" &&
	test "$(flux queue list -q standby -no "{started.ascii}")" = "n" &&
	test "$(flux queue list -q batch -no "{started.ascii}")" = "n"
'

test_expect_success 'starting the parent releases only the started sibling' '
	flux queue start batch &&
	test "$(flux queue list -q expedite -no "{started.ascii}")" = "y" &&
	test "$(flux queue list -q standby -no "{started.ascii}")" = "n" &&
	test "$(flux queue list -q batch -no "{started.ascii}")" = "y"
'

test_expect_success 'disabling one vqueue leaves its sibling and parent enabled' '
	flux queue enable --all &&
	flux queue disable -m test expedite &&
	test "$(flux queue list -q expedite -no "{enabled.ascii}")" = "n" &&
	test "$(flux queue list -q standby -no "{enabled.ascii}")" = "y" &&
	test "$(flux queue list -q batch -no "{enabled.ascii}")" = "y" &&
	flux queue enable --all
'

test_done
