#!/bin/sh

test_description='simple sched-simple tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 job

query="flux resource list --state=free -no {rlist}"

flux R encode -r0-1 -c0-1 >R.test

(flux R encode -r0 -c0-1 && flux R encode -r1 -c0) | flux R append \
	>R.test.first_fit


dmesg_grep=${SHARNESS_TEST_SRCDIR}/scripts/dmesg-grep.py

kvs_job_dir() {
	flux job id --to=kvs $1
}
list_R() {
	for id in "$@"; do
		flux job eventlog $id | sed -n 's/.*alloc //gp'
	done
}

test_expect_success 'unload job-exec module to prevent job execution' '
	flux module remove job-exec
'
test_expect_success 'reload ingest without validator' '
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'sched-simple: generate jobspec for simple test job' '
	flux run --dry-run hostname >basic.json
'
test_expect_success 'sched-simple cannot be loaded again under a new name' '
	test_must_fail flux module load --name=newsched sched-simple
'
test_expect_success 'job-manager: load sched-simple w/ an illegal mode' '
	flux module unload sched-simple &&
	flux module load sched-simple mode=foobar
'
test_expect_success 'job-manager: load sched-simple w/ an illegal limited range' '
	flux module unload sched-simple &&
	flux module load sched-simple mode=limited=-1
'
test_expect_success 'sched-simple: reload sched-simple with default resource.R' '
	flux module unload sched-simple &&
	flux resource reload R.test &&
	flux module load sched-simple &&
	test_debug "echo result=\"$($query)\"" &&
	test "$($query)" = "rank[0-1]/core[0-1]"
'
test_expect_success 'sched-simple: unsatisfiable request is canceled' '
	flux submit -n1 -c 3 hostname >job0.id &&
	job0id=$(cat job0.id) &&
	flux job wait-event --timeout=5.0 $job0id exception &&
	flux job eventlog $job0id | grep "unsatisfiable request"
'
test_expect_success 'sched-simple: gpu request is canceled' '
	jobid=$(flux run -n1 -g1 --dry-run hostname | flux job submit) &&
	flux job wait-event --timeout=5.0 $jobid exception &&
	flux job eventlog $jobid \
		| grep  "sched-simple does not support resource type .gpu."
'
Y2J="flux python ${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py"
SPEC=${SHARNESS_TEST_SRCDIR}/jobspec/valid/basic.yaml
test_expect_success 'sched-simple: invalid minimal jobspec is canceled' '
	${Y2J}<${SPEC} | jq ".version = 1" | flux job submit >job00.id &&
	jobid=$(cat job00.id) &&
	flux job wait-event --timeout=5.0 $jobid exception &&
	flux job eventlog $jobid | grep "getting duration: Object item not found: system"
'
test_expect_success 'sched-simple: submit 5 jobs' '
	flux job submit basic.json >job1.id &&
	flux job submit basic.json >job2.id &&
	flux job submit basic.json >job3.id &&
	flux job submit basic.json >job4.id &&
	flux job submit basic.json >job5.id &&
	flux job wait-event --timeout=5.0 $(cat job4.id) alloc &&
	flux job wait-event --timeout=5.0 $(cat job5.id) submit
'
test_expect_success 'sched-simple: check allocations for running jobs' '
	list_R $(cat job1.id job2.id job3.id job4.id) > allocs.out &&
	cat <<-EOF >allocs.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core1"}}
	EOF
	test_cmp allocs.expected allocs.out
'
test_expect_success 'sched-simple: no remaining resources' '
	test "$($query)" = ""
'
test_expect_success 'sched-simple: cancel one job' '
	flux cancel $(cat job3.id) &&
	flux job wait-event --timeout=5.0 $(cat job3.id) exception &&
	flux job wait-event --timeout=5.0 $(cat job3.id) free
'
test_expect_success 'sched-simple: waiting job now has alloc event' '
	flux job wait-event --timeout=5.0 $(cat job5.id) alloc &&
	test "$(list_R $(cat job5.id))" = "annotations={\"sched\":{\"resource_summary\":\"rank0/core1\"}}"
'
test_expect_success 'sched-simple: cancel all jobs' '
	flux cancel $(cat job5.id) &&
	flux cancel $(cat job4.id) &&
	flux cancel $(cat job2.id) &&
	flux cancel $(cat job1.id) &&
	flux job wait-event --timeout=5.0 $(cat job1.id) exception &&
	flux job wait-event --timeout=5.0 $(cat job1.id) free &&
	test "$($query)" = "rank[0-1]/core[0-1]"
'
test_expect_success 'sched-simple: reload in best-fit alloc-mode' '
	flux module reload sched-simple alloc-mode=best-fit
'
test_expect_success 'sched-simple: submit 5 more jobs' '
	flux job submit basic.json >job6.id &&
	flux job submit basic.json >job7.id &&
	flux job submit basic.json >job8.id &&
	flux job submit basic.json >job9.id &&
	flux job submit basic.json >job10.id &&
	flux job wait-event --timeout=5.0 $(cat job9.id) alloc &&
	flux job wait-event --timeout=5.0 $(cat job10.id) submit
'
test_expect_success 'sched-simple: check allocations for running jobs' '
	list_R $(cat job6.id job7.id job8.id job9.id) > best-fit-allocs.out &&
	cat <<-EOF >best-fit-allocs.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	annotations={"sched":{"resource_summary":"rank1/core1"}}
	EOF
	test_cmp best-fit-allocs.expected best-fit-allocs.out
'
test_expect_success 'sched-simple: cancel pending & running job' '
	id=$(cat job10.id) &&
	flux cancel $id &&
	flux job wait-event --timeout=5.0 $id exception &&
	flux cancel $(cat job6.id) &&
	test_expect_code 1 flux kvs get $(kvs_job_dir $id).R
'
test_expect_success 'sched-simple: cancel remaining jobs' '
	flux cancel $(cat job7.id) &&
	flux cancel $(cat job8.id) &&
	flux cancel $(cat job9.id) &&
	flux job wait-event --timeout=5.0 $(cat job9.id) free
'
test_expect_success 'sched-simple: reload in first-fit alloc-mode' '
	flux module remove sched-simple &&
	flux resource reload R.test.first_fit &&
	flux module load sched-simple alloc-mode=first-fit &&
	test_debug "echo result=\"$($query)\"" &&
	test "$($query)" = "rank0/core[0-1] rank1/core0"
'
test_expect_success 'sched-simple: submit 3 more jobs' '
	flux job submit basic.json >job11.id &&
	flux job submit basic.json >job12.id &&
	flux job submit basic.json >job13.id &&
	flux job wait-event --timeout=5.0 $(cat job13.id) alloc
'
test_expect_success 'sched-simple: check allocations for running jobs' '
	list_R $(cat job11.id job12.id job13.id ) \
		 > first-fit-allocs.out &&
	cat <<-EOF >first-fit-allocs.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	EOF
	test_cmp first-fit-allocs.expected first-fit-allocs.out
'
test_expect_success 'sched-simple: reload with outstanding allocations' '
	flux module reload sched-simple &&
	test_debug "echo result=\"$($query)\"" &&
	test "$($query)" = ""
'
test_expect_success 'sched-simple: verify three jobs are active' '
	count=$(flux job list | wc -l) &&
	test ${count} -eq 3
'

test_expect_success 'sched-simple: remove sched-simple and cancel jobs' '
	flux module remove sched-simple &&
	flux cancel --all
'
test_expect_success 'sched-simple: there are no outstanding sched requests' '
	flux queue status -v >queue_status.out &&
	grep "0 alloc requests pending to scheduler" queue_status.out
'
test_expect_success 'sched-simple: reload in unlimited mode' '
	flux module load sched-simple mode=unlimited &&
	$dmesg_grep -t 10 "scheduler: ready unlimited"
'
test_expect_success 'sched-simple: submit 5 more jobs' '
	flux job submit basic.json >job14.id &&
	flux job submit basic.json >job15.id &&
	flux job submit basic.json >job16.id &&
	flux job submit basic.json >job17.id &&
	flux job submit basic.json >job18.id &&
	flux job wait-event --timeout=5.0 $(cat job16.id) alloc &&
	flux job wait-event --timeout=5.0 $(cat job18.id) submit
'
test_expect_success 'sched-simple: check allocations for running jobs' '
	list_R $(cat job14.id job15.id job16.id ) \
		 > unlimited-allocs.out &&
	cat <<-EOF >unlimited-allocs.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	EOF
	test_cmp unlimited-allocs.expected unlimited-allocs.out
'
test_expect_success 'sched-simple: update urgency of job' '
	flux job urgency $(cat job18.id) 20
'
test_expect_success 'sched-simple: cancel running job' '
	flux cancel $(cat job14.id) &&
	flux job wait-event --timeout=5.0 $(cat job14.id) free
'
test_expect_success 'sched-simple: ensure more urgent job run' '
	list_R $(cat job18.id job15.id job16.id) \
		 > unlimited-allocs2.out &&
	cat <<-EOF >unlimited-allocs2.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	EOF
	test_cmp unlimited-allocs2.expected unlimited-allocs2.out
'
# cancel all jobs, to ensure no interference with follow up tests
# cancel non-running jobs first to ensure they are not accidentally run
test_expect_success 'sched-simple: cancel jobs' '
	flux cancel --all --states=pending &&
	flux cancel --all &&
	flux job wait-event --timeout=5.0 $(cat job18.id) free &&
	flux job wait-event --timeout=5.0 $(cat job15.id) free &&
	flux job wait-event --timeout=5.0 $(cat job16.id) free
'
test_expect_success 'sched-simple: reload sched-simple to cover free flags' '
	flux module reload sched-simple test-free-nolookup
'
# That SCHEDUTIL_FREE_NOLOOKUP is now a no-op but since flux-sched-0.33.0
# still uses it, ensure that free still works when it is used
test_expect_success 'sched-simple: submit job and cancel it' '
	flux dmesg --clear &&
	flux job submit basic.json >job19.id &&
	flux job wait-event --timeout=5.0 $(cat job19.id) alloc &&
	flux cancel $(cat job19.id) &&
	$dmesg_grep -t 10 "free: rank0/core0"
'
test_expect_success 'sched-simple: remove sched-simple and cancel jobs' '
	flux module remove sched-simple &&
	flux cancel --all
'
test_expect_success 'sched-simple: there are no outstanding sched requests' '
	flux queue status -v >queue_status.out &&
	grep "0 alloc requests pending to scheduler" queue_status.out
'

test_expect_success 'sched-simple: load sched-simple and wait for queue drain' '
	flux module load sched-simple &&
	run_timeout 30 flux queue drain
'
test_done
