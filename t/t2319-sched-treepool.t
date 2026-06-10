#!/bin/bash

test_description='sched-simple pool-class: TreePool sub-node affinity'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Sierra topology (IBM Power9 + NVIDIA V100): 4 nodes, 2 sockets × 22 cores
#   + 2 GPUs/socket = 44 cores, 4 GPUs per node.  HBM NUMANodes have empty
#   cpusets in hwloc 2.x and are filtered; only sockets appear in the tree.
SIERRA_XML="${SHARNESS_TEST_SRCDIR}/hwloc-data/sierra.xml"

test_under_flux 1 full \
    --conf=fake-resources.nnodes=4 \
    --conf=fake-resources.hwloc-xml-path="${SIERRA_XML}" \
    --conf=fake-resources.amend-r=flux.resource.TreePool:amend

EXEC_TEST='--setattr=system.exec.test.run_duration=3600s'

test_expect_success 'reload sched-simple with pool-class=TreePool' '
    flux module reload sched-simple pool-class=TreePool
'

test_expect_success 'allocated R carries scheduling.children with writer key' '
    jobid=$(flux submit $EXEC_TEST -N1 -n1 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} alloc &&
    flux job info ${jobid} R | jq -e ".scheduling.children" &&
    flux job info ${jobid} R | jq -e ".scheduling.writer == \"TreePool\""
'
test_expect_success 'allocated children trimmed to the one assigned node' '
    flux job info ${jobid} R | jq -e ".scheduling.children | length == 1" &&
    flux job info ${jobid} R |
        jq -e ".scheduling.children[0].ranks | test(\"-\") | not"
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'GPU+cross-socket slot succeeds via best-effort fallback' '
    jobid=$(flux submit $EXEC_TEST -N1 -n1 -c23 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} alloc
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'GPU slot exceeding node GPU count is permanently denied' '
    jobid=$(flux submit -N1 -n1 -c1 -g5 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} exception
'

test_expect_success 'best-fit: two GPU+core slots pack onto the same node, different socket' '
    job0=$(flux submit $EXEC_TEST -N1 -n1 -c1 -g1 sleep inf) &&
    job1=$(flux submit $EXEC_TEST -N1 -n1 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${job0} alloc &&
    flux job wait-event --timeout=10 ${job1} alloc &&
    rank0=$(flux job info ${job0} R | jq -r ".execution.R_lite[0].rank") &&
    rank1=$(flux job info ${job1} R | jq -r ".execution.R_lite[0].rank") &&
    test "$rank0" = "$rank1" &&
    gpu0=$(flux job info ${job0} R | jq -r ".execution.R_lite[0].children.gpu") &&
    gpu1=$(flux job info ${job1} R | jq -r ".execution.R_lite[0].children.gpu") &&
    test "$gpu0" != "$gpu1"
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

# Fill all GPUs across all 4 Sierra nodes (4 nodes × 4 GPUs = 16 slots),
# then verify that a CPU-only job still allocates despite no free GPUs anywhere.
test_expect_success 'CPU-only job runs when all GPUs in pool are exhausted' '
    gpujob=$(flux submit $EXEC_TEST -N4 -n16 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=30 ${gpujob} alloc &&
    cpujob=$(flux submit $EXEC_TEST -N1 -n1 -c1 sleep inf) &&
    flux job wait-event --timeout=10 ${cpujob} alloc
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'exclusive single-slot job: nslots is 1, not inflated' '
    jobid=$(flux submit $EXEC_TEST --exclusive -N1 -n1 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} alloc &&
    flux job info ${jobid} R | jq -e ".execution.nslots == 1"
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'exclusive two-slot job: nslots is 2, not inflated' '
    jobid=$(flux submit $EXEC_TEST --exclusive -N1 -n2 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} alloc &&
    flux job info ${jobid} R | jq -e ".execution.nslots == 2"
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'sequential exclusive jobs land on different nodes' '
    job0=$(flux submit $EXEC_TEST --exclusive -N1 -n1 -c1 -g1 sleep inf) &&
    job1=$(flux submit $EXEC_TEST --exclusive -N1 -n1 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${job0} alloc &&
    flux job wait-event --timeout=10 ${job1} alloc &&
    rank0=$(flux job info ${job0} R | jq ".execution.R_lite[0].rank") &&
    rank1=$(flux job info ${job1} R | jq ".execution.R_lite[0].rank") &&
    test "$rank0" != "$rank1"
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'exclusive jobs fill all nodes; 5th stays pending' '
    ids= &&
    for i in $(seq 1 4); do
        ids="${ids} $(flux submit $EXEC_TEST \
            --exclusive -N1 -n1 -c1 -g1 sleep inf)"
    done &&
    for id in ${ids}; do
        flux job wait-event --timeout=30 ${id} alloc
    done &&
    overflow=$(flux submit $EXEC_TEST --exclusive -N1 -n1 -c1 -g1 sleep inf) &&
    ! flux job wait-event --timeout=5 ${overflow} alloc
'
test_expect_success 'cleanup running jobs' '
    flux cancel --all &&
    flux queue drain --timeout=30s
'

test_expect_success 'unload sched-simple' '
    flux module remove sched-simple
'

# Test pool_class class-attribute path: a Scheduler subclass that binds
# TreePool via pool_class so no pool-class= argument or writer key is needed.
TREE_SUBCLASS="${SHARNESS_TEST_SRCDIR}/scheduler/sched-tree-subclass.py"

test_expect_success 'load sched-tree-subclass (pool_class class attribute)' '
    flux module load ${TREE_SUBCLASS}
'

test_expect_success 'pool_class attr: GPU+core job allocates with affinity' '
    jobid=$(flux submit $EXEC_TEST -N1 -n1 -c1 -g1 sleep inf) &&
    flux job wait-event --timeout=10 ${jobid} alloc &&
    flux cancel ${jobid} &&
    flux job wait-event --timeout=10 ${jobid} clean
'

test_expect_success 'unload sched-tree-subclass' '
    flux module remove sched-tree-subclass
'

test_done
