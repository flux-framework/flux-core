#!/bin/bash

test_description='sched-simple TreePool RFC 49 Cluster A test vectors'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Cluster A: Dell PowerEdge XE9680 (2-socket Xeon, SNC4)
#   2 sockets × 4 NUMA domains × 15 cores + 1 GPU per NUMA
#   = 120 cores, 8 GPUs per node, 16 nodes
#
# sched_tree_amender uses the module:function form of amend-r, so its
# directory must be on PYTHONPATH for importlib to find it by name.
export PYTHONPATH="${SHARNESS_TEST_SRCDIR}/scheduler${PYTHONPATH:+:${PYTHONPATH}}"

test_under_flux 1 full \
    --conf=fake-resources.nnodes=16 \
    --conf=fake-resources.cores-per-node=120 \
    --conf=fake-resources.gpus-per-node=8 \
    --conf=fake-resources.amend-r=sched_tree_amender:cluster_a

EXEC_TEST='--setattr=system.exec.test.run_duration=3600s'

test_expect_success 'reload sched-simple with pool-class=TreePool' '
    flux module reload sched-simple pool-class=TreePool
'

# RFC 49 test vectors (Cluster A: Xeon/Nvidia, SNC4, 2 sockets x 4 NUMA x 15 cores + 1 GPU)
# Allocations are cumulative to demonstrate topology-aware placement.
n=0
while read shape rlite; do
    n=$((n+1))
    test_expect_success "TVA${n}: --resources-shape='${shape}'" "
        jobid=\$(flux submit $EXEC_TEST \
            --resources-shape='${shape}' sleep inf) &&
        flux job wait-event --timeout=10 \$jobid alloc &&
        flux job info \$jobid R |
            jq -e '.execution.R_lite[0] == ${rlite}'
    "
done <<'EOF'
slot=1/node=1/[core=8;gpu=1] {"rank":"0","children":{"core":"0-7","gpu":"0"}}
slot=1/node=1/[core=8;gpu=1] {"rank":"0","children":{"core":"15-22","gpu":"1"}}
node/slot=8/[core=15;gpu=1] {"rank":"1","children":{"core":"0-119","gpu":"0-7"}}
slot=4/node/core=120 {"rank":"2-5","children":{"core":"0-119"}}
slot=1/node=1/core=4 {"rank":"0","children":{"core":"8-11"}}
slot=1/node=1/[core=15;gpu=1] {"rank":"0","children":{"core":"30-44","gpu":"2"}}
node/slot=6/[core=15;gpu=1] {"rank":"6","children":{"core":"0-89","gpu":"0-5"}}
slot=1/node=1/[core=60;gpu=4] {"rank":"0","children":{"core":"60-119","gpu":"4-7"}}
slot=1/numa{x} {"rank":"0","children":{"core":"45-59","gpu":"3"}}
slot=1/socket{x} {"rank":"7","children":{"core":"0-59","gpu":"0-3"}}
slot=1/node{x} {"rank":"8","children":{"core":"0-119","gpu":"0-7"}}
EOF

test_expect_success 'cleanup: cancel all TVA jobs' '
    flux cancel --all &&
    flux queue drain --timeout=60s
'

test_expect_success 'unload sched-simple' '
    flux module remove sched-simple
'

test_done
