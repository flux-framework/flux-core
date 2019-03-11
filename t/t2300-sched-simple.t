#!/bin/sh

test_description='simple sched-simple tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 4 job

query=${FLUX_BUILD_DIR}/src/modules/sched-simple/rlist-query

hwloc_by_rank='{"0-1": {"Core": 2, "cpuset": "0-1"}}'
hwloc_by_rank_first_fit='{"0": {"Core": 2}, "1": {"Core": 1}}'

kvs_active() {
	flux job id --to=kvs-active $1
}
list_R() {
	for id in "$@"; do
		flux job eventlog $id | sed -n 's/.*alloc //gp'
	done
}

test_expect_success 'sched-simple: generate jobspec for simple test job' '
        flux jobspec srun -n1 hostname >basic.json
'
test_expect_success 'sched-simple: load default by_rank' '
	flux kvs put resource.hwloc.by_rank="$(echo $hwloc_by_rank)" &&
	flux kvs get resource.hwloc.by_rank
'
test_expect_success 'sched-simple: load sched-simple' '
	flux module load -r 0 sched-simple &&
	flux dmesg 2>&1 | grep "ready:.*rank\[0-1\]/core\[0-1\]" &&
	test "$($query)" = "rank[0-1]/core[0-1]"
'
test_expect_success 'sched-simple: unsatisfiable request is canceled' '
	flux jobspec srun -c 3 hostname | flux job submit >job0.id &&
	job0id=$(cat job0.id) &&
        flux job wait-event --timeout=5.0 $job0id exception &&
	flux job eventlog $job0id | grep "unsatisfiable request"
'

Y2J=${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py
SPEC=${SHARNESS_TEST_SRCDIR}/jobspec/valid/basic.yaml
test_expect_success 'sched-simple: invalid minimal jobspec is canceled' '
	${Y2J}<${SPEC} | flux job submit >job00.id &&
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
	rank0/core0
	rank1/core0
	rank0/core1
	rank1/core1
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
	test "$(list_R $(cat job5.id))" = "rank0/core1"
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
	flux module remove -r 0 sched-simple &&
	flux module load -r 0 sched-simple mode=best-fit
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
	rank0/core0
	rank0/core1
	rank1/core0
	rank1/core1
	EOF
	test_cmp best-fit-allocs.expected best-fit-allocs.out
'
test_expect_success 'sched-simple: cancel pending job' '
	id=$(cat job10.id) &&
	flux job cancel $id &&
	flux job wait-event --timeout=5.0 $id exception &&
	flux job cancel $(cat job6.id) &&
	test_expect_code 1 flux kvs get $(kvs_active $id).R
'
test_expect_success 'sched-simple: cancel remaining jobs' '
	flux job cancel $(cat job7.id) &&
	flux job cancel $(cat job8.id) &&
	flux job cancel $(cat job9.id) &&
	flux job wait-event --timeout=5.0 $(cat job9.id) free
'
test_expect_success 'sched-simple: reload in first-fit mode' '
        flux module remove -r 0 sched-simple &&
	flux kvs put resource.hwloc.by_rank="$(echo $hwloc_by_rank_first_fit)" &&
        flux module load -r 0 sched-simple mode=first-fit &&
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
	rank0/core0
	rank0/core1
	rank1/core0
	EOF
	test_cmp first-fit-allocs.expected first-fit-allocs.out
'
test_expect_success 'sched-simple: reload with outstanding allocations' '
	flux module remove -r 0 sched-simple &&
	flux module load sched-simple &&
	flux dmesg | grep "hello: alloc rank0/core0" &&
	test "$($query)" = ""
'
test_expect_success 'sched-simple: remove sched-simple' '
	flux module remove -r 0 sched-simple
'

test_done
