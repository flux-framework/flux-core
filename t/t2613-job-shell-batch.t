#!/bin/sh
#
test_description='Test flux-shell per-resource and batch support'

. `dirname $0`/sharness.sh

test_under_flux 2 job

test_expect_success 'flux-shell: bails on invalid per-resource' '
	test_expect_code 1 flux run -o per-resource.type=foo hostname
'
test_expect_success 'flux-shell: bails on invalid per-resource count' '
	test_expect_code 1 flux run -o per-resource.count=0 hostname
'
test_expect_success 'flux-shell: bails on invalid per-resource object' '
	test_expect_code 1 flux run -o per-resource.blah hostname
'
test_expect_success 'flux-shell: bails on invalid batch object' '
	test_expect_code 1 flux run --setattr system.batch.broker-opts=5 \
		hostname
'
test_expect_success 'flux-shell: per-resource with count works' '
	flux run \
	        -o per-resource.type=core \
	        -o per-resource.count=4 \
	        echo foo > 4-per-core.out 2>4-per-core.err &&
	test_debug "grep . 4-per-core.*" &&
	cat <<-EOF >4-per-core.expected &&
	foo
	foo
	foo
	foo
	EOF
	test_cmp 4-per-core.expected 4-per-core.out
'
test_expect_success 'flux-shell: per-resource type=node works' '
	ncores=$(flux resource list -s up -no {ncores}) &&
	flux run -n ${ncores} \
	        -o per-resource.type=node \
	        -o per-resource.count=1 \
	        echo foo > per-node.out 2>per-node.err &&
	test_debug "grep . per-node.*" &&
	cat <<-EOF >per-node.expected &&
	foo
	foo
	EOF
	test_cmp per-node.expected per-node.out
'
test_expect_success 'flux-shell: historical batch jobspec still work' '
	for spec in $SHARNESS_TEST_SRCDIR/batch/jobspec/*.json; do
		input=$(basename $spec) &&
		cat $spec |
		    jq -S ".attributes.system.environment.PATH=\"$PATH\"" |
		    jq -S ".attributes.system.environment.PYTHONPATH=\"$PYTHONPATH\"" |
		    jq -S ".attributes.system.environment.HOME=\"$HOME\"" |
		    jq -S ".attributes.system.cwd=\"$(pwd)\"" \
		    >$input &&
		flux job submit --flags=waitable $input
	done &&
	test_when_finished "flux dmesg -H" &&
	flux job attach -vEX $(flux job last) &&
	flux job wait --all --verbose
'
test_done
