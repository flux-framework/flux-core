#!/bin/sh

test_description='Test the flux-jobspec command'

. `dirname $0`/sharness.sh

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
VALIDATE=${JOBSPEC}/validate.py
SCHEMA=${FLUX_SOURCE_DIR}/src/modules/job-ingest/schemas/jobspec.jsonschema
MINI_SCHEMA=${JOBSPEC}/minimal-schema.json
SUMMARIZE=${JOBSPEC}/summarize-minimal-jobspec.py

#  Set path to jq
#
jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ

validate_emission() {
    flux jobspec $@ | ${VALIDATE} --schema ${SCHEMA}
}

validate_minimal_emission() {
    flux jobspec $@ | ${VALIDATE} --schema ${MINI_SCHEMA}
}

test_expect_success 'flux-jobspec srun with no args emits valid canonical jobspec' '
    validate_emission srun sleep 1
'

test_expect_success 'flux-jobspec srun with no args emits minimal jobspec' '
    validate_minimal_emission srun sleep 1
'

test_expect_success 'flux-jobspec srun with just num_tasks emits valid canonical jobspec' '
    validate_emission srun -n4 sleep 1
'

test_expect_success 'flux-jobspec srun with just num_tasks emits minimal jobspec' '
    validate_minimal_emission srun -n4 sleep 1
'

test_expect_success 'flux-jobspec srun with just cores_per_task emits valid canonical jobspec' '
    validate_emission srun -c4 sleep 1
'

test_expect_success 'flux-jobspec srun with just cores_per_task emits minimal jobspec' '
    validate_minimal_emission srun -c4 sleep 1
'

test_expect_success 'flux-jobspec srun without num_nodes emits valid canonical jobspec' '
    validate_emission srun -n4 -c1 sleep 1
'

test_expect_success 'flux-jobspec srun without num_nodes emits minimal jobspec' '
    validate_minimal_emission srun -n4 -c1 sleep 1
'

test_expect_success 'flux-jobspec srun with all args emits valid canonical jobspec' '
    validate_emission srun -N4 -n4 -c1 sleep 1
'

test_expect_success 'flux-jobspec srun with all args emits minimal jobspec' '
    validate_minimal_emission srun -N4 -n4 -c1 sleep 1
'

test_expect_success HAVE_JQ 'ensure flux-jobspec does not gobble application arguments' '
    flux jobspec srun myApp -c1 -n1 -N1 | $jq ".tasks[0].command" > myApp.out &&
    cat <<-EOF | $jq . > myApp.expected &&
    [ "myApp", "-c1", "-n1", "-N1" ]
	EOF
    test_cmp myApp.expected myApp.out
'

test_expect_success 'requesting more nodes than tasks should produce an error' '
   test_must_fail flux jobspec srun -N8 -n2 hostname 2>&1 |
   grep -q "Number of nodes greater than the number of tasks"
'

test_expect_success HAVE_JQ 'specifying only -N8 should produce (in total) 8 nodes, 8 tasks, 8 cores' '
    flux jobspec srun -N8 hostname > only-nodes.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} only-nodes.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} only-nodes.jobspec.json &&
    ${SUMMARIZE} -j only-nodes.jobspec.json > only-nodes.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 8 and .total_num_cores == 8" only-nodes.summary.json
'

test_expect_success HAVE_JQ 'specifying only -n8 should produce (in total) 0 nodes, 8 tasks, 8 cores' '
    flux jobspec srun -n8 hostname > only-tasks.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} only-tasks.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} only-tasks.jobspec.json &&
    ${SUMMARIZE} -j only-tasks.jobspec.json > only-tasks.summary.json &&
    jq -e ".total_num_nodes == 0 and .total_num_tasks == 8 and .total_num_cores == 8" only-tasks.summary.json
'

test_expect_success HAVE_JQ 'specifying only -c8 should produce (in total) 0 nodes, 1 task, 8 cores' '
    flux jobspec srun -c8 hostname > only-cores.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} only-cores.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} only-cores.jobspec.json &&
    ${SUMMARIZE} -j only-cores.jobspec.json > only-cores.summary.json &&
    jq -e ".total_num_nodes == 0 and .total_num_tasks == 1 and .total_num_cores == 8" only-cores.summary.json
'

test_expect_success HAVE_JQ 'specifying -N8 -c2 should produce (in total) 8 nodes, 8 tasks, 16 cores' '
    flux jobspec srun -N8 -c2 hostname > nodes-cores.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} nodes-cores.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} nodes-cores.jobspec.json &&
    ${SUMMARIZE} -j nodes-cores.jobspec.json > nodes-cores.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 8 and .total_num_cores == 16" nodes-cores.summary.json
'

test_expect_success HAVE_JQ 'specifying -N8 -n16 should produce (in total) 8 nodes, 16 tasks, 16 cores' '
    flux jobspec srun -N8 -n16 hostname > nodes-tasks.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} nodes-tasks.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} nodes-tasks.jobspec.json &&
    ${SUMMARIZE} -j nodes-tasks.jobspec.json > nodes-tasks.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 16 and .total_num_cores == 16" nodes-tasks.summary.json
'

test_expect_success HAVE_JQ 'specifying -n16 -c4 should produce (in total) 0 nodes, 16 tasks, 64 cores' '
    flux jobspec srun -n16 -c4 hostname > tasks-cores.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} tasks-cores.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} tasks-cores.jobspec.json &&
    ${SUMMARIZE} -j tasks-cores.jobspec.json > tasks-cores.summary.json &&
    jq -e ".total_num_nodes == 0 and .total_num_tasks == 16 and .total_num_cores == 64" tasks-cores.summary.json
'

test_expect_success HAVE_JQ 'specifying -N8 -n16 -c4 should produce (in total) 8 nodes, 16 tasks, 64 cores' '
    flux jobspec srun -N8 -n16 -c4 hostname > all-three-1.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} all-three-1.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} all-three-1.jobspec.json &&
    ${SUMMARIZE} -j all-three-1.jobspec.json > all-three-1.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 16 and .total_num_cores == 64" all-three-1.summary.json
'

test_expect_success HAVE_JQ 'specifying -N9 -n9 -c2 should produce (in total) 9 nodes, 9 tasks, 18 cores' '
    flux jobspec srun -N9 -n9 -c2 hostname > all-three-2.jobspec.json &&
    ${VALIDATE} -s ${MINI_SCHEMA} all-three-2.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} all-three-2.jobspec.json &&
    ${SUMMARIZE} -j all-three-2.jobspec.json > all-three-2.summary.json &&
    jq -e ".total_num_nodes == 9 and .total_num_tasks == 9 and .total_num_cores == 18" all-three-2.summary.json
'

test_expect_success HAVE_JQ 'specifying -N8 -n9 -c1 should produce (in total) 8 nodes, 9 tasks, 16 cores and a warning' '
    flux jobspec srun -N8 -n9 -c1 hostname > all-three-3.jobspec.json 2> all-three-3.warning.err &&
    ${VALIDATE} -s ${MINI_SCHEMA} all-three-3.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} all-three-3.jobspec.json &&
    ${SUMMARIZE} -j all-three-3.jobspec.json > all-three-3.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 9 and .total_num_cores == 16" all-three-3.summary.json &&
    grep -q "Number of tasks is not an integer multiple of the number of nodes." all-three-3.warning.err
'

test_expect_success HAVE_JQ 'specifying -N8 -n9 -c2 should produce (in total) 8 nodes, 9 tasks, 32 cores and a warning' '
    flux jobspec srun -N8 -n9 -c2 hostname > all-three-4.jobspec.json 2> all-three-4.warning.err &&
    ${VALIDATE} -s ${MINI_SCHEMA} all-three-4.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} all-three-4.jobspec.json &&
    ${SUMMARIZE} -j all-three-4.jobspec.json > all-three-4.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 9 and .total_num_cores == 32" all-three-4.summary.json &&
    grep -q "Number of tasks is not an integer multiple of the number of nodes." all-three-4.warning.err
'

test_expect_success HAVE_JQ 'specifying -N8 -n25 -c2 should produce (in total) 8 nodes, 25 tasks, 64 cores and a warning' '
    flux jobspec srun -N8 -n25 -c2 hostname > all-three-5.jobspec.json 2> all-three-5.warning.err &&
    ${VALIDATE} -s ${MINI_SCHEMA} all-three-5.jobspec.json &&
    ${VALIDATE} -s ${SCHEMA} all-three-5.jobspec.json &&
    ${SUMMARIZE} -j all-three-5.jobspec.json > all-three-5.summary.json &&
    jq -e ".total_num_nodes == 8 and .total_num_tasks == 25 and .total_num_cores == 64" all-three-5.summary.json &&
    grep -q "Number of tasks is not an integer multiple of the number of nodes." all-three-5.warning.err
'


test_done
