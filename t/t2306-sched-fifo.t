#!/bin/sh

test_description='sched-fifo Python scheduler tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 job

# Custom resource set: 2 nodes, 2 cores each (4 single-core slots total)
flux R encode -r0-1 -c0-1 >R.test

# Asymmetric set for first-fit test: rank0 has 2 cores, rank1 has 1 core
(flux R encode -r0 -c0-1 && flux R encode -r1 -c0) | flux R append \
	>R.test.first_fit

# GPU resource set: 2 nodes, 2 cores + 1 GPU each
flux R encode -r0-1 -c0-1 -g0 >R.test.gpu

query_free="flux resource list --state=free -no {rlist}"

alloc_summary() { flux jobs -no {annotations.sched.resource_summary} "$1"; }

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


test_expect_success 'unload job-exec to prevent job execution' '
	flux module remove job-exec
'
test_expect_success 'reload job-ingest without validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'generate single-core jobspec' '
	flux run --dry-run hostname >basic.json
'
test_expect_success 'load sched-fifo with custom resource set' '
	flux module unload sched-simple &&
	flux resource reload R.test &&
	flux module load sched-fifo &&
	test_debug "echo result=\"$($query_free)\"" &&
	test "$($query_free)" = "rank[0-1]/core[0-1]"
'

#
# Argument validation
#
test_expect_success 'sched-fifo: invalid queue-depth is rejected' '
	flux module unload sched-fifo &&
	test_must_fail flux module load sched-fifo queue-depth=bogus &&
	flux module load sched-fifo
'
test_expect_success 'sched-fifo: unknown alloc-mode is rejected' '
	flux module unload sched-fifo &&
	test_must_fail flux module load sched-fifo alloc-mode=bogus &&
	flux module load sched-fifo
'

#
# Basic FIFO allocation (worst-fit is default)
#
test_expect_success 'sched-fifo: submit 5 single-core jobs (4 slots available)' '
	flux job submit basic.json >j1.id &&
	flux job submit basic.json >j2.id &&
	flux job submit basic.json >j3.id &&
	flux job submit basic.json >j4.id &&
	flux job submit basic.json >j5.id &&
	flux job wait-event --timeout=5 $(cat j4.id) alloc &&
	flux job wait-event --timeout=5 $(cat j5.id) submit
'
test_expect_success 'sched-fifo: worst-fit spreads across nodes' '
	alloc_summary $(cat j1.id) >summaries.out &&
	alloc_summary $(cat j2.id) >>summaries.out &&
	alloc_summary $(cat j3.id) >>summaries.out &&
	alloc_summary $(cat j4.id) >>summaries.out &&
	test $(grep -c "^rank0/" summaries.out) = 2 &&
	test $(grep -c "^rank1/" summaries.out) = 2
'

test_expect_success 'sched-fifo: submit second pending job' '
	flux job submit basic.json >j6.id
'

#
# Priority: boosting urgency of j6 should move it ahead of j5
#
test_expect_success 'sched-fifo: boost j6 urgency above j5' '
	flux job urgency $(cat j6.id) 20 &&
	flux job wait-event --timeout=5 $(cat j6.id) urgency
'
test_expect_success 'sched-fifo: higher-priority job runs when slot opens' '
	flux cancel $(cat j1.id) &&
	flux job wait-event --timeout=5 $(cat j1.id) free &&
	flux job wait-event --timeout=5 $(cat j6.id) alloc
'
test_expect_success 'sched-fifo: lower-priority job remains pending' '
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat j5.id) alloc
'

#
# Cancel a pending job
#
test_expect_success 'sched-fifo: cancel pending job gets exception' '
	flux cancel $(cat j5.id) &&
	flux job wait-event --timeout=5 $(cat j5.id) exception
'
test_expect_success 'sched-fifo: cancel remaining running jobs' '
	flux cancel $(cat j2.id) $(cat j3.id) $(cat j4.id) $(cat j6.id) &&
	flux job wait-event --timeout=5 $(cat j6.id) free &&
	test "$($query_free)" = "rank[0-1]/core[0-1]"
'

#
# GPU job denial: with default resource class (Rv1Pool) and no GPU resources
# in the custom R.test resource set, GPU jobs fail with an infeasibility error.
#
test_expect_success 'sched-fifo: GPU job is denied when no GPUs available' '
	jobid=$(flux run -n1 -g1 --dry-run hostname | flux job submit) &&
	flux job wait-event --timeout=5 $jobid exception &&
	flux job eventlog $jobid | grep -i "gpu"
'

#
# Infeasibility: request that can never be satisfied
#
test_expect_success 'sched-fifo: infeasible job is denied immediately' '
	jobid=$(flux submit -n1 -c100 hostname) &&
	flux job wait-event --timeout=5 $jobid exception
'

#
# GPU scheduling: with Rv1Pool (default), GPU jobs are allocated when GPUs are
# present.  With resource-class=Rv1RlistPool (supports_gpu=False), GPU jobs are denied
# even when GPU resources exist.
#
test_expect_success 'sched-fifo: reload with GPU resource set' '
	flux module remove sched-fifo &&
	flux resource reload R.test.gpu &&
	flux module load sched-fifo
'
test_expect_success 'sched-fifo: generate single-GPU jobspec' '
	flux run -n1 -g1 --dry-run hostname >gpu.json
'
test_expect_success 'sched-fifo: GPU job is scheduled by Rv1Pool' '
	flux job submit gpu.json >jg1.id &&
	flux job wait-event --timeout=5 $(cat jg1.id) alloc &&
	alloc_summary $(cat jg1.id) | grep -q "gpu" &&
	flux job info $(cat jg1.id) R | \
	    jq -e "[.execution.R_lite[].children | has(\"gpu\")] | any"
'
test_expect_success 'sched-fifo: cancel GPU job' '
	flux cancel $(cat jg1.id) &&
	flux job wait-event --timeout=5 $(cat jg1.id) free
'
test_expect_success 'sched-fifo: Rv1RlistPool denies GPU job even with GPU resources' '
	flux module reload sched-fifo resource-class=Rv1RlistPool &&
	flux job submit gpu.json >jg2.id &&
	flux job wait-event --timeout=5 $(cat jg2.id) exception &&
	flux job eventlog $(cat jg2.id) | grep -i "gpu"
'
test_expect_success 'sched-fifo: restore non-GPU resource set' '
	flux module remove sched-fifo &&
	flux resource reload R.test &&
	flux module load sched-fifo
'

#
# best-fit alloc-mode: packs jobs onto fewest nodes.
# alloc-mode is an Rv1RlistPool-specific feature; load with resource-class=Rv1RlistPool.
#
test_expect_success 'sched-fifo: reload in best-fit mode' '
	flux module reload sched-fifo resource-class=Rv1RlistPool alloc-mode=best-fit
'
test_expect_success 'sched-fifo: submit 4 jobs in best-fit mode' '
	flux job submit basic.json >j7.id &&
	flux job submit basic.json >j8.id &&
	flux job submit basic.json >j9.id &&
	flux job submit basic.json >j10.id &&
	flux job wait-event --timeout=5 $(cat j10.id) alloc
'
test_expect_success 'sched-fifo: best-fit packs onto one node first' '
	alloc_summary $(cat j7.id) >best-fit.out &&
	alloc_summary $(cat j8.id) >>best-fit.out &&
	alloc_summary $(cat j9.id) >>best-fit.out &&
	alloc_summary $(cat j10.id) >>best-fit.out &&
	cat <<-EOF >best-fit.expected &&
	rank0/core0
	rank0/core1
	rank1/core0
	rank1/core1
	EOF
	test_cmp best-fit.expected best-fit.out
'
test_expect_success 'sched-fifo: cancel best-fit jobs' '
	flux cancel $(cat j7.id) $(cat j8.id) $(cat j9.id) $(cat j10.id) &&
	flux job wait-event --timeout=5 $(cat j10.id) free
'

#
# first-fit alloc-mode: fills nodes in rank order
#
test_expect_success 'sched-fifo: reload with asymmetric resources in first-fit mode' '
	flux module remove sched-fifo &&
	flux resource reload R.test.first_fit &&
	flux module load sched-fifo resource-class=Rv1RlistPool alloc-mode=first-fit &&
	test_debug "echo result=\"$($query_free)\"" &&
	test "$($query_free)" = "rank0/core[0-1] rank1/core0"
'
test_expect_success 'sched-fifo: submit 3 jobs in first-fit mode' '
	flux job submit basic.json >j11.id &&
	flux job submit basic.json >j12.id &&
	flux job submit basic.json >j13.id &&
	flux job wait-event --timeout=5 $(cat j13.id) alloc
'
test_expect_success 'sched-fifo: first-fit fills nodes in rank order' '
	alloc_summary $(cat j11.id) >first-fit.out &&
	alloc_summary $(cat j12.id) >>first-fit.out &&
	alloc_summary $(cat j13.id) >>first-fit.out &&
	cat <<-EOF >first-fit.expected &&
	rank0/core0
	rank0/core1
	rank1/core0
	EOF
	test_cmp first-fit.expected first-fit.out
'

#
# Hello protocol: reload with running jobs
#
test_expect_success 'sched-fifo: reload with outstanding allocations' '
	flux module reload sched-fifo resource-class=Rv1RlistPool alloc-mode=first-fit &&
	test_debug "echo result=\"$($query_free)\"" &&
	test "$($query_free)" = ""
'
test_expect_success 'sched-fifo: cancel first-fit jobs' '
	flux cancel $(cat j11.id) $(cat j12.id) $(cat j13.id) &&
	flux job wait-event --timeout=5 $(cat j13.id) free
'

#
# resource_update: drain/undrain triggers rescheduling
#
test_expect_success 'sched-fifo: restore symmetric resource set' '
	flux module remove sched-fifo &&
	flux resource reload R.test &&
	flux module load sched-fifo
'

#
# forecast: t_estimate annotations via forward simulation
#
# R.test has 4 single-core slots (2 nodes × 2 cores).  jf_run fills all 4
# slots with a 3600s walltime.  Both pending jobs carry a time limit so that
# the simulation can chain: jf_head gets t_estimate ≈ now+3600 (when jf_run
# frees), and jf_second gets t_estimate ≈ now+7200 (after jf_head runs its
# 3600s).  Long time limits are used so that the simulation times are well in
# the future and never flattened to now by max(t, time.time()), which would
# make the two estimates equal on slow CI runners.
#
test_expect_success 'sched-fifo: forecast-period: invalid value is rejected' '
	flux module unload sched-fifo &&
	test_must_fail flux module load sched-fifo forecast-period=bogus &&
	test_must_fail flux module load sched-fifo forecast-period=0 &&
	test_must_fail flux module load sched-fifo forecast-period=-1 &&
	flux module load sched-fifo
'
test_expect_success 'sched-fifo: forecast: submit job filling all 4 slots' '
	flux submit -n4 --time-limit=3600s hostname >jf_run.id &&
	flux job wait-event --timeout=5 $(cat jf_run.id) alloc
'
test_expect_success 'sched-fifo: forecast: submit two blocked pending jobs' '
	flux submit -n4 --time-limit=3600s hostname >jf_head.id &&
	flux submit -n4 --time-limit=3600s hostname >jf_second.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jf_head.id) alloc
'
test_expect_success 'sched-fifo: forecast: head job gets t_estimate annotation' '
	wait_annotation $(cat jf_head.id) t_estimate "[0-9]"
'
test_expect_success 'sched-fifo: forecast: second job estimate is after head job estimate' '
	test_wait_until -i 100 "
		t_head=\$(flux jobs -no {annotations.sched.t_estimate} $(cat jf_head.id)) &&
		t_second=\$(flux jobs -no {annotations.sched.t_estimate} $(cat jf_second.id)) &&
		test -n \"\$t_head\" && test -n \"\$t_second\" &&
		test \"\${t_second%%.*}\" -gt \"\${t_head%%.*}\"
	"
'
test_expect_success 'sched-fifo: forecast: t_estimate cleared when head job runs' '
	flux cancel $(cat jf_run.id) &&
	flux job wait-event --timeout=5 $(cat jf_head.id) alloc &&
	test "$(flux jobs -no {annotations.sched.t_estimate} $(cat jf_head.id))" = ""
'
test_expect_success 'sched-fifo: forecast: cancel forecast test jobs' '
	flux cancel $(cat jf_head.id) $(cat jf_second.id) &&
	flux job wait-event --timeout=5 $(cat jf_head.id) free
'

#
# forecast: stale t_estimate cleared when a no-duration job becomes head
#
# jf_run2 fills all 4 slots.  jf_est gets a t_estimate via forecast().
# A higher-priority job with no time limit (jf_inf) is then submitted and
# boosted to head of queue.  Since jf_inf has unknown duration, forecast()
# cannot chain past it; jf_est's stale t_estimate must be cleared.
#
test_expect_success 'sched-fifo: forecast stale: fill all 4 slots' '
	flux submit -n4 --time-limit=300s hostname >jf_run2.id &&
	flux job wait-event --timeout=5 $(cat jf_run2.id) alloc
'
test_expect_success 'sched-fifo: forecast stale: submit pending job with time limit' '
	flux submit -n4 --time-limit=300s hostname >jf_est.id &&
	wait_annotation $(cat jf_est.id) t_estimate "[0-9]"
'
test_expect_success 'sched-fifo: forecast stale: submit higher-priority job with no time limit' '
	flux submit -n4 hostname >jf_inf.id &&
	flux job urgency $(cat jf_inf.id) 20 &&
	flux job wait-event --timeout=5 $(cat jf_inf.id) urgency
'
test_expect_success 'sched-fifo: forecast stale: stale t_estimate on jf_est is cleared' '
	wait_no_annotation $(cat jf_est.id) t_estimate
'
test_expect_success 'sched-fifo: forecast stale: cancel stale-annotation test jobs' '
	flux cancel $(cat jf_run2.id) $(cat jf_est.id) $(cat jf_inf.id) &&
	flux job wait-event --timeout=5 $(cat jf_run2.id) free
'

test_expect_success 'sched-fifo: drain rank 0' '
	flux resource drain 0
'
test_expect_success 'sched-fifo: fill rank 1 (2 cores)' '
	flux job submit basic.json >jd1.id &&
	flux job submit basic.json >jd2.id &&
	flux job wait-event --timeout=5 $(cat jd2.id) alloc &&
	alloc_summary $(cat jd1.id) | grep -q "^rank1/" &&
	alloc_summary $(cat jd2.id) | grep -q "^rank1/"
'
test_expect_success 'sched-fifo: job blocks when drained node is only option' '
	flux job submit basic.json >jd3.id &&
	test_expect_code 1 flux job wait-event --timeout=0.5 $(cat jd3.id) alloc
'
test_expect_success 'sched-fifo: undrain rank 0 unblocks pending job' '
	flux resource undrain 0 &&
	flux job wait-event --timeout=5 $(cat jd3.id) alloc &&
	alloc_summary $(cat jd3.id) | grep -q "^rank0/"
'
test_expect_success 'sched-fifo: cancel drain test jobs' '
	flux cancel $(cat jd1.id) $(cat jd2.id) $(cat jd3.id) &&
	flux job wait-event --timeout=5 $(cat jd3.id) free
'

test_expect_success 'sched-fifo: drain cleanup and queue drain' '
	flux module reload sched-fifo &&
	run_timeout 30 flux queue drain
'
test_expect_success 'reload sched-simple' '
	flux module remove sched-fifo &&
	flux module load sched-simple
'

test_done
