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
test_under_flux 1 job

flux setattr log-stderr-level 1

JOBSPEC=${SHARNESS_TEST_SRCDIR}/jobspec
Y2J="flux python ${JOBSPEC}/y2j.py"

test_expect_success 'flux job-frobnicator works' '
	flux mini run --env=-* --dry-run hostname \
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
test_expect_success HAVE_JQ 'job-frobnicator: defaults plugin does things' '
	flux mini run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		| jq  -e ".data.attributes.system.duration == 1800"
'
test_expect_success HAVE_JQ 'job-frobnicator: defaults plugin does not do things' '
	flux mini run --env=-* --dry-run -t 1h hostname \
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
test_expect_success HAVE_JQ 'job-frobnicator sets default queue duration' '
	flux mini run --env=-* --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> queue-debug.out &&
	jq -e ".data.attributes.system.queue == \"debug\"" < queue-debug.out &&
	jq -e ".data.attributes.system.duration == 3600"   < queue-debug.out
'
test_expect_success HAVE_JQ 'job-frobnicator sets specified queue duration' '
	flux mini run --env=-* --setattr queue=batch --dry-run hostname \
		| flux job-frobnicator --jobspec-only --plugins=defaults \
		> queue-batch.out &&
	jq -e ".data.attributes.system.queue == \"batch\"" < queue-batch.out &&
	jq -e ".data.attributes.system.duration == 28800"  < queue-batch.out
'
test_expect_success 'job-frobnicator errors on a missing queue config' '
	cat <<-EOF >conf.d/conf.toml &&
	[policy.jobspec.defaults.system]
	queue = "debug"
	EOF
	flux config reload &&
	flux mini run --env=-* --dry-run hostname \
	   | test_expect_code 1 \
	       flux job-frobnicator --jobspec-only --plugins=defaults \
	       >bad1.out 2>&1 &&
	grep "default queue.*debug.*must be in" bad1.out
'
test_expect_success 'job-frobnicator errors on an invalid queue config' '
	cat <<-EOF >conf.d/conf.toml &&
	queues = 42
	[policy.jobspec.defaults.system]
	queue = "debug"
	EOF
	flux config reload &&
	flux mini run --env=-* --dry-run hostname \
	   | test_expect_code 1 \
	       flux job-frobnicator --jobspec-only --plugins=defaults \
	       >bad2.out 2>&1 &&
	grep "queues must be a table" bad2.out
'

test_done
