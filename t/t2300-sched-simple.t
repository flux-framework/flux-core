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


SCHEMA=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec.jsonschema
JSONSCHEMA_VALIDATOR=${FLUX_SOURCE_DIR}/src/modules/job-ingest/validators/validate-schema.py

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
test_expect_success 'sched-simple: reload ingest module with lax validator' '
	flux module reload job-ingest \
		validator-args="--schema,${SCHEMA}" \
		validator=${JSONSCHEMA_VALIDATOR} &&
	flux exec -r all -x 0 flux module reload job-ingest \
		validator-args="--schema,${SCHEMA}" \
		validator=${JSONSCHEMA_VALIDATOR}
'
test_expect_success 'sched-simple: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'
test_expect_success 'sched-simple: load default resource.R' '
	flux kvs put resource.R="$(cat R.test)" &&
	flux kvs get resource.R
'
test_expect_success 'sched-simple: reload sched-simple' '
	flux module unload sched-simple &&
	flux module reload resource monitor-force-up noverify &&
	flux module load sched-simple &&
	flux dmesg 2>&1 >reload.dmesg.log &&
	grep "ready:.*rank\[0-1\]/core\[0-1\]" reload.dmesg.log &&
	test_debug "echo result=\"$($query)\"" &&
	test "$($query)" = "rank[0-1]/core[0-1]"
'
test_expect_success 'sched-simple: unsatisfiable request is canceled' '
	flux jobspec srun -c 3 hostname | flux job submit >job0.id &&
	job0id=$(cat job0.id) &&
        flux job wait-event --timeout=5.0 $job0id exception &&
	flux job eventlog $job0id | grep "unsatisfiable request"
'
test_expect_success 'sched-simple: gpu request is canceled' '
	jobid=$(flux mini run -n1 -g1 --dry-run hostname | flux job submit) &&
	flux job wait-event --timeout=5.0 $jobid exception &&
	flux job eventlog $jobid | grep  "Unsupported resource type .gpu."
'
Y2J="flux python ${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py"
SPEC=${SHARNESS_TEST_SRCDIR}/jobspec/valid/basic.yaml
test_expect_success HAVE_JQ 'sched-simple: invalid minimal jobspec is canceled' '
	${Y2J}<${SPEC} | jq ".version = 1" | flux job submit >job00.id &&
	jobid=$(cat job00.id) &&
        flux job wait-event --timeout=5.0 $jobid exception &&
	flux job eventlog $jobid | grep "Unable to determine slot size"
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
	flux job cancel $(cat job3.id) &&
        flux job wait-event --timeout=5.0 $(cat job3.id) exception &&
        flux job wait-event --timeout=5.0 $(cat job3.id) free
'
test_expect_success 'sched-simple: waiting job now has alloc event' '
        flux job wait-event --timeout=5.0 $(cat job5.id) alloc &&
	test "$(list_R $(cat job5.id))" = "annotations={\"sched\":{\"resource_summary\":\"rank0/core1\"}}"
'
test_expect_success 'sched-simple: cancel all jobs' '
	flux job cancel $(cat job5.id) &&
	flux job cancel $(cat job4.id) &&
	flux job cancel $(cat job2.id) &&
	flux job cancel $(cat job1.id) &&
	flux job wait-event --timeout=5.0 $(cat job1.id) exception &&
	flux job wait-event --timeout=5.0 $(cat job1.id) free &&
	test "$($query)" = "rank[0-1]/core[0-1]"
'
test_expect_success 'sched-simple: reload in best-fit mode' '
	flux module reload sched-simple mode=best-fit
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
test_expect_success 'sched-simple: cancel pending job' '
	id=$(cat job10.id) &&
	flux job cancel $id &&
	flux job wait-event --timeout=5.0 $id exception &&
	flux job cancel $(cat job6.id) &&
	test_expect_code 1 flux kvs get $(kvs_job_dir $id).R
'
test_expect_success 'sched-simple: cancel remaining jobs' '
	flux job cancel $(cat job7.id) &&
	flux job cancel $(cat job8.id) &&
	flux job cancel $(cat job9.id) &&
	flux job wait-event --timeout=5.0 $(cat job9.id) free
'
test_expect_success 'sched-simple: reload with sched-PUs option' '
	flux module reload -f sched-simple sched-PUs
'
test_expect_success 'sched-simple: PUs now treated as cores' '
	test_debug "flux resource list -v" &&
	test "$($query)" = "rank[0-1]/core[0-3]"
'
test_expect_success 'sched-simple: reload in first-fit mode' '
        flux module remove sched-simple &&
        flux module remove resource &&
	flux kvs put resource.R="$(cat R.test.first_fit)" &&
	flux module load resource monitor-force-up noverify &&
        flux module load sched-simple mode=first-fit &&
	flux dmesg | grep "ready:.*rank0/core\[0-1\] rank1/core0"
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
	flux dmesg | grep "hello: alloc rank0/core0" &&
	test "$($query)" = ""
'
test_expect_success 'sched-simple: verify three jobs are active' '
	count=$(flux job list | wc -l) &&
	test ${count} -eq 3
'

test_expect_success 'sched-simple: remove sched-simple and cancel jobs' '
	flux module remove sched-simple &&
	flux job cancelall -f
'
test_expect_success 'sched-simple: there are no outstanding sched requests' '
	flux queue status -v 2>queue_status.out &&
	grep "0 alloc requests pending to scheduler" queue_status.out &&
	grep "0 free requests pending to scheduler" queue_status.out
'
test_expect_success 'sched-simple: reload in unlimited mode' '
	flux module load sched-simple unlimited &&
	flux dmesg | grep "scheduler: ready unlimited"
'
test_expect_success 'sched-simple: submit 3 more jobs' '
	flux job submit basic.json >job11.id &&
	flux job submit basic.json >job12.id &&
	flux job submit basic.json >job13.id &&
	flux job wait-event --timeout=5.0 $(cat job13.id) alloc
'
test_expect_success 'sched-simple: check allocations for running jobs' '
	list_R $(cat job11.id job12.id job13.id ) \
		 > single-allocs.out &&
	cat <<-EOF >first-fit-allocs.expected &&
	annotations={"sched":{"resource_summary":"rank0/core0"}}
	annotations={"sched":{"resource_summary":"rank0/core1"}}
	annotations={"sched":{"resource_summary":"rank1/core0"}}
	EOF
	test_cmp first-fit-allocs.expected first-fit-allocs.out
'
test_expect_success 'sched-simple: remove sched-simple and cancel jobs' '
	flux module remove sched-simple &&
	flux job cancelall -f
'
test_expect_success 'sched-simple: there are no outstanding sched requests' '
	flux queue status -v 2>queue_status.out &&
	grep "0 alloc requests pending to scheduler" queue_status.out &&
	grep "0 free requests pending to scheduler" queue_status.out
'

test_expect_success 'sched-simple: load sched-simple and wait for queue drain' '
	flux module load sched-simple &&
	run_timeout 30 flux queue drain
'
test_done
