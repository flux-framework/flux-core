#!/bin/sh

test_description='Test flux mini command'

. $(dirname $0)/sharness.sh

test_under_flux 4

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

flux setattr log-stderr-level 1

#  Reload sched-simple with PU scheduling so we can treat single-core
#  system with many threads per core as MULTICORE
#
test_expect_success 'reload sched-simple with sched-PUs' '
	flux module reload -f sched-simple sched-PUs
'

test $(nproc) -gt 1 && test_set_prereq HAVE_MULTICORE

test_expect_success 'flux mini fails with usage message' '
	test_must_fail flux mini 2>usage.err &&
	grep -i usage: usage.err
'
test_expect_success 'flux mini submit + flux job attach works' '
	jobid=$(flux mini submit hostname) &&
	flux job attach $jobid
'
test_expect_success 'flux mini run works' '
	flux mini run hostname >run.out &&
	hostname >run.exp &&
	test_cmp run.exp run.out
'
test_expect_success 'flux mini run --ntasks=2 works' '
	flux mini run --ntasks=2 hostname >run2.out &&
	(hostname;hostname) >run2.exp &&
	test_cmp run2.exp run2.out
'
test_expect_success 'flux mini run --ntasks=2 --label-io works' '
	flux mini run --ntasks=2 --label-io echo Hi |sort >run2l.out &&
	cat >run2l.exp <<-EOT &&
	0: Hi
	1: Hi
	EOT
	test_cmp run2l.exp run2l.out
'
test_expect_success HAVE_MULTICORE 'flux mini submit --ntasks=2 --cores-per-task=2 works' '
	jobid=$(flux mini submit --ntasks=2 --cores-per-task=2 hostname) &&
	flux job attach $jobid
'
test_expect_success 'flux mini run --ntasks=2 --nodes=2 works' '
	flux mini run --ntasks=2 --nodes=2 hostname
'
test_expect_success 'flux mini run --ntasks=1 --nodes=2 fails' '
	test_must_fail flux mini run --ntasks=1 --nodes=2 hostname \
		2>run1n2N.err &&
	grep -i "node count must not be greater than task count" run1n2N.err
'
test_expect_success 'flux mini run (default ntasks) --nodes=2 fails' '
	test_must_fail flux mini run --nodes=2 hostname 2>run2N.err &&
	grep -i "node count must not be greater than task count" run2N.err
'
test_expect_success 'flux mini submit --priority=6 works' '
	jobid=$(flux mini submit --priority=6 hostname) &&
	flux job eventlog $jobid | grep submit | grep priority=6
'
test_expect_success 'flux mini submit --flags debug works' '
	jobid=$(flux mini submit --flags debug hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=2
'
test_expect_success 'flux mini submit --flags waitable works' '
	jobid=$(flux mini submit --flags waitable hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=4
'
test_expect_success 'flux mini submit --flags debug,waitable works' '
	jobid=$(flux mini submit --flags debug,waitable hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=6
'
test_expect_success 'flux mini run -v produces jobid on stderr' '
	flux mini run -v hostname 2>v.err &&
	grep jobid: v.err
'
test_expect_success 'flux mini run -vv produces job events on stderr' '
	flux mini run -vv hostname 2>vv.err &&
	grep submit vv.err
'
test_expect_success 'flux mini run -vvv produces exec events on stderr' '
	flux mini run -vvv hostname 2>vvv.err &&
	grep complete vvv.err
'
test_expect_success HAVE_JQ 'flux mini submit --time-limit=5d works' '
	flux mini submit --dry-run --time-limit=5d hostname >t5d.out &&
	test $(jq ".attributes.system.duration" t5d.out) = "432000"
'
test_expect_success HAVE_JQ 'flux mini submit --time-limit=4h works' '
	flux mini submit --dry-run --time-limit=4h hostname >t4h.out &&
	test $(jq ".attributes.system.duration" t4h.out) = "14400"
'
test_expect_success HAVE_JQ 'flux mini submit --time-limit=1m works' '
	flux mini submit --dry-run --time-limit=5m hostname >t5m.out &&
	test $(jq ".attributes.system.duration" t5m.out) = "300"
'
test_expect_success HAVE_JQ 'flux mini submit -t5s works' '
	flux mini submit --dry-run -t5s hostname >t5s.out &&
	test $(jq ".attributes.system.duration" t5s.out) = "5"
'
test_expect_success HAVE_JQ 'flux mini submit -t5 works' '
	flux mini submit --dry-run -t5 hostname >t5.out &&
	test $(jq ".attributes.system.duration" t5.out) = "5"
'
test_expect_success 'flux mini submit --time-limit=00:30 fails' '
	test_must_fail flux mini submit --time-limit=00:30 hostname 2>st.err &&
	grep -i "invalid Flux standard duration" st.err
'
test_expect_success 'flux mini submit --time-limit=4-00:30:00 fails' '
	test_must_fail flux mini submit --time-limit=4-00:30:00 hostname 2>st2.err &&
	grep -i "invalid Flux standard duration" st2.err
'

test_expect_success HAVE_JQ 'flux mini submit --setattr works' '
	flux mini submit --dry-run \
		--setattr user.meep=false \
		--setattr user.foo=\"xxx\" \
		--setattr user.foo2=yyy \
		--setattr system.bar=42 hostname >attr.out &&
	test $(jq ".attributes.user.meep" attr.out) = "false" &&
	test $(jq ".attributes.user.foo" attr.out) = "\"xxx\"" &&
	test $(jq ".attributes.user.foo2" attr.out) = "\"yyy\"" &&
	test $(jq ".attributes.system.bar" attr.out) = "42"
'
test_expect_success 'flux mini submit --setattr fails without value' '
	test_expect_code 1 \
		flux mini submit --dry-run \
		--setattr foo \
		hostname >attr_fail.out 2>&1 &&
	test_debug "cat attr_fail.out" &&
	grep "Missing value for attr foo" attr_fail.out
'
test_expect_success HAVE_JQ 'flux mini submit --setopt works' '
	flux mini submit --dry-run \
		--setopt foo=true \
		--setopt baz \
		--setopt bar.baz=42 hostname >opt.out &&
	test $(jq ".attributes.system.shell.options.foo" opt.out) = "true" &&
	test $(jq ".attributes.system.shell.options.bar.baz" opt.out) = "42" &&
	test $(jq ".attributes.system.shell.options.baz" opt.out) = "1"
'
test_expect_success HAVE_JQ 'flux mini submit --output passes through to shell' '
	flux mini submit --dry-run --output=my/file hostname >output.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.type" output.out) = "\"file\"" &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" output.out) = "\"my/file\""
'
test_expect_success HAVE_JQ 'flux mini submit --error passes through to shell' '
	flux mini submit --dry-run --error=/my/error hostname >error.out &&
	test $(jq ".attributes.system.shell.options.output.stderr.type" error.out) = "\"file\"" &&
	test $(jq ".attributes.system.shell.options.output.stderr.path" error.out) = "\"/my/error\""
'
test_expect_success HAVE_JQ 'flux mini submit --label-io --output passes through to shell' '
	flux mini submit --dry-run \
		--label-io --output=foo hostname >labout.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.label" labout.out) = "true" &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" labout.out) = "\"foo\""
'
test_expect_success HAVE_JQ 'flux mini submit --output id mustache passes through to shell' '
	flux mini submit --dry-run --output=foo.{{id}} hostname >musid.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" musid.out) = "\"foo.{{id}}\""
'
test_expect_success HAVE_JQ 'flux mini submit command arguments work' '
	flux mini submit --dry-run a b c >args.out &&
	test $(jq ".tasks[0].command[0]" args.out) = "\"a\"" &&
	test $(jq ".tasks[0].command[1]" args.out) = "\"b\"" &&
	test $(jq ".tasks[0].command[2]" args.out) = "\"c\""
'
test_expect_success HAVE_JQ 'flux mini submit --gpus-per-task adds gpus to task slot' '
	flux mini submit --dry-run -g2 hostname >gpu.out &&
	test $(jq ".resources[0].with[1].type" gpu.out) = "\"gpu\"" &&
	test $(jq ".resources[0].with[1].count" gpu.out) = "2"
'
test_expect_success HAVE_JQ 'flux mini --job-name works' '
	flux mini submit --dry-run --job-name=foobar hostname >name.out &&
	test $(jq ".attributes.system.job.name" name.out) = "\"foobar\""
'

test_expect_success HAVE_JQ 'flux mini submit --hw-threads-per-core adds PUs as children of core' '
	flux mini submit --dry-run -V2 -T2 hostname > hw-thread.out &&
	test $(jq ".resources[0].type" hw-thread.out) = "\"slot\"" &&
	test $(jq ".resources[0].with[0].type" hw-thread.out) = "\"core\"" &&
	test $(jq ".resources[0].with[0].with[0].type" hw-thread.out) = "\"PU\"" &&
	test $(jq ".resources[0].with[0].with[0].count" hw-thread.out) = "2"
'

test_expect_success HAVE_JQ 'flux mini submit --hw-threads-per-core works when node is specified' '
	flux mini submit --dry-run -V2 -N2 -n2 -T1 hostname > hw-thread.out  &&
	test $(jq ".resources[0].type" hw-thread.out) = "\"node\"" &&
	test $(jq ".resources[0].with[0].type" hw-thread.out) = "\"slot\"" &&
	test $(jq ".resources[0].with[0].with[0].type" hw-thread.out) = "\"core\"" &&
	test $(jq ".resources[0].with[0].with[0].with[0].type" hw-thread.out) = "\"PU\""
'

test_expect_success HAVE_JQ 'flux mini submit --hw-threads-per-core works in concert with --gpus-per-task' '
	flux mini submit --dry-run -V2 -N2 -n2 -T1 -g1 hostname > hw-thread.out  &&
	test $(jq ".resources[0].type" hw-thread.out) = "\"node\"" &&
	test $(jq ".resources[0].with[0].type" hw-thread.out) = "\"slot\"" &&
	test $(jq ".resources[0].with[0].with[0].type" hw-thread.out) = "\"core\"" &&
	test $(jq ".resources[0].with[0].with[1].type" hw-thread.out) = "\"gpu\"" &&
	test $(jq ".resources[0].with[0].with[0].with[0].type" hw-thread.out) = "\"PU\""
'

test_done
