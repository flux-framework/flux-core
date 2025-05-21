#!/bin/sh

test_description='flux batch specific tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
test_under_flux 4 job

flux setattr log-stderr-level 1

NCORES=$(flux kvs get resource.R | flux R decode --count=core)
test ${NCORES} -gt 4 && test_set_prereq MULTICORE

test_expect_success 'create generic test batch script' '
	cat <<-EOF >batch-script.sh
	#!/bin/sh
	ncores=\$(flux resource list -s all -no {ncores})
	nnodes=\$(flux resource list -s all -no {nnodes})
	printf "size=%d nodes=%d\n" \$(flux getattr size) \$nnodes
	flux run -n \$ncores hostname
	EOF
'
test_expect_success 'flux batch copies script into jobspec' '
	flux batch -n1 --dry-run batch-script.sh | \
		jq -j .attributes.system.files.script.data > script.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success 'flux batch takes a script on stdin' '
	flux batch -n1 --dry-run < batch-script.sh | \
		jq -j .attributes.system.files.script.data > script-stdin.sh &&
	test_cmp batch-script.sh script.sh
'
test_expect_success 'flux batch --wrap option works' '
	flux batch -n1 --dry-run --wrap foo bar baz | \
		jq -j .attributes.system.files.script.data >script-wrap.out &&
	cat <<-EOF >script-wrap.expected &&
	#!/bin/sh
	foo bar baz
	EOF
	test_cmp script-wrap.expected script-wrap.out
'
test_expect_success 'flux batch --wrap option works with --' '
	flux batch -n1 --dry-run --wrap -- foo --n=2 | \
		jq -j .attributes.system.files.script.data >script-wrap.out &&
	cat <<-EOF >script-wrap.expected &&
	#!/bin/sh
	foo --n=2
	EOF
	test_cmp script-wrap.expected script-wrap.out
'
test_expect_success 'flux batch --wrap option works on stdin' '
	printf "foo\nbar\nbaz\n" | \
	    flux batch -n1 --dry-run --wrap | \
		jq -j .attributes.system.files.script.data >stdin-wrap.out &&
	cat <<-EOF >stdin-wrap.expected &&
	#!/bin/sh
	foo
	bar
	baz
	EOF
	test_cmp stdin-wrap.expected stdin-wrap.out
'
test_expect_success 'flux batch disables exit-timeout by default' '
	flux batch -n1 --wrap --dry-run true | \
	    jq -e ".attributes.system.shell.options[\"exit-timeout\"] == \"none\""
'
test_expect_success 'flux batch allows exit-timeout to be overridden' '
	flux batch -n1 -o exit-timeout=30s --wrap --dry-run true | \
	    jq -e ".attributes.system.shell.options[\"exit-timeout\"] == \"30s\""
'
test_expect_success 'flux batch fails for binary file' '
	test_expect_code 1 flux batch -n1 $(which hostname)
'
test_expect_success 'flux batch fails for file without she-bang' '
	cat <<-EOF >invalid-script.sh &&
	flux run hostname
	EOF
	test_expect_code 1 flux batch -n1 invalid-script.sh
'
test_expect_success 'flux batch fails if -N > -n' '
	test_expect_code 1 flux batch -N4 -n1 --wrap hostname
'
test_expect_success 'flux batch -N2 requests 2 nodes exclusively' '
	flux batch -N2 --wrap --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success 'flux batch --exclusive works' '
	flux batch -N1 -n1 --exclusive --wrap --dry-run hostname | \
		jq -S ".resources[0]" | \
		jq -e ".type == \"node\" and .exclusive"
'
test_expect_success NO_ASAN 'flux batch: submit a series of jobs' '
	id1=$(flux batch --flags=waitable -n1 batch-script.sh) &&
	id2=$(flux batch --flags=waitable -n4 batch-script.sh) &&
	id3=$(flux batch --flags=waitable -N2 -n4 batch-script.sh) &&
	flux resource list &&
	flux jobs &&
	id4=$(flux batch --flags=waitable -N2 -n2 -x batch-script.sh) &&
	id5=$(flux batch --flags=waitable -N2 batch-script.sh) &&
	run_timeout 180 flux job wait --verbose --all
'
test_expect_success NO_ASAN 'flux batch: job results are expected' '
	test_debug "grep . flux-*.out" &&
	grep "size=1 nodes=1" flux-${id1}.out &&
	grep "size=1 nodes=1" flux-${id2}.out &&
	grep "size=2 nodes=2" flux-${id3}.out &&
	grep "size=2 nodes=2" flux-${id4}.out &&
	grep "size=2 nodes=2" flux-${id5}.out
'
test_expect_success MULTICORE 'flux batch: exclusive flag worked' '
	test $(flux job info ${id4} R | flux R decode --count=core) -gt 2 &&
	test $(flux job info ${id5} R | flux R decode --count=core) -gt 2
'

test_expect_success 'flux batch: --output=kvs directs output to kvs' '
	id=$(flux batch -n1 --flags=waitable --output=kvs batch-script.sh) &&
	run_timeout 180 flux job attach $id > kvs-output.log 2>&1 &&
	test_debug "cat kvs-output.log" &&
	grep "size=1 nodes=1" kvs-output.log
'
test_expect_success 'flux batch: --broker-opts works' '
	id=$(flux batch -n1 --flags=waitable \
	     --broker-opts=-v batch-script.sh) &&
	id2=$(flux batch -n1 --flags=waitable \
	     --broker-opts=-v,-v batch-script.sh) &&
	run_timeout 180 flux job wait $id &&
	test_debug "cat flux-${id}.out" &&
	grep "boot: rank=0 size=1" flux-${id}.out &&
	run_timeout 180 flux job wait $id2 &&
	grep "boot: rank=0 size=1" flux-${id2}.out &&
	grep "entering event loop" flux-${id2}.out
'
test_expect_success 'flux batch: critical-ranks attr is set on all ranks' '
	id=$(flux batch -N4 \
		--output=critical-ranks.out \
		--error=critical-ranks.err \
		--broker-opts=-Stbon.fanout=2 \
		--wrap flux exec flux getattr broker.critical-ranks) &&
	flux job status $id &&
	test_debug "cat critical-ranks.out" &&
	test_debug "cat critical-ranks.err" &&
	cat <<-EOF >critical-ranks.expected &&
	0-1
	0-1
	0-1
	0-1
	EOF
	test_cmp critical-ranks.expected critical-ranks.out
'
test_expect_success 'flux batch: user can set broker.critical-ranks' '
	id=$(flux batch -N4 \
		--output=critical-ranks2.out \
		--error=critical-ranks2.err \
		--broker-opts=-Sbroker.critical-ranks=0 \
		--wrap flux exec flux getattr broker.critical-ranks) &&
	flux job status $id &&
	test_debug "cat critical-ranks2.out" &&
	test_debug "cat critical-ranks2.err" &&
	cat <<-EOF >critical-ranks2.expected &&
	0
	0
	0
	0
	EOF
	test_cmp critical-ranks2.expected critical-ranks2.out
'
test_expect_success 'flux batch: flux can bootstrap without broker.mapping' '
	id=$(flux batch -N4 -o pmi-simple.nomap \
		--wrap flux resource info) &&
	flux job status $id
'
test_expect_success 'flux batch: sets mpi=none by default' '
	flux batch -N1 --dry-run --wrap hostname | \
		jq -e ".attributes.system.shell.options.mpi = \"none\""
'
test_expect_success 'flux batch: mpi option can be overridden' '
	flux batch -o mpi=foo -N1 --dry-run --wrap hostname | \
		jq -e ".attributes.system.shell.options.mpi = \"foo\""
'
test_expect_success 'flux batch: MPI env vars are not set in batch script' '
	unset OMPI_MCA_pmix &&
	id=$(flux batch -N1 --output=envtest.out --wrap printenv) &&
	flux job status $id &&
	test_must_fail grep OMPI_MCA_pmix envtest.out
'
test_expect_success 'flux batch: --dump works' '
	id=$(flux batch -N1 --dump \
		--flags=waitable --wrap true) &&
	run_timeout 180 flux job wait $id &&
	tar tvf flux-${id}-dump.tgz
'
test_expect_success 'flux batch: --dump=FILE works' '
	id=$(flux batch -N1 --dump=testdump.tgz \
		--flags=waitable --wrap true) &&
	run_timeout 180 flux job wait $id &&
	tar tvf testdump.tgz
'
test_expect_success 'flux batch: --dump=FILE works with mustache' '
	id=$(flux batch -N1 --dump=testdump-{{id}}.tgz \
		--flags=waitable --wrap true) &&
	run_timeout 180 flux job wait $id &&
	tar tvf testdump-${id}.tgz
'
test_expect_success 'flux batch: supports directives in script' '
	cat <<-EOF >directives.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --job-name=test-name
	flux resource list
	EOF
	flux batch --dry-run directives.sh > directives.json &&
	jq -e ".attributes.system.job.name == \"test-name\"" < directives.json
'
test_expect_success 'flux batch: cmdline overrides directives' '
	cat <<-EOF >directives2.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --job-name=test-name
	flux resource list
	EOF
	flux batch --dry-run --job-name=foo directives2.sh \
	  > directives2.json &&
	jq -e ".attributes.system.job.name == \"foo\"" < directives2.json
'
test_expect_success 'flux batch: bad argument in directive is caught' '
	cat <<-EOF >directives3.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --bad-arg
	date; hostname
	EOF
	test_must_fail flux batch --dry-run directives3.sh >d3.out 2>&1 &&
	test_debug "cat d3.out" &&
	grep "argument parsing failed at directives3.sh line 3" d3.out
'
test_expect_success 'flux batch: shell parsing error is caught' '
	cat <<-EOF >directives4.sh &&
	#!/bin/sh
	# flux: --job-name=" name
	date; hostname
	EOF
	test_must_fail flux batch --dry-run directives4.sh >d4.out 2>&1 &&
	test_debug "cat d4.out" &&
	grep "directives4.sh: line 2" d4.out
'
test_expect_success 'flux batch: file can be added via directives' '
	cat <<-EOF >directives5.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --add-file=foo="""
	# flux: This is a test file
	# flux: """
	cat \$FLUX_JOB_TMPDIR/foo
	EOF
	flux batch --dry-run directives5.sh >d5.json &&
	jq -e ".attributes.system.files.foo.data == \"This is a test file\n\"" \
		<d5.json
'
test_expect_success 'flux batch: multiline --add-file requires name=' '
	cat <<-EOF >directives6.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --add-file="""
	# flux: This is a test file
	# flux: """
	cat \$FLUX_JOB_TMPDIR/foo
	EOF
	test_must_fail flux batch --dry-run directives6.sh >d6.out 2>&1 &&
	grep "file name missing" d6.out
'
test_expect_success 'flux batch: hidden --add-brokers option requires -N' '
	test_must_fail flux batch -n4 --add-brokers=1 --dry-run --wrap true
'
test_expect_success 'flux batch: hidden --add-brokers option works' '
	cat <<-EOF >add-brokers.sh &&
	#!/bin/sh
	# flux: -N4 --output=add-brokers.out --error=add-brokers.err
	# flux: --conf=tbon.topo=kary:2
	# flux: --add-brokers=2
	#
	flux resource list -s free -no {nnodes}
	flux resource status -s exclude -no {ranks}
	flux exec -lr 1-2 flux getattr tbon.parent-endpoint \
	    | sed s#://.*## | sort
	flux run -N4 true
	EOF
	cat <<-EOF >add-brokers.expected &&
	4
	1-2
	1: ipc
	2: ipc
	EOF
	id=$(flux batch add-brokers.sh) &&
	flux job wait-event $id clean &&
	test_debug "cat add-brokers.err" &&
	test_debug "cat add-brokers.out" &&
	test_cmp add-brokers.expected add-brokers.out
'
test_done
