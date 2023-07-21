#!/bin/sh

test_description='Test the flux-pstree command'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

export FLUX_PYCLI_LOGLEVEL=10
export FLUX_URI_RESOLVE_LOCAL=t
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py --line-buffer -f asciicast"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
export SHELL=/bin/sh

test_expect_success 'flux-pstree errors on invalid arguments' '
	test_must_fail flux pstree --skip-root=foo &&
	test_must_fail flux pstree --details=foo &&
	test_must_fail flux pstree --filter=bubbling
'
test_expect_success 'flux-pstree works in an empty instance' '
	name="empty" &&
	flux pstree > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --skip-root=yes works in empty instance' '
	name="empty-skip-root" &&
	flux pstree --skip-root=yes > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree -x works in empty instance' '
	name="empty-extended" &&
	flux pstree -x > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	       JOBID USER     ST NTASKS NNODES  RUNTIME
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree -a works in empty instance' '
	name="empty-all" &&
	flux pstree -a > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree -x --skip-root=no works in empty instance' '
	name="empty-extended-no-skip-root" &&
	flux pstree -x --skip-root=no > ${name}.output &&
	test_debug "cat ${name}.output" &&
	test $(cat ${name}.output | wc -l) -eq 2
'
#  Start a child instance that immediately exits, so that we can test
#   that `flux pstree` doesn't error on child instances that are no
#   longer running.
#
#  Then, start a job hiearachy with 2 child instances, each of which
#   run a sleep job, touch a ready.<id> file, then block waiting for
#   the sleep job to finish.
#
test_expect_success 'start a recursive job' '
	id=$(flux submit flux start true) &&
	rid=$(flux submit -n2 \
		flux start \
		flux submit --wait --cc=1-2 flux start \
			"flux submit sleep 300 && \
			 touch ready.\$FLUX_JOB_CC && \
			 flux queue idle") &&
	flux job wait-event $id clean
'
test_expect_success 'wait for hierarchy to be ready' '
	flux getattr broker.pid &&
	$waitfile -t 60 ready.1 &&
	$waitfile -t 60 ready.2
'
test_expect_success 'flux-pstree works' '
	name="normal" &&
	flux pstree > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	└── flux
	    ├── flux
	    │   └── sleep
	    └── flux
	        └── sleep
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree JOBID works' '
	name="jobid" &&
	flux pstree $rid > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	flux
	├── flux
	│   └── sleep
	└── flux
	    └── sleep
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree errors on unknown JOBID' '
	name="jobidunknown" &&
	flux pstree 123456789 2> ${name}.output &&
	test_debug "cat ${name}.output" &&
	grep "unknown" ${name}.output
'
test_expect_success 'flux-pstree errors on illegal JOBID' '
	name="jobidillegal" &&
	test_must_fail flux pstree IllegalID 2> ${name}.output &&
	test_debug "cat ${name}.output" &&
	grep "invalid JobID value" ${name}.output
'
test_expect_success 'flux-pstree works when run inside child job' '
	name="proxy" &&
	flux proxy $rid flux pstree > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	flux
	├── flux
	│   └── sleep
	└── flux
	    └── sleep
	EOF
	test_cmp ${name}.expected ${name}.output
'

test_expect_success 'flux-pstree -a works' '
	name="all" &&
	flux pstree -a > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	├── flux
	│   ├── flux
	│   │   └── sleep:R
	│   └── flux
	│       └── sleep:R
	└── flux:CD
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_columns_variable_preserved && test_set_prereq USE_COLUMNS
test_expect_success USE_COLUMNS 'flux-pstree truncates at COLUMNS' '
	name="truncated" &&
	COLUMNS=16 flux pstree -a > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	├── flux
	│   ├── flux
	│   │   └── sle+
	│   └── flux
	│       └── sle+
	└── flux:CD
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree does not truncate with -l' '
	name="notruncate" &&
	COLUMNS=16 flux pstree -al > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	├── flux
	│   ├── flux
	│   │   └── sleep:R
	│   └── flux
	│       └── sleep:R
	└── flux:CD
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --skip-root=yes works' '
	name="skip-root" &&
	flux pstree --skip-root=yes > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	flux
	├── flux
	│   └── sleep
	└── flux
	    └── sleep
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --level=1 works' '
	name="level1" &&
	flux pstree --level=1 > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	└── flux
	    └── 2*[flux]
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --level=1 --no-combine works' '
	name="level1-no-combine" &&
	flux pstree -L1 --no-combine > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	.
	└── flux
	    ├── flux
	    └── flux
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree -x' '
	name="extended" &&
	flux pstree -x > ${name}.output &&
	test_debug "cat ${name}.output" &&
	test $(cat ${name}.output | wc -l) -eq 6 &&
	head -n 1 ${name}.output | grep NNODES
'
test_expect_success 'flux-pstree --details=NAME works' '
	flux bulksubmit --watch \
		--job-name=details-{} \
		--output=details-{}.output \
		flux pstree --details={} \
		::: resources progress stats &&
	test_debug "cat details-*.output" &&
	grep STATS details-stats.output &&
	grep CORES details-resources.output &&
	grep PROG details-progress.output
'
test_expect_success 'flux-pstree --label= works' '
	name="label-format" &&
	flux pstree --label="{name} foo" > ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	. foo
	└── flux foo
	    ├── flux foo
	    │   └── sleep foo
	    └── flux foo
	        └── sleep foo
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --parent-label= works' '
	name="label-format" &&
	flux pstree \
		--label="{name} foo" \
		--parent-label="{name} bar" \
		> ${name}.output &&
	test_debug "cat ${name}.output" &&
	cat <<-EOF >${name}.expected &&
	. bar
	└── flux bar
	    ├── flux bar
	    │   └── sleep foo
	    └── flux bar
	        └── sleep foo
	EOF
	test_cmp ${name}.expected ${name}.output
'
test_expect_success 'flux-pstree --detail-format works' '
	name="detail-format" &&
	flux pstree --detail-format="{id.f58:<12}" > ${name}.output &&
	test_debug "cat ${name}.output" &&
	test $(head -n1 ${name}.output) = "JOBID"
'
test_expect_success 'flux-pstree --detail-format --skip-root=no works' '
	name="detail-format2" &&
	flux pstree \
		--no-header \
		--skip-root=no \
		--detail-format="{id.f58}" > ${name}.output &&
	test_debug "cat ${name}.output" &&
	test "$(head -n1 ${name}.output)" = ". ."
'
test_done
