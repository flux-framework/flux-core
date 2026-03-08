#!/bin/sh

test_description='sched-backfill EASY backfill scheduler tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 job

# Custom resource set: 2 nodes, 2 cores each (4 single-core slots total)
flux R encode -r0-1 -c0-1 >R.test

# GPU resource set: 2 nodes, 2 cores + 1 GPU each
flux R encode -r0-1 -c0-1 -g0 >R.test.gpu

query_free="flux resource list --state=free -no {rlist}"

alloc_summary() { flux jobs -no {annotations.sched.resource_summary} "$1"; }
t_estimate()    { flux jobs -no {annotations.sched.t_estimate} "$1"; }

# Poll until annotation field matches expected value (scheduler annotations
# do not generate eventlog entries, so wait-event cannot be used)
wait_annotation() {
	local id="$1" field="$2" value="$3"
	test_wait_until -i 100 \
	    "flux jobs -no {annotations.sched.$field} $id | grep -q '$value'"
}

# Poll until annotation field is empty/cleared
wait_no_annotation() {
	local id="$1" field="$2"
	test_wait_until -i 100 "test \"\$(flux jobs -no {annotations.sched.$field} $id)\" = \"\""
}


# Poll until all resources are free
wait_free() {
	test_wait_until -i 300 \
	    "flux resource list --state=free -no {rlist} | grep -qx 'rank\[0-1\]/core\[0-1\]'"
}

test_expect_success 'unload job-exec to prevent job execution' '
	flux module remove job-exec
'
test_expect_success 'reload job-ingest without validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'generate single-core jobspec' '
	flux run --dry-run hostname >basic.json
'
test_expect_success 'load sched-backfill with custom resource set' '
	flux module unload sched-simple &&
	flux resource reload R.test &&
	flux module load sched-backfill &&
	test_debug "echo result=\"$($query_free)\"" &&
	test "$($query_free)" = "rank[0-1]/core[0-1]"
'

#
# Argument validation
#
test_expect_success 'sched-backfill: invalid queue-depth is rejected' '
	flux module unload sched-backfill &&
	test_must_fail flux module load sched-backfill queue-depth=bogus &&
	flux module load sched-backfill
'
test_expect_success 'sched-backfill: unknown alloc-mode is rejected' '
	flux module unload sched-backfill &&
	test_must_fail flux module load sched-backfill alloc-mode=bogus &&
	flux module load sched-backfill
'

#
# Basic FIFO allocation (worst-fit is default; no backfill opportunity
# since all jobs have the same resource requirements)
#
test_expect_success 'sched-backfill: submit 5 single-core jobs (4 slots available)' '
	flux job submit basic.json >j1.id &&
	flux job submit basic.json >j2.id &&
	flux job submit basic.json >j3.id &&
	flux job submit basic.json >j4.id &&
	flux job submit basic.json >j5.id &&
	flux job wait-event --timeout=5 $(cat j4.id) alloc &&
	flux job wait-event --timeout=5 $(cat j5.id) submit
'
test_expect_success 'sched-backfill: cancel basic FIFO jobs' '
	flux cancel $(cat j1.id) $(cat j2.id) $(cat j3.id) $(cat j4.id) \
	            $(cat j5.id) &&
	flux job wait-event --timeout=5 $(cat j4.id) free &&
	wait_free
'

#
# EASY backfill
#
# Resource layout: jlong holds 2 of 4 cores with a 3600s walltime, leaving
# 2 cores free.  jhead needs all 4 cores and is blocked; its estimated
# reservation time is approximately jlong_submit_time + 3600s.
# Long walltimes are used so the shadow time stays well in the future even
# on slow CI runners where test setup may take many seconds.
#
# Three backfill candidates are submitted while jhead waits:
#   jnodur   - 1 core, no duration:  cannot backfill (finish time unknown)
#   jshort   - 1 core, 60s:          can backfill (fits; 60 < 3600s shadow)
#   jtoolong - 1 core, 4000s:        cannot backfill (would end after shadow)
#
test_expect_success 'sched-backfill: submit 2-core job with 3600s walltime' '
	flux submit -n2 --time-limit=3600s hostname >jlong.id &&
	flux job wait-event --timeout=5 $(cat jlong.id) alloc
'
test_expect_success 'sched-backfill: head job needing all 4 cores is blocked' '
	flux submit -n4 hostname >jhead.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jhead.id) alloc
'
test_expect_success 'sched-backfill: head job gets t_estimate annotation' '
	wait_annotation $(cat jhead.id) t_estimate "[0-9]"
'
test_expect_success 'sched-backfill: job without duration does not backfill' '
	flux job submit basic.json >jnodur.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jnodur.id) alloc
'
test_expect_success 'sched-backfill: job with short duration backfills' '
	flux submit -n1 --time-limit=60s hostname >jshort.id &&
	flux job wait-event --timeout=5 $(cat jshort.id) alloc
'
test_expect_success 'sched-backfill: backfilled job has backfill annotation set to head jobid' '
	jhead=$(flux job id --to=f58 $(cat jhead.id)) &&
	wait_annotation $(cat jshort.id) backfill "$jhead"
'
test_expect_success 'sched-backfill: head job is still pending after backfill' '
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jhead.id) alloc
'
test_expect_success 'sched-backfill: job with duration exceeding shadow does not backfill' '
	flux submit -n1 --time-limit=4000s hostname >jtoolong.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jtoolong.id) alloc
'
test_expect_success 'sched-backfill: cancel backfill test jobs' '
	flux cancel $(cat jlong.id) $(cat jshort.id) \
	            $(cat jhead.id) $(cat jnodur.id) $(cat jtoolong.id) &&
	flux job wait-event --timeout=5 $(cat jlong.id) free &&
	wait_free
'

#
# Exclusive allocation: an exclusive job reserves the whole node even if
# it only requests 1 slot.  _shadow_time must account for the full node
# core count (alloc.count("core")), not just nslots*slot_size, or it will
# undercount freed cores and return None, preventing any backfill.
#
# Setup: jexcl runs exclusively on node 0 (reserves both cores) with a 3600s
# walltime, leaving node 1's 2 cores free.  jexcl_head needs all 4 cores
# and is blocked.  jexcl_short (1 core, 60s) should backfill on node 1.
#
test_expect_success 'sched-backfill: exclusive: submit exclusive 1-node job' '
	flux submit -N1 -n1 --exclusive --time-limit=3600s hostname >jexcl.id &&
	flux job wait-event --timeout=5 $(cat jexcl.id) alloc &&
	alloc_summary $(cat jexcl.id) | grep -q "^rank0/"
'
test_expect_success 'sched-backfill: exclusive: head job needing all 4 cores is blocked' '
	flux submit -n4 hostname >jexcl_head.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jexcl_head.id) alloc
'
test_expect_success 'sched-backfill: exclusive: short job backfills on the free node' '
	flux submit -n1 --time-limit=60s hostname >jexcl_short.id &&
	flux job wait-event --timeout=5 $(cat jexcl_short.id) alloc &&
	alloc_summary $(cat jexcl_short.id) | grep -q "^rank1/"
'
test_expect_success 'sched-backfill: exclusive: cancel exclusive test jobs' '
	flux cancel $(cat jexcl.id) $(cat jexcl_head.id) $(cat jexcl_short.id) &&
	flux job wait-event --timeout=5 $(cat jexcl.id) free &&
	wait_free
'

#
# Node-based backfill: the shadow-time estimate for a node-based head job
# (nnodes > 0) must count free *nodes*, not aggregate cores.
#
# With R.test (2 nodes × 2 cores each), jnb_long runs exclusively on node 0,
# consuming both of its cores.  jnb_head needs both nodes (-N2) and is blocked.
# At this point the 2 free cores on node 1 equal jnb_head's slot requirement
# (2 slots × 1 core), so the old aggregate-core path returned shadow=now and
# backfill was impossible.  The node-based path returns shadow=jnb_long's
# end time (~3600s out), allowing jnb_short (1 node, 60s) to backfill.
#
test_expect_success 'sched-backfill: node-based: exclusive job occupies rank 0' '
	flux submit -N1 -n1 --exclusive --time-limit=3600s hostname >jnb_long.id &&
	flux job wait-event --timeout=5 $(cat jnb_long.id) alloc &&
	alloc_summary $(cat jnb_long.id) | grep -q "^rank0/"
'
test_expect_success 'sched-backfill: node-based: head job needing both nodes is blocked' '
	flux submit -N2 -n2 hostname >jnb_head.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jnb_head.id) alloc
'
test_expect_success 'sched-backfill: node-based: head job gets t_estimate annotation' '
	wait_annotation $(cat jnb_head.id) t_estimate "[0-9]"
'
test_expect_success 'sched-backfill: node-based: short 1-node job backfills on rank 1' '
	flux submit -N1 -n1 --time-limit=60s hostname >jnb_short.id &&
	flux job wait-event --timeout=5 $(cat jnb_short.id) alloc &&
	alloc_summary $(cat jnb_short.id) | grep -q "^rank1/"
'
test_expect_success 'sched-backfill: node-based: head job still pending after backfill' '
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jnb_head.id) alloc
'
test_expect_success 'sched-backfill: node-based: cancel node-based backfill test jobs' '
	flux cancel $(cat jnb_long.id) $(cat jnb_head.id) $(cat jnb_short.id) &&
	flux job wait-event --timeout=5 $(cat jnb_long.id) free &&
	wait_free
'

#
# Priority: boosting urgency of a lower-priority pending job moves it to
# head of queue, so it gets the reservation and runs first
#
test_expect_success 'sched-backfill: submit 2-core holder job' '
	flux submit -n2 hostname >jp_hold.id &&
	flux job wait-event --timeout=5 $(cat jp_hold.id) alloc
'
test_expect_success 'sched-backfill: submit two 4-core pending jobs' '
	flux submit -n4 hostname >jp1.id &&
	flux submit -n4 hostname >jp2.id &&
	flux job wait-event --timeout=5 $(cat jp1.id) submit &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jp1.id) alloc
'
test_expect_success 'sched-backfill: boost jp2 urgency above jp1' '
	flux job urgency $(cat jp2.id) 20 &&
	flux job wait-event --timeout=5 $(cat jp2.id) urgency
'
test_expect_success 'sched-backfill: higher-priority job runs when slot opens' '
	flux cancel $(cat jp_hold.id) &&
	flux job wait-event --timeout=5 $(cat jp_hold.id) free &&
	flux job wait-event --timeout=5 $(cat jp2.id) alloc
'
test_expect_success 'sched-backfill: lower-priority job remains pending' '
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jp1.id) alloc
'
test_expect_success 'sched-backfill: cancel priority test jobs' '
	flux cancel $(cat jp1.id) $(cat jp2.id) &&
	flux job wait-event --timeout=5 $(cat jp2.id) free &&
	wait_free
'

#
# Stale t_estimate: when a higher-priority job displaces the head, the old
# head's t_estimate annotation must be cleared.
#
# jst_hold occupies 2 of 4 cores.  jst_head needs all 4 and is blocked,
# receiving a t_estimate reservation.  A new higher-priority job (jst_new)
# is then submitted and boosted to head; schedule() must clear the stale
# t_estimate on jst_head.
#
test_expect_success 'sched-backfill: stale t_estimate: hold 2 cores' '
	flux submit -n2 --time-limit=3600s hostname >jst_hold.id &&
	flux job wait-event --timeout=5 $(cat jst_hold.id) alloc
'
test_expect_success 'sched-backfill: stale t_estimate: head job blocked and annotated' '
	flux submit -n4 hostname >jst_head.id &&
	wait_annotation $(cat jst_head.id) t_estimate "[0-9]"
'
test_expect_success 'sched-backfill: stale t_estimate: higher-priority job displaces head' '
	flux submit -n4 hostname >jst_new.id &&
	flux job urgency $(cat jst_new.id) 20 &&
	flux job wait-event --timeout=5 $(cat jst_new.id) urgency
'
test_expect_success 'sched-backfill: stale t_estimate: old head t_estimate is cleared' '
	wait_no_annotation $(cat jst_head.id) t_estimate
'
test_expect_success 'sched-backfill: stale t_estimate: cancel stale-annotation test jobs' '
	flux cancel $(cat jst_hold.id) $(cat jst_head.id) $(cat jst_new.id) &&
	flux job wait-event --timeout=5 $(cat jst_hold.id) free &&
	wait_free
'

#
# Cancel a pending job
#
test_expect_success 'sched-backfill: cancel pending job gets exception' '
	flux submit -n4 hostname >jc_run.id &&
	flux submit -n4 hostname >jc_pend.id &&
	flux job wait-event --timeout=5 $(cat jc_run.id) alloc &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jc_pend.id) alloc &&
	flux cancel $(cat jc_pend.id) &&
	flux job wait-event --timeout=5 $(cat jc_pend.id) exception &&
	flux cancel $(cat jc_run.id) &&
	flux job wait-event --timeout=5 $(cat jc_run.id) free
'

#
# GPU job with no GPUs in resource set: structurally infeasible, denied
# immediately with an error mentioning GPUs.
#
test_expect_success 'sched-backfill: GPU job is denied when no GPUs available' '
	jobid=$(flux run -n1 -g1 --dry-run hostname | flux job submit) &&
	flux job wait-event --timeout=5 $jobid exception &&
	flux job eventlog $jobid | grep -i "gpu"
'

#
# Infeasibility: request that can never be satisfied
#
test_expect_success 'sched-backfill: infeasible job is denied immediately' '
	jobid=$(flux submit -n1 -c100 hostname) &&
	flux job wait-event --timeout=5 $jobid exception
'

#
# GPU scheduling: with GPU resources present, GPU jobs are scheduled and the
# allocated R contains GPU children.
#
test_expect_success 'sched-backfill: reload with GPU resource set' '
	flux module remove sched-backfill &&
	flux resource reload R.test.gpu &&
	flux module load sched-backfill
'
test_expect_success 'sched-backfill: generate single-GPU jobspec' '
	flux run -n1 -g1 --dry-run hostname >gpu.json
'
test_expect_success 'sched-backfill: GPU job is scheduled by Rv1Pool' '
	flux job submit gpu.json >jg1.id &&
	flux job wait-event --timeout=5 $(cat jg1.id) alloc &&
	alloc_summary $(cat jg1.id) | grep -q "gpu" &&
	flux job info $(cat jg1.id) R | \
	    jq -e "[.execution.R_lite[].children | has(\"gpu\")] | any"
'
test_expect_success 'sched-backfill: cancel basic GPU job' '
	flux cancel $(cat jg1.id) &&
	flux job wait-event --timeout=5 $(cat jg1.id) free
'

#
# GPU backfill: shadow-time computation uses aggregate GPU counts (slot-based
# path) for requests with nnodes==0.
#
# With R.test.gpu (2 nodes × 2 cores + 1 GPU each), jgpu_long holds 1 GPU on
# one node with a 3600s walltime, leaving 1 GPU free on the other node.
# jgpu_head needs 2 GPUs (both nodes) and is blocked; shadow ~ now+3600s.
# jgpu_short (1 GPU, 60s) can backfill — it fits and 60 < 3600.
# jgpu_nodur (1 GPU, no duration) cannot backfill — finish time unknown.
#
test_expect_success 'sched-backfill: GPU backfill: submit 1-GPU job with 3600s walltime' '
	flux submit -n1 -g1 --time-limit=3600s hostname >jgpu_long.id &&
	flux job wait-event --timeout=5 $(cat jgpu_long.id) alloc &&
	alloc_summary $(cat jgpu_long.id) | grep -q "gpu"
'
test_expect_success 'sched-backfill: GPU backfill: head job needing 2 GPUs is blocked' '
	flux submit -n2 -g1 hostname >jgpu_head.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jgpu_head.id) alloc
'
test_expect_success 'sched-backfill: GPU backfill: head job gets t_estimate annotation' '
	wait_annotation $(cat jgpu_head.id) t_estimate "[0-9]"
'
test_expect_success 'sched-backfill: GPU backfill: job without duration cannot backfill' '
	flux submit -n1 -g1 hostname >jgpu_nodur.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jgpu_nodur.id) alloc
'
test_expect_success 'sched-backfill: GPU backfill: short GPU job backfills' '
	flux submit -n1 -g1 --time-limit=60s hostname >jgpu_short.id &&
	flux job wait-event --timeout=5 $(cat jgpu_short.id) alloc &&
	alloc_summary $(cat jgpu_short.id) | grep -q "gpu"
'
test_expect_success 'sched-backfill: GPU backfill: head job still pending after backfill' '
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jgpu_head.id) alloc
'
test_expect_success 'sched-backfill: GPU backfill: cancel GPU backfill test jobs' '
	flux cancel $(cat jgpu_long.id) $(cat jgpu_head.id) \
	            $(cat jgpu_short.id) $(cat jgpu_nodur.id) &&
	flux job wait-event --timeout=5 $(cat jgpu_long.id) free
'

test_expect_success 'sched-backfill: restore non-GPU resource set' '
	flux module remove sched-backfill &&
	flux resource reload R.test &&
	flux module load sched-backfill
'

#
# Hello protocol: reload with running jobs
#
test_expect_success 'sched-backfill: reload with outstanding allocations' '
	flux job submit basic.json >jh1.id &&
	flux job submit basic.json >jh2.id &&
	flux job submit basic.json >jh3.id &&
	flux job submit basic.json >jh4.id &&
	flux job wait-event --timeout=5 $(cat jh4.id) alloc &&
	flux module reload sched-backfill &&
	test_debug "echo result=\"$($query_free)\"" &&
	test "$($query_free)" = ""
'
test_expect_success 'sched-backfill: cancel hello test jobs' '
	flux cancel $(cat jh1.id) $(cat jh2.id) $(cat jh3.id) $(cat jh4.id) &&
	flux job wait-event --timeout=5 $(cat jh4.id) free &&
	wait_free
'

#
# resource_update: drain/undrain triggers rescheduling
#
test_expect_success 'sched-backfill: drain rank 0' '
	flux resource drain 0
'
test_expect_success 'sched-backfill: fill rank 1 (2 cores)' '
	flux job submit basic.json >jd1.id &&
	flux job submit basic.json >jd2.id &&
	flux job wait-event --timeout=5 $(cat jd2.id) alloc &&
	alloc_summary $(cat jd1.id) | grep -q "^rank1/" &&
	alloc_summary $(cat jd2.id) | grep -q "^rank1/"
'
test_expect_success 'sched-backfill: job blocks when drained node is only option' '
	flux job submit basic.json >jd3.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jd3.id) alloc
'
test_expect_success 'sched-backfill: undrain rank 0 unblocks pending job' '
	flux resource undrain 0 &&
	flux job wait-event --timeout=5 $(cat jd3.id) alloc &&
	alloc_summary $(cat jd3.id) | grep -q "^rank0/"
'
test_expect_success 'sched-backfill: cancel drain test jobs' '
	flux cancel $(cat jd1.id) $(cat jd2.id) $(cat jd3.id) &&
	flux job wait-event --timeout=5 $(cat jd3.id) free
'

test_expect_success 'sched-backfill: drain cleanup and queue drain' '
	flux module reload sched-backfill &&
	run_timeout 30 flux queue drain
'
test_expect_success 'reload sched-simple' '
	flux module remove sched-backfill &&
	flux module load sched-simple
'

test_done
