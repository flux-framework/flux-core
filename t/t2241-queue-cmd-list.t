#!/bin/sh
test_description='Test flux queue command'

. $(dirname $0)/sharness.sh

test_under_flux 1 full

# anon queue

test_expect_success 'flux-queue: default lists expected fields' '
	flux queue list > default.out &&
	grep TDEFAULT default.out &&
	grep TLIMIT default.out &&
	grep NNODES default.out &&
	grep NCORES default.out &&
	grep ST default.out &&
	grep EN default.out &&
	grep NGPUS default.out
'

test_expect_success 'flux-queue: FLUX_QUEUE_LIST_FORMAT_DEFAULT works' '
	FLUX_QUEUE_LIST_FORMAT_DEFAULT="{limits.min.nnodes} {limits.max.ngpus}" \
		flux queue list > default_override.out &&
	grep MINNODES default_override.out &&
	grep MAXGPUS default_override.out
'

test_expect_success 'flux-queue: --no-header works' '
	flux queue list --no-header > default_no_header.out &&
	test_must_fail grep TDEFAULT default_no_header.out &&
	test_must_fail grep TLIMIT default_no_header.out &&
	test_must_fail grep NNODES default_no_header.out &&
	test_must_fail grep NCORES default_no_header.out &&
	test_must_fail grep ST default_no_header.out &&
	test_must_fail grep EN default_no_header.out &&
	test_must_fail grep NGPUS default_no_header.out
'

ALL_LIMITS_FMT="\
{defaults.timelimit},{limits.timelimit},\
{limits.range.nnodes},{limits.range.ncores},{limits.range.ngpus},\
{limits.min.nnodes},{limits.min.ncores},{limits.min.ngpus},\
{limits.max.nnodes},{limits.max.ncores},{limits.max.ngpus}\
"

test_expect_success 'flux-queue: expected headers with non-default fields output' '
	flux queue list -o "${ALL_LIMITS_FMT}" > non_default.out &&
	grep TDEFAULT non_default.out &&
	grep TLIMIT non_default.out &&
	grep NNODES non_default.out &&
	grep NCORES non_default.out &&
	grep NGPUS non_default.out &&
	grep MINNODES non_default.out &&
	grep MINCORES non_default.out &&
	grep MINGPUS non_default.out &&
	grep MAXNODES non_default.out &&
	grep MAXCORES non_default.out &&
	grep MAXGPUS non_default.out
'

test_expect_success 'flux-queue: empty config has no queues' '
	flux queue list -n \
		-o "{queue},{queuem}" > empty_config_queue.out &&
	echo "," > empty_config_queue.exp &&
	test_cmp empty_config_queue.exp empty_config_queue.out
'
test_expect_success 'flux-queue: empty config limits are 0/infinity' '
	flux queue list -n \
		-o "${ALL_LIMITS_FMT}" > empty_config_all.out &&
	echo "inf,inf,0-inf,0-inf,0-inf,0,0,0,inf,inf,inf" > empty_config_all.exp &&
	test_cmp empty_config_all.exp empty_config_all.out
'
test_expect_success 'flux-queue: empty config: queue is enabled/started' '
	test_debug "flux queue list" &&
	test "$(flux queue list -no {submission})" = "enabled" &&
	test "$(flux queue list -no {scheduling})" = "started" &&
	test "$(flux queue list -no {enabled})" = "✔" &&
	test "$(flux queue list -no {started})" = "✔"
'
test_expect_success 'flux-queue: stop anonymous queue' '
	flux queue stop
'
test_expect_success 'flux-queue: queue is enabled/stopped' '
	test_debug "flux queue list" &&
	test "$(flux queue list -no {submission})" = "enabled" &&
	test "$(flux queue list -no {scheduling})" = "stopped" &&
	test "$(flux queue list -no {enabled})" = "✔" &&
	test "$(flux queue list -no {started})" = "✗"
'
test_expect_success 'flux-queue: disable anonymous queue' '
	flux queue disable testing
'
test_expect_success 'flux-queue: queue is disabled/stopped' '
	test_debug "flux queue list" &&
	test "$(flux queue list -no {submission})" = "disabled" &&
	test "$(flux queue list -no {scheduling})" = "stopped" &&
	test "$(flux queue list -no {enabled})" = "✗" &&
	test "$(flux queue list -no {started})" = "✗"
'
test_expect_success 'flux-queue: enable and start anonymous queue' '
	flux queue enable &&
	flux queue start
'
test_expect_success 'flux-queue: --queue option fails with anonymouns queue' '
	test_expect_code 1 flux queue list --queue=batch
'

test_expect_success 'flux-queue: fsd of infinity is infinity' '
	flux queue list -n \
		-o "{defaults.timelimit!F},{limits.timelimit!F}" \
		> empty_config_fsd.out &&
	echo "inf,inf" > empty_config_fsd.exp &&
	test_cmp empty_config_fsd.exp empty_config_fsd.out
'

# N.B. job-size.max.ngpus left out to test default of infinity
test_expect_success 'config flux with policy defaults' '
	flux config load <<EOT
[policy.jobspec.defaults.system]
duration = "1h"

[policy.limits]
duration = "4h"
job-size.min.nnodes = 2
job-size.max.nnodes = 10
job-size.min.ncores = 1
job-size.max.ncores = -1
job-size.min.ngpus = 1
EOT
'

test_expect_success 'flux-queue: defaults config timelimits' '
       flux queue list -n \
	       -o "{defaults.timelimit},{limits.timelimit},{defaults.timelimit!F},{limits.timelimit!F}" \
	       > policy_defaults_timelimit.out &&
       echo "3600.0,14400.0,1h,4h" > policy_defaults_timelimit.exp &&
       test_cmp policy_defaults_timelimit.exp policy_defaults_timelimit.out
'
test_expect_success 'flux-queue: defaults config ranges' '
       flux queue list -n \
	       -o "{limits.range.nnodes},{limits.range.ncores},{limits.range.ngpus}" \
	       > policy_defaults_range.out &&
       echo "2-10,1-inf,1-inf" > policy_defaults_range.exp &&
       test_cmp policy_defaults_range.exp policy_defaults_range.out
'
test_expect_success 'flux-queue: defaults config mins' '
       flux queue list -n \
	       -o "{limits.min.nnodes},{limits.min.ncores},{limits.min.ngpus}" \
	       > policy_defaults_min.out &&
       echo "2,1,1" > policy_defaults_min.exp &&
       test_cmp policy_defaults_min.exp policy_defaults_min.out
'
test_expect_success 'flux-queue: defaults config maxes' '
       flux queue list -n \
	       -o "{limits.max.nnodes},{limits.max.ncores},{limits.max.ngpus}" \
	       > policy_defaults_max.out &&
       echo "10,inf,inf" > policy_defaults_max.exp &&
       test_cmp policy_defaults_max.exp policy_defaults_max.out
'

#
# named queues
#

test_expect_success 'config flux with several named queues but no other config' '
	flux config load <<EOT
[queues.batch]
[queues.debug]
EOT
'
# we can't predict order of listing, so we grep and count lines
test_expect_success 'flux-queue: queue config, no default marked' '
	flux queue list -no "{queue},{queuem}" > queues_no_default.out &&
	test $(grep "^batch,batch$" queues_no_default.out | wc -l) -eq 1 &&
	test $(grep ^"debug,debug$" queues_no_default.out | wc -l) -eq 1
'
test_expect_success 'flux-queue: --queue option works' '
	test "$(flux queue list -q batch -no "{queue}")" = "batch" &&
	test "$(flux queue list -q debug -no "{queue}")" = "debug" &&
	test_debug "flux queue list -q batch,debug -n" &&
	test $(flux queue list -q batch,debug -n  | wc -l) -eq 2
'
test_expect_success 'flux-queue: invalid queue is detected with --queue' '
	test_expect_code 1 flux queue list --queue=foo
'

# N.B. job-size.max.ngpus left out of [policy.limits] to test default
# of infinity
# N.B. Note that many values in [queues.debug.policy.limits] left out to
# test that system wide config is parsed correctly
test_expect_success 'create queue defaults config for tests' '
	flux config load <<EOT
[policy.jobspec.defaults.system]
queue = "batch"
duration = "1h"

[policy.limits]
duration = "4h"
job-size.min.nnodes = 2
job-size.max.nnodes = 10
job-size.min.ncores = 1
job-size.max.ncores = -1
job-size.min.ngpus = 1

[queues.batch]
policy.jobspec.defaults.system.duration = "2h"

[queues.batch.policy.limits]
duration = "8h"
job-size.min.nnodes = 5
job-size.max.nnodes = 15
job-size.min.ncores = 1
job-size.max.ncores = 20
job-size.min.ngpus = 1
job-size.max.ngpus = 4

[queues.debug.policy.limits]
duration = "2h"
job-size.max.nnodes = 4
job-size.min.ncores = 2
EOT
'
# we can't predict order of listing, so we grep and count lines
test_expect_success 'flux-queue: queue config' '
	flux queue list -no "{queue},{queuem}" > queues_config_queue.out &&
	test $(grep "^batch,batch\*$" queues_config_queue.out | wc -l) -eq 1 &&
	test $(grep ^"debug,debug$" queues_config_queue.out | wc -l) -eq 1
'
test_expect_success 'flux-queue: queue config timelimits' '
	flux queue list -n \
		-o "{queue},{defaults.timelimit},{limits.timelimit},{defaults.timelimit!F},{limits.timelimit!F}" \
		> queues_timelimit.out &&
	test $(grep "batch,7200.0,28800.0,2h,8h" queues_timelimit.out | wc -l) -eq 1 &&
	test $(grep "debug,3600.0,7200.0,1h,2h" queues_timelimit.out | wc -l) -eq 1
'
test_expect_success 'flux-queue: queue config ranges' '
	flux queue list -n \
		-o "{queue},{limits.range.nnodes},{limits.range.ncores},{limits.range.ngpus}" \
		> queues_range.out &&
	test $(grep "batch,5-15,1-20,1-4" queues_range.out | wc -l) -eq 1 &&
	test $(grep "debug,2-4,2-inf,1-inf" queues_range.out | wc -l) -eq 1
'
test_expect_success 'flux-queue: queue config mins' '
	flux queue list -n \
		-o "{queue},{limits.min.nnodes},{limits.min.ncores},{limits.min.ngpus}" \
		> queues_min.out &&
	test $(grep "batch,5,1,1" queues_min.out | wc -l) -eq 1 &&
	test $(grep "debug,2,2,1" queues_min.out | wc -l) -eq 1
'
test_expect_success 'flux-queue: queue config maxes' '
	flux queue list -n \
		-o "{queue},{limits.max.nnodes},{limits.max.ncores},{limits.max.ngpus}" \
		> queues_max.out &&
	test $(grep "batch,15,20,4" queues_max.out | wc -l) -eq 1 &&
	test $(grep "debug,4,inf,inf" queues_max.out | wc -l) -eq 1
'

#
# config
#

#  We can't add configuration to ~/.config or /etc/xdg/flux, so
#   just test that XDG_CONFIG_HOME works.
#
test_expect_success 'Create flux-queue.toml config file' '
	mkdir -p dir/flux &&
	cat <<-EOT >dir/flux/flux-queue.toml
	[list.formats.myformat]
	description = "my list format"
	format = "{queue}"
	EOT
'

test_expect_success "flux-queue list accepts configured formats" '
	XDG_CONFIG_HOME="$(pwd)/dir" \
		flux queue list --format=help > list-help.out &&
	test_debug "cat list-help.out" &&
	grep myformat list-help.out &&
	XDG_CONFIG_HOME="$(pwd)/dir" \
		flux queue list --format=get-config=myformat \
		>list-get-config.out &&
	test_debug "cat list-get-config.out" &&
	grep list\.formats\.myformat list-get-config.out &&
	XDG_CONFIG_HOME="$(pwd)/dir" \
		flux queue list -n \
			--format=myformat > list-myformat.out &&
	test $(grep "^batch$" list-myformat.out | wc -l) -eq 1 &&
	test $(grep ^"debug$" list-myformat.out | wc -l) -eq 1
'

#
#
# corner cases
#

test_expect_success 'flux-queue: invalid fields recognized' '
	test_must_fail flux queue list -o "{foobar}" &&
	test_must_fail flux queue list -o "{defaults.foobar}" &&
	test_must_fail flux queue list -o "{limits.foobar}" &&
	test_must_fail flux queue list -o "{limits.range.foobar}" &&
	test_must_fail flux queue list -o "{limits.min.foobar}" &&
	test_must_fail flux queue list -o "{limits.max.foobar}"
'

test_done
