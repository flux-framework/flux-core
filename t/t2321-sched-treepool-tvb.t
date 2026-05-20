#!/bin/bash

test_description='sched-simple TreePool RFC 49 Cluster B test vectors'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# Cluster B: HPE Cray EX / AMD MI300A
#   4 packages × 24 cores + 1 GPU per package
#   = 96 cores, 4 GPUs per node, 1152 nodes
#
# sched_tree_amender uses the module:function form of amend-r, so its
# directory must be on PYTHONPATH for importlib to find it by name.
export PYTHONPATH="${SHARNESS_TEST_SRCDIR}/scheduler${PYTHONPATH:+:${PYTHONPATH}}"

test_under_flux 1 full \
    --conf=fake-resources.nnodes=1152 \
    --conf=fake-resources.cores-per-node=96 \
    --conf=fake-resources.gpus-per-node=4 \
    --conf=fake-resources.amend-r=sched_tree_amender:cluster_b

EXEC_TEST='--setattr=system.exec.test.run_duration=3600s'

test_expect_success 'reload sched-simple with pool-class=TreePool' '
    flux module reload sched-simple pool-class=TreePool
'

# RFC 49 test vectors (Cluster B: MI300A, 4 packages × 24 cores + 1 GPU)
# Allocations are cumulative to demonstrate topology-aware placement.
n=0
while read shape rlite; do
    n=$((n+1))
    test_expect_success "TVB${n}: --resources-shape='${shape}'" "
        jobid=\$(flux submit $EXEC_TEST \
            --resources-shape='${shape}' sleep inf) &&
        flux job wait-event --timeout=10 \$jobid alloc &&
        flux job info \$jobid R |
            jq -e '.execution.R_lite[0] == ${rlite}'
    "
done <<'EOF'
slot=1/node=1/[core=24;gpu=1] {"rank":"0","children":{"core":"0-23","gpu":"0"}}
slot=1/node=1/[core=24;gpu=1] {"rank":"0","children":{"core":"24-47","gpu":"1"}}
node/slot=4/[core=24;gpu=1] {"rank":"1","children":{"core":"0-95","gpu":"0-3"}}
slot=4/node/[core=96;gpu=4] {"rank":"2-5","children":{"core":"0-95","gpu":"0-3"}}
slot=1/node=1/core=8 {"rank":"0","children":{"core":"48-55"}}
slot=1/node=1/[core=24;gpu=1] {"rank":"0","children":{"core":"72-95","gpu":"3"}}
node/slot=3/[core=24;gpu=1] {"rank":"6","children":{"core":"0-71","gpu":"0-2"}}
slot=1/socket{x} {"rank":"6","children":{"core":"72-95","gpu":"3"}}
slot=1/node{x} {"rank":"7","children":{"core":"0-95","gpu":"0-3"}}
EOF

test_expect_success 'cleanup: cancel all TVB jobs' '
    flux cancel --all &&
    flux queue drain --timeout=60s
'

test_expect_success 'unload sched-simple' '
    flux module remove sched-simple
'

test_done
