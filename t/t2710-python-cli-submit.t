#!/bin/sh

test_description='Test flux submit command'

. $(dirname $0)/sharness.sh

test_under_flux 4

NCORES=$(flux resource list -no {ncores})
test ${NCORES} -gt 4 && test_set_prereq MULTICORE

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

flux setattr log-stderr-level 1

test_expect_success 'flux submit --dry-run works without Flux instance' '
	FLUX_URI=/no/such/path \
	    flux submit -n1 --dry-run hostname >test.json
'
test_expect_success 'flux submit fails with error message' '
	test_must_fail flux submit 2>usage.err &&
	grep "job command and arguments are missing" usage.err
'
test_expect_success 'flux submit + flux job attach works' '
	jobid=$(flux submit hostname) &&
	flux job attach $jobid
'
test_expect_success MULTICORE 'flux submit --ntasks=2 --cores-per-task=2 works' '
	jobid=$(flux submit --ntasks=2 --cores-per-task=2 hostname) &&
	flux job attach $jobid
'
test_expect_success 'flux submit --urgency=6 works' '
	jobid=$(flux submit --urgency=6 hostname) &&
	flux job eventlog $jobid | grep submit | grep urgency=6
'
test_expect_success 'flux submit --urgency special options works' '
	jobid=$(flux submit --urgency=default hostname) &&
	flux job eventlog $jobid | grep submit | grep urgency=16 &&
	jobid=$(flux submit --urgency=hold hostname) &&
	flux job eventlog $jobid | grep submit | grep urgency=0 &&
	jobid=$(flux submit --urgency=expedite hostname) &&
	flux job eventlog $jobid | grep submit | grep urgency=31
'
test_expect_success 'flux submit --flags debug works' '
	jobid=$(flux submit --flags debug hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=2
'
test_expect_success 'flux submit --flags waitable works' '
	jobid=$(flux submit --flags waitable hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=4
'
test_expect_success 'flux submit --flags debug,waitable works' '
	jobid=$(flux submit --flags debug,waitable hostname) &&
	flux job eventlog $jobid | grep submit | grep flags=6
'
test_expect_success 'flux submit --flags=novalidate works' '
	jobid=$(flux submit --flags novalidate /bin/true) &&
	flux job eventlog $jobid | grep submit | grep flags=8
'
test_expect_success 'flux submit with bad flags fails' '
	test_must_fail flux submit --flags notaflag /bin/true
'
test_expect_success 'flux submit --time-limit=5d works' '
	flux submit --dry-run --time-limit=5d hostname >t5d.out &&
	jq -e ".attributes.system.duration == 432000" < t5d.out
'
test_expect_success 'flux submit --time-limit=4h works' '
	flux submit --dry-run --time-limit=4h hostname >t4h.out &&
	jq -e ".attributes.system.duration == 14400" < t4h.out
'
test_expect_success 'flux submit --time-limit=1m works' '
	flux submit --dry-run --time-limit=5m hostname >t5m.out &&
	jq -e ".attributes.system.duration == 300" < t5m.out
'
test_expect_success 'flux submit -t5s works' '
	flux submit --dry-run -t5s hostname >t5s.out &&
	jq -e ".attributes.system.duration == 5" < t5s.out
'
test_expect_success 'flux submit -t5 sets 5m duration' '
	flux submit --dry-run -t5 hostname >t5.out &&
	jq -e ".attributes.system.duration == 300" < t5.out
'
test_expect_success 'flux submit --time-limit=00:30 fails' '
	test_must_fail flux submit --time-limit=00:30 hostname 2>st.err &&
	grep -i "invalid Flux standard duration" st.err
'
test_expect_success 'flux submit --time-limit=4-00:30:00 fails' '
	test_must_fail flux submit --time-limit=4-00:30:00 hostname 2>st2.err &&
	grep -i "invalid Flux standard duration" st2.err
'

test_expect_success 'flux submit --queue works' '
	flux submit --env=-* --dry-run --queue=batch hostname >queue.out && grep -i "batch" queue.out
'

test_expect_success 'flux submit --queue works' '
	flux submit --env=-* --dry-run -q debug hostname >queue2.out && grep -i "debug" queue2.out
'
test_expect_success 'flux submit --bank works' '
	flux submit --env=-* --dry-run --bank=mybank hostname >bank.out &&
	grep -i "mybank" bank.out
'
test_expect_success 'flux submit -B works' '
	flux submit --env=-* --dry-run -B mybank2 hostname >bank2.out &&
	grep -i "mybank2" bank2.out
'
test_expect_success 'flux submit --setattr works' '
	flux submit --env=-* --dry-run \
		--setattr user.meep=false \
		--setattr user.foo=\"xxx\" \
		--setattr user.foo2=yyy \
		--setattr foo \
		--setattr .test=a \
		--setattr test2=b \
		--setattr system.bar=42 hostname >attr.out &&
	jq -e ".attributes.user.meep == false" attr.out &&
	jq -e ".attributes.user.foo == \"xxx\"" attr.out &&
	jq -e ".attributes.user.foo2 == \"yyy\"" attr.out &&
	jq -e ".attributes.system.foo == 1" attr.out &&
	jq -e ".attributes.test == \"a\"" attr.out &&
	jq -e ".attributes.system.test2 == \"b\"" attr.out &&
	jq -e ".attributes.system.bar == 42" attr.out
'

test_expect_success 'flux submit --setattr=^ATTR=VAL works' '
	cat | jq -S . >attr.json <<-EOF &&
	[
	  { "foo":"value",
	    "bar": 42
	  },
	  { "foo":"value2",
	    "bar": null
	  }
	]
	EOF
	flux submit --dry-run \
		--setattr ^user.foo=attr.json \
		hostname | \
	    jq -S .attributes.user.foo > attrout.json &&
	test_cmp attr.json attrout.json
'
test_expect_success 'flux submit --setattr=^ detects bad JSON' '
	cat <<-EOF > bad.json &&
	[ { "foo":"value",
	    "bar": 42
	  },
	  { foo":"value2",
	    "bar": null
	  }
	]
	EOF
	test_expect_code 1 \
	    flux submit --dry-run \
	      --setattr ^user.foo=bad.json \
	      hostname > attrbadjson.out 2>&1 &&
	test_debug "cat attrbadjson.out" &&
	grep "ERROR: --setattr: bad.json:" attrbadjson.out &&
	test_expect_code 1 \
	    flux submit --dry-run \
	      --setattr ^user.foo=nosuchfile.json \
	      hostname > attrbadfile.out 2>&1 &&
	test_debug "cat attrbadfile.out" &&
	grep "ERROR:.*nosuchfile" attrbadfile.out
'
test_expect_success 'flux submit --setopt works' '
	flux submit --dry-run \
		--setopt foo=true \
		--setopt baz \
		--setopt bar.baz=42 hostname >opt.out &&
	test $(jq ".attributes.system.shell.options.foo" opt.out) = "true" &&
	test $(jq ".attributes.system.shell.options.bar.baz" opt.out) = "42" &&
	test $(jq ".attributes.system.shell.options.baz" opt.out) = "1"
'
test_expect_success 'flux submit --output passes through to shell' '
	flux submit --dry-run --output=my/file hostname >output.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.type" output.out) = "\"file\"" &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" output.out) = "\"my/file\""
'
test_expect_success 'flux submit --error passes through to shell' '
	flux submit --dry-run --error=/my/error hostname >error.out &&
	test $(jq ".attributes.system.shell.options.output.stderr.type" error.out) = "\"file\"" &&
	test $(jq ".attributes.system.shell.options.output.stderr.path" error.out) = "\"/my/error\""
'
test_expect_success 'flux submit --label-io --output passes through to shell' '
	flux submit --dry-run \
		--label-io --output=foo hostname >labout.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.label" labout.out) = "true" &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" labout.out) = "\"foo\""
'
test_expect_success 'flux submit --output id mustache passes through to shell' '
	flux submit --dry-run --output=foo.{{id}} hostname >musid.out &&
	test $(jq ".attributes.system.shell.options.output.stdout.path" musid.out) = "\"foo.{{id}}\""
'
test_expect_success 'flux submit --cwd passes through to jobspec' '
	flux submit --cwd=/foo/bar/baz --dry-run hostname > cwd.out &&
	jq -e ".attributes.system.cwd == \"/foo/bar/baz\"" < cwd.out
'
test_expect_success 'flux submit command arguments work' '
	flux submit --dry-run a b c >args.out &&
	test $(jq ".tasks[0].command[0]" args.out) = "\"a\"" &&
	test $(jq ".tasks[0].command[1]" args.out) = "\"b\"" &&
	test $(jq ".tasks[0].command[2]" args.out) = "\"c\""
'
test_expect_success 'flux submit --gpus-per-task adds gpus to task slot' '
	flux submit --dry-run -g2 hostname >gpu.out &&
	test $(jq ".resources[0].with[1].type" gpu.out) = "\"gpu\"" &&
	test $(jq ".resources[0].with[1].count" gpu.out) = "2"
'
test_expect_success 'flux submit --job-name works' '
	flux submit --dry-run --job-name=foobar hostname >name.out &&
	test $(jq ".attributes.system.job.name" name.out) = "\"foobar\""
'
test_expect_success 'flux submit --env=-*/--env-remove=* works' '
	flux submit --dry-run --env=-* hostname > no-env.out &&
	jq -e ".attributes.system.environment == {}" < no-env.out &&
	flux submit --dry-run --env-remove=* hostname > no-env2.out &&
	jq -e ".attributes.system.environment == {}" < no-env2.out
'
test_expect_success 'flux submit --env=VAR works' '
	FOO=bar flux submit --dry-run \
	    --env=-* --env FOO hostname >FOO-env.out &&
	jq -e ".attributes.system.environment == {\"FOO\": \"bar\"}" FOO-env.out
'
test_expect_success 'flux submit --env=PATTERN works' '
	FOO_ONE=bar FOO_TWO=baz flux submit --dry-run \
	    --env=-* --env="FOO_*" hostname >FOO-pattern-env.out &&
	jq -e ".attributes.system.environment == \
	    {\"FOO_ONE\": \"bar\", \"FOO_TWO\": \"baz\"}" FOO-pattern-env.out &&
	FOO_ONE=bar FOO_TWO=baz flux submit --dry-run \
	    --env=-* --env="/^FOO_.*/" hostname >FOO-pattern2-env.out &&
	jq -e ".attributes.system.environment == \
	    {\"FOO_ONE\": \"bar\", \"FOO_TWO\": \"baz\"}" FOO-pattern2-env.out

'
test_expect_success 'flux submit --env=VAR=VAL works' '
	flux submit --dry-run \
	    --env=-* --env PATH=/bin hostname >PATH-env.out &&
	jq -e ".attributes.system.environment == {\"PATH\": \"/bin\"}" PATH-env.out &&
	FOO=bar flux submit --dry-run \
	    --env=-* --env FOO=\$FOO:baz hostname >FOO-append.out &&
	jq -e ".attributes.system.environment == {\"FOO\": \"bar:baz\"}" FOO-append.out
'
test_expect_success 'flux submit --env-file works' '
	cat <<-EOF >envfile &&
	-*
	FOO=bar
	BAR=\${FOO}/baz
	EOF
	for arg in "--env=^envfile" "--env-file=envfile"; do
	  flux submit --dry-run ${arg} hostname >envfile.out &&
	  jq -e ".attributes.system.environment == \
	       {\"FOO\":\"bar\", \"BAR\":\"bar/baz\"}" envfile.out
	done
'
test_expect_success 'flux submit --cc works' '
	flux submit --cc=0-3 sh -c "echo \$FLUX_JOB_CC" >cc.jobids &&
	test_debug "cat cc.jobids" &&
	test $(wc -l < cc.jobids) -eq 4 &&
	for job in $(cat cc.jobids); do
		flux job attach $job
	done > cc.output &&
	sort cc.output > cc.output.sorted &&
	cat <<-EOF >cc.output.expected &&
	0
	1
	2
	3
	EOF
	test_cmp cc.output.expected cc.output.sorted
'
test_expect_success 'flux submit --cc substitutes {cc}' '
	flux submit --quiet --watch --cc=0-3 echo {cc} \
	    | sort >substitute-cc.out &&
	test_debug "cat substitute-cc.out" &&
	test $(wc -l < substitute-cc.out) -eq 4 &&
	cat <<-EOF >substitute-cc.expected &&
	0
	1
	2
	3
	EOF
	test_cmp substitute-cc.expected substitute-cc.out
'
test_expect_success 'flux submit does not substitute {} without --cc' '
	flux submit \
		--env=-* \
		--setattr=system.test={} \
		--dry-run true > nocc.json &&
	jq -e ".attributes.system.test == {}" < nocc.json
'
test_expect_success 'flux submit --tasks-per-node works' '
	flux submit \
		--env=-* \
		-N 2 \
		--tasks-per-node=2 \
		--dry-run true > ntasks-per-node.json &&
	jq -e \
	 ".attributes.system.shell.options.\"per-resource\".type == \"node\"" \
		< ntasks-per-node.json &&
	jq -e \
	 ".attributes.system.shell.options.\"per-resource\".count == 2" \
		< ntasks-per-node.json
'
test_expect_success 'flux submit --input=IDSET fails' '
	test_must_fail flux submit --input=0 hostname &&
	test_must_fail flux submit -n2 --input=0-1 hostname
'
test_expect_success 'flux submit: create test files ' '
	cat <<-EOF >file.txt &&
	This is a test file
	EOF
	dd if=/dev/urandom of=file.binary bs=64 count=1 &&
	touch file.empty
'
for file in file.txt file.binary file.empty; do
	test_expect_success \
		"flux submit --add-file works for ${file#file.} file" '
		flux submit -n1 --watch --add-file=${file} \
			cp {{tmpdir}}/${file} ${file}.result &&
		test_cmp ${file} ${file}.result
	'
done
test_expect_success 'flux submit --add-file=name=file works' '
	flux submit -n1 --watch --add-file=myfile=file.txt \
		cp {{tmpdir}}/myfile . &&
	test_cmp file.txt myfile
'
test_expect_success 'flux submit --add-file complains for non-regular files' '
	test_must_fail flux submit -n1 --add-file=/tmp hostname
'
test_expect_success 'flux submit --add-file complains for missing files' '
	test_must_fail flux submit -n1 --add-file=doesnotexist hostname
'
test_done
