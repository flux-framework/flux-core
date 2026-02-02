#!/bin/sh
test_description='Test job frobnicator command'

. $(dirname $0)/sharness.sh

#  Setup a config dir for this test_under_flux
export FLUX_CONF_DIR=$(pwd)/conf.d
mkdir -p conf.d
cat <<EOF >conf.d/conf.toml
[policy.jobspec.defaults.system]
duration = "30m"
EOF
test_under_flux 1 job -Slog-stderr-level=1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"

test_expect_success 'flux job-frobnicator works' '
	flux run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only
'
test_expect_success 'flux job-frobnicator --list-plugins works' '
	flux job-frobnicator --list-plugins >list-plugins.output 2>&1 &&
	test_debug "cat list-plugins.output" &&
	grep defaults list-plugins.output
'
test_expect_success 'flux job-frobnicator --help can show help for plugins' '
	cat <<-EOF >test-plugin.py &&
	from flux.job.frobnicator import FrobnicatorPlugin
	class Frobnicator(FrobnicatorPlugin):
	    def __init__(self, parser):
	        self.test = False
	        parser.add_argument("--test", action="store_true")

	    def configure(self, args):
	        self.test = args.test

	    def frob(self, jobspec, user, urgency, flags):
	        pass
	EOF
	flux job-frobnicator --plugins=./test-plugin.py --help >help.out 2>&1 &&
	test_debug "cat help.out" &&
	grep "Options provided by plugins" help.out &&
	grep "\-\-test" help.out
'
test_expect_success 'flux job-frobnicator errors on invalid plugin' '
	test_expect_code 1 flux job-frobnicator --plugin=foo </dev/null &&
	test_expect_code 1 flux job-frobnicator --plugin=/tmp </dev/null
'
test_expect_success 'flux job-frobnicator exits on eof' '
	flux job-frobnicator --plugins=defaults </dev/null
'
test_expect_success 'flux job-frobnicator exits with error on bad input' '
	echo {} | test_expect_code 1 flux job-frobnicator --plugins=defaults
'
test_expect_success 'job-frobnicator: all valid jobspecs accepted' '
	for f in ${JOBSPEC}/valid/*; do
	    $Y2J <$f | flux job-frobnicator --jobspec-only --plugins=defaults
	done
'
test_expect_success 'job-frobnicator: defaults plugin does things' '
	flux run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		| jq  -e ".data.attributes.system.duration == 1800"
'
test_expect_success 'job-frobnicator: defaults plugin does not do things' '
	flux run --env=-* --dry-run -t 1h hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		| jq  -e ".data.attributes.system.duration == 3600"
'
test_expect_success 'configure queues with default durations' '
	cat <<-EOF >conf.d/conf.toml &&
	[policy.jobspec.defaults.system]
	queue = "debug"
	[queues.debug]
	policy.jobspec.defaults.system.duration = "1h"
	[queues.batch]
	policy.jobspec.defaults.system.duration = "8h"
	EOF
	flux config reload
'
test_expect_success 'job-frobnicator sets default queue duration' '
	flux run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> queue-debug.out &&
	jq -e ".data.attributes.system.queue == \"debug\"" < queue-debug.out &&
	jq -e ".data.attributes.system.duration == 3600"   < queue-debug.out
'
test_expect_success 'job-frobnicator sets specified queue duration' '
	flux run --env=-* --queue=batch --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> queue-batch.out &&
	jq ".data.attributes.system.queue" < queue-batch.out &&
	jq -e ".data.attributes.system.queue == \"batch\"" < queue-batch.out &&
	jq ".data.attributes.system.duration"  < queue-batch.out &&
	jq -e ".data.attributes.system.duration == 28800"  < queue-batch.out
'
test_expect_success 'add default duration with specific queue duration' '
	cat <<-EOF >conf.d/conf.toml &&
	[policy.jobspec.defaults.system]
	queue = "debug"
	duration = "2h"
	[queues.debug]
	[queues.batch]
	policy.jobspec.defaults.system.duration = "8h"
	EOF
	flux config reload
'
test_expect_success 'job-frobnicator overrides default duration with queue duration' '
	flux run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> default-debug.out &&
	flux run --env=-* --queue=debug --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> default-debug2.out &&
	flux run --env=-* --queue=batch --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> default-batch.out &&
	jq .data.attributes.system.duration < default-debug.out &&
	jq -e ".data.attributes.system.duration == 7200" < default-debug.out &&
	jq .data.attributes.system.duration < default-debug2.out &&
	jq -e ".data.attributes.system.duration == 7200" < default-debug2.out &&
	jq .data.attributes.system.duration < default-batch.out &&
	jq -e ".data.attributes.system.duration == 28800" < default-batch.out
'
test_expect_success 'configure queue constraints' '
	cat <<-EOF >conf.d/conf.toml &&
	[queues.debug]
	requires = [ "debug" ]
	EOF
	flux config reload
'
test_expect_success 'constraints plugin sets queue constraint' '
	flux run --env=-* --dry-run --queue=debug hostname \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	   > constraint-setqueue.out &&
	jq -e ".data.attributes.system.constraints.properties \
	    == [ \"debug\" ]" < constraint-setqueue.out
'
test_expect_success 'constraints plugin adds queue constraint' '
	flux run --env=-* --dry-run --requires=foo \
	  --queue=debug hostname \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	   > constraint-addqueue.out &&
	jq -e ".data.attributes.system.constraints.properties|any(.==\"debug\")"\
	   < constraint-addqueue.out &&
	jq -e ".data.attributes.system.constraints.properties|any(.==\"foo\")"\
	   < constraint-addqueue.out
'
test_expect_success 'constraints plugin works with no configured queues' '
	cp /dev/null conf.d/conf.toml &&
	flux config reload &&
	flux run --env=-* --dry-run hostname \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	> constraint-noqueue.out &&
	jq -e "has(\"data\")" <constraint-noqueue.out
'
test_expect_success 'constraints plugin works without requires' '
	cat >conf.d/conf.toml <<-EOT &&
	[queues.debug]
	EOT
	flux config reload &&
	flux run --env=-* --dry-run hostname \
           --queue=debug \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	> constraint-norequires.out &&
	jq -e "has(\"data\")" <constraint-norequires.out
'
test_expect_success 'frobnicator defaults are defaults,constraints' '
	cat <<-EOF >conf.d/conf.toml &&
	[policy]
	jobspec.defaults.system.queue = "debug"
	[queues.debug]
	requires = [ "debug" ]
	EOF
	flux config reload &&
	flux run --env=-* --dry-run hostname \
	   | flux job-frobnicator --jobspec-only \
	> defaultplugins.out &&
	jq -e ".data.attributes.system.queue == \"debug\"" \
	    <defaultplugins.out &&
	jq -e ".data.attributes.system.constraints.properties \
	    == [ \"debug\" ]" \
	    <defaultplugins.out
'
test_expect_success 'defaults plugin allows queues without default' '
	cat <<-EOF >conf.d/conf.toml &&
	[queues.debug]
	requires = [ "debug" ]
	EOF
	flux config reload &&
	flux run --env=-* --dry-run --queue=debug hostname \
	   | flux job-frobnicator --jobspec-only --plugins=defaults \
	    > nodefault.out &&
	jq -e "has(\"data\")" <nodefault.out
'
test_expect_success 'defaults plugin requires queue if configured' '
	cat <<-EOF >conf.d/conf.toml &&
	[queues.debug]
	requires = [ "debug" ]
	EOF
	flux config reload &&
	flux run --env=-* --dry-run hostname \
	   | flux job-frobnicator --jobspec-only --plugins=defaults \
	    > nodefaultnojob.out &&
	jq -e ".errstr == \"no queue specified\"" <nodefaultnojob.out
'

test_done
