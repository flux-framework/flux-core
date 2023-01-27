#!/bin/sh

test_description='Test python flux.constraint.parser standalone operation'

. `dirname $0`/sharness.sh

parser="flux python -m flux.constraint.parser"

test_expect_success 'flux.constraint.parser works' '
	$parser
'
test_expect_success 'flux.constraint.parser prints usage' '
	$parser --help > parser.out 2>&1 &&
	test_debug "cat parser.out" &&
	grep -i usage parser.out
'
test_expect_success 'flux.constraint.parser parses cmdline' '
	$parser test:a test:b >basic.out &&
	test_debug "cat basic.out" &&
	grep and basic.out
'
test_expect_success 'flux.constraint.parser --default-op works' '
	$parser --default=op=test a b >default-op.out &&
	test_debug "cat default-op.out" &&
	grep test default-op.out
'
test_expect_success 'flux.constraint.parser --debug works' '
	$parser --debug --default=op=test "a|(b&-c)" >debug.out 2>&1 &&
	test_debug "cat debug.out" &&
	grep TOKEN debug.out
'
test_done
