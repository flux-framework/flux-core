#!/bin/sh
#
test_description='Test flux-shell mustache template rendering'

. `dirname $0`/sharness.sh

test_under_flux 2 job

test_expect_success 'mustache: templates are rendered in job arguments' '
	flux run -N2 -n4 \
	  echo {{id.words}}: size={{size}} nnodes={{nnodes}} name={{name}} \
	  >output.1 &&
	test_debug "cat output.1" &&
	grep "size=4 nnodes=2 name=echo" output.1
'
test_expect_success 'mustache: per-task tags work' '
	flux run -N2 -n4 --label-io \
	  echo {{taskid}}: rank={{task.rank}} index={{task.index}} \
	  | sort >output.2 &&
	test_debug "cat output.2" &&
	cat <<-EOF >expected.2 &&
	0: 0: rank=0 index=0
	1: 1: rank=1 index=1
	2: 2: rank=2 index=0
	3: 3: rank=3 index=1
	EOF
	test_cmp expected.2 output.2
'
test_expect_success 'mustache: per-node tags work' '
	flux run -N2 -n2 --label-io \
	  echo id={{node.id}} name={{node.name}} cores={{node.cores}}[{{node.ncores}}] \
	  | sort >output.3 &&
	test_debug "cat output.3" &&
	cat <<-EOF >expected.3 &&
	0: id=0 name=$(hostname) cores=0[1]
	1: id=1 name=$(hostname) cores=0[1]
	EOF
	test_cmp expected.3 output.3
'
test_expect_success 'mustache: node.resources renders as JSON' '
	flux run -N2 -n2 echo {{node.resources}} | jq
'
test_expect_success 'mustache: unsupported tag is left alone' '
	flux run echo {{foo}} {{node.foo}} {{task.foo}} >output.4 &&
	test_debug "cat output.4" &&
	test "$(cat output.4)" = "{{foo}} {{node.foo}} {{task.foo}}"
'
test_expect_success 'mustache: mustache templates can be rendered in env' '
	flux run --env=TEST={{tmpdir}} -N2 \
		sh -c "test \$TEST = \$FLUX_JOB_TMPDIR"
'
test_expect_success 'mustache: env variables can have per-task tags' '
	flux run --env=TEST={{taskid}} -N2 -n4 \
		sh -c "test \$TEST = \$FLUX_TASK_RANK" &&
	flux run --env=T1={{size}} --env=T2={{taskid}} -N2 -n4 \
		sh -c "test \$T1 -eq 4 -a \$T2 = \$FLUX_TASK_RANK"
'
test_expect_success 'mustache: invalid tags in env vars are left unexpanded' '
	flux run --env=TEST={{task.foo}} \
		sh -c "test \$TEST = {{task.foo}}"
'
test_done
