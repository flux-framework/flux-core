#!/bin/sh

test_description='flux-mini batch specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
test_under_flux 4 job

flux setattr log-stderr-level 1

test_expect_success 'create generic test batch script' '
	cat <<-EOF >batch-script.sh
	#!/bin/sh
	ncores=\$(flux resource list -s all -no {ncores})
	nnodes=\$(flux resource list -s all -no {nnodes})
	printf "size=%d nodes=%d\n" \$(flux getattr size) \$nnodes
	flux mini run -n \$ncores hostname
	EOF
'

test_expect_success HAVE_JQ 'flux-mini batch copies script into jobspec' '
	flux mini batch -n1 --dry-run batch-script.sh | \
		jq -j .attributes.system.batch.script > script.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success HAVE_JQ 'flux-mini batch takes a script on stdin' '
	flux mini batch -n1 --dry-run < batch-script.sh | \
		jq -j .attributes.system.batch.script > script-stdin.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success HAVE_JQ 'flux mini batch --wrap option works' '
	flux mini batch -n1 --dry-run --wrap foo bar baz | \
		jq -j .attributes.system.batch.script >script-wrap.out &&
	cat <<-EOF >script-wrap.expected &&
	#!/bin/sh
	foo bar baz
	EOF
	test_cmp script-wrap.expected script-wrap.out
'
test_expect_success HAVE_JQ 'flux mini batch --wrap option works on stdin' '
	printf "foo\nbar\nbaz\n" | \
	    flux mini batch -n1 --dry-run --wrap | \
		jq -j .attributes.system.batch.script >stdin-wrap.out &&
	cat <<-EOF >stdin-wrap.expected &&
	#!/bin/sh
	foo
	bar
	baz
	EOF
	test_cmp stdin-wrap.expected stdin-wrap.out
'
test_expect_success 'flux-mini batch fails for binary file' '
	test_expect_code 1 flux mini batch -n1 $(which hostname)
'
test_expect_success 'flux-mini batch fails for file without she-bang' '
	cat <<-EOF >invalid-script.sh &&
	flux mini run hostname
	EOF
	test_expect_code 1 flux mini batch -n1 invalid-script.sh
'
test_expect_success 'flux-mini batch: submit a series of jobs' '
	id1=$(flux mini batch --flags=waitable -n1 batch-script.sh | flux job id) &&
	id2=$(flux mini batch --flags=waitable -n4 batch-script.sh | flux job id) &&
	id3=$(flux mini batch --flags=waitable -N2 -n4 batch-script.sh | flux job id) &&
	flux job wait --all
'
test_expect_success 'flux-mini batch: job results are expected' '
	test_debug "grep . flux-*.out" &&
	grep "size=1 nodes=1" flux-${id1}.out &&
	grep "size=1 nodes=1" flux-${id2}.out &&
	grep "size=2 nodes=2" flux-${id3}.out
'
test_expect_success 'flux-mini batch: --output=kvs directs output to kvs' '
	id=$(flux mini batch -n1 --flags=waitable --output=kvs batch-script.sh) &&
	flux job attach $id > kvs-output.log 2>&1 &&
	test_debug "cat kvs-output.log" &&
	grep "size=1 nodes=1" kvs-output.log
'
test_expect_success 'flux-mini batch: --broker-opts works' '
	id=$(flux mini batch -n1 --flags=waitable \
	     --broker-opts=-v batch-script.sh | flux job id) &&
	id2=$(flux mini batch -n1 --flags=waitable \
	     --broker-opts=-v,-H5 batch-script.sh | flux job id) &&
	flux job wait $id &&
	test_debug "cat flux-${id}.out" &&
	grep "boot: rank=0 size=1" flux-${id}.out &&
	flux job wait $id2 &&
	grep "boot: rank=0 size=1" flux-${id2}.out &&
	grep "heartbeat: T=5.0s" flux-${id2}.out
'

test_expect_success 'create hardware-thread batch script' '
	cat <<-EOF >hardware-thread-batch-script.sh
	#!/bin/sh
	flux mini run -V2 --hw-threads-per-core 2 hostname
	EOF
'

test_expect_success 'flux-mini batch: hardware-thread jobs can be submitted but fail to run' '
	hwt_id1=$(flux mini batch --flags=waitable -n1 hardware-thread-batch-script.sh) &&
	hwt_id2=$(flux mini batch --flags=waitable -n4 hardware-thread-batch-script.sh) &&
	test_must_fail flux job wait --all
'

test_expect_success 'flux-mini batch: jobs failed to run because simple-sched does not support hardware-thread resource' '
	grep "Unsupported resource type" flux-${hwt_id1}.out &&
	grep "Unsupported resource type" flux-${hwt_id2}.out
'

test_expect_success 'create version batch script' '
	cat <<-EOF >version-batch-script.sh
	#!/bin/sh
	flux mini run hostname
	EOF
'

test_expect_success HAVE_JQ 'flux-mini batch: Jobs of both versions can be assembled from batch script' '
	flux mini batch --dry-run --flags=waitable -n1 -V1 hardware-thread-batch-script.sh > v1.out &&
	flux mini batch --dry-run --flags=waitable -n1 -V2 hardware-thread-batch-script.sh > v2.out &&
	test $(jq ".version" v1.out) = 1 &&
	test $(jq ".version" v2.out) = 2
'

test_done
