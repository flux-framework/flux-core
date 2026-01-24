#!/bin/sh
#
test_description='Test flux-shell output flow control'

. `dirname $0`/sharness.sh

test_under_flux 4 job

test_expect_success 'negative output.client.lwm fails' '
	test_must_fail flux run -o output.client.lwm=-1 true
'
test_expect_success 'negative output.client.hwm fails' '
	test_must_fail flux run -o output.client.hwm=-1 true
'
test_expect_success 'output.client.hwm = output.client.lwm fails' '
	test_must_fail flux run \
	    -o output.client.lwm=1 -o output.client.hwm=1 true
'
test_expect_success 'output.client.hwm < output.client.lwm fails' '
	test_must_fail flux run \
	    -o output.client.lwm=2 -o output.client.hwm=1 true
'
test_expect_success 'run a job that generates stdout on all ranks' '
	flux run -N4 -l -o verbose \
	    flux lptest >simple.out 2>simple.err
'
test_expect_success 'now do stdout with a tiny output.client.hwm/lwm' '
	flux run -N4 -l -o verbose -o \
	    output.client.lwm=1 -o output.client.hwm=10 \
	    flux lptest >simple_flow.out 2>simple_flow.err
'
test_expect_success 'flow control stop and start occurred' '
	test_debug "grep output simple_flow.err" &&
	grep -q "flow control stop" simple_flow.err &&
	grep -q "flow control start" simple_flow.err
'
test_expect_success 'no output was lost' '
	test_debug "ls -l simple.out simple_flow.out" &&
	sort <simple.out >simple.out.sorted &&
	sort <simple_flow.out >simple_flow.out.sorted &&
	test_cmp simple.out.sorted simple_flow.out.sorted
'
test_expect_success 'stderr has flow control as well' '
	flux run -N4 -l -o verbose -o \
	    output.client.lwm=1 -o output.client.hwm=10 \
	    sh -c "flux lptest >&2" 2>err_flow.err &&
	grep -q "flow control stop" err_flow.err &&
	grep -q "flow control start" err_flow.err
'
test_expect_success 'run a job with output.client.lwm=0' '
	flux run -N4 -l -o verbose -o \
	    output.client.lwm=0 -o output.client.hwm=10 \
	    flux lptest >zero.out 2>zero.err
'
test_expect_success 'flow control stop and start occurred' '
	test_debug "grep output zero.err" &&
	grep -q "flow control stop" zero.err &&
	grep -q "flow control start" zero.err
'
test_expect_success 'no output was lost' '
	test_debug "ls -l simple.out zero.out" &&
	sort <zero.out >zero.out.sorted &&
	test_cmp simple.out.sorted zero.out.sorted
'

test_done
