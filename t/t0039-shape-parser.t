#!/bin/sh

test_description='Test python flux.shape.parser standalone operation'

. `dirname $0`/sharness.sh

parser="flux python -m flux.shape.parser"

test_expect_success 'flux.shape.parser works' '
	$parser
'
test_expect_success 'flux.shape.parser prints usage' '
	$parser --help >usage.out 2>&1 &&
	grep -i usage usage.out
'
test_expect_success 'flux.shape.parser parses simple expression' '
	cat >expected <<-EOF &&
	[{"type": "slot", "count": 4, "label": "task", "with": [{"type": "node", "count": 2}]}]
	EOF
	$parser -- slot=4/node=[2] >output &&
	test_cmp expected output
'
test_expect_success 'flux.shape.parser fails with unclosed quote' '
	test_must_fail $parser -- slot/\"blahblahblah 2>error_a &&
	grep "Unclosed quote" error_a
'
test_expect_success 'flux.shape.parser handles dicts' '
	cat >expected_b <<-EOF &&
	[{"type": "slot", "count": 1, "label": "task", "foo": "bar", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- slot{task\,foo:bar}/node{} >output_b &&
	test_cmp expected_b output_b
'
test_expect_success 'flux.shape.parser fails with invalid token' '
	test_must_fail $parser -- slot/node=\; 2>error_c &&
	grep "Invalid token" error_c
'
test_expect_success 'flux.shape.parser fails with illegal character' '
	test_must_fail $parser -- slot/node=a 2>error_d &&
	grep "Illegal character" error_d
'
test_expect_success 'flux.shape.parser handles single slot without label' '
	cat >expected1 <<-EOF &&
	[{"type": "slot", "count": 1, "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- slot/node >output1 &&
	test_cmp expected1 output1
'
test_expect_success 'flux.shape.parser parses slot with label' '
	cat >expected2 <<-EOF &&
	[{"type": "slot", "count": 4, "label": "nodelevel", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- "slot=4{nodelevel}/node" >output2 &&
	test_cmp expected2 output2
'
test_expect_success 'flux.shape.parser parses count range' '
	cat >expected3 <<-EOF &&
	[{"type": "slot", "count": "3-30", "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- slot=3-30/node >output3 &&
	test_cmp expected3 output3
'
test_expect_success 'flux.shape.parser parses open-ended range' '
	cat >expected4 <<-EOF &&
	[{"type": "slot", "count": "2+", "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- slot=2+/node >output4 &&
	test_cmp expected4 output4
'
test_expect_success 'flux.shape.parser parses idset count' '
	cat >expected5 <<-EOF &&
	[{"type": "slot", "count": "4,9,16,25", "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- slot=4,9,16,25/node >output5 &&
	test_cmp expected5 output5
'
test_expect_success 'flux.shape.parser parses attributes in dict' '
	cat >expected6 <<-EOF &&
	[{"type": "node", "count": 1, "exclusive": false, "with": [{"type": "core", "count": 4}]}]
	EOF
	$parser -- "node{-x}/core=4" >output6 &&
	test_cmp expected6 output6
'
test_expect_success 'flux.shape.parser parses nested resources' '
	cat >expected7 <<-EOF &&
	[{"type": "node", "count": 1, "with": [{"type": "socket", "count": 2, "with": [{"type": "core", "count": 4}]}]}]
	EOF
	$parser -- node/socket=2/core=4 >output7 &&
	test_cmp expected7 output7
'
test_expect_success 'flux.shape.parser parses resource list with brackets' '
	cat >expected8 <<-EOF &&
	[{"type": "slot", "count": 10, "label": "task1", "with": [{"type": "core", "count": 1}]}, {"type": "slot", "count": 2, "label": "task2", "with": [{"type": "core", "count": 2}]}]
	EOF
	$parser -- "[slot=10{task1}/core;slot=2{task2}/core=2]" >output8 &&
	test_cmp expected8 output8
'
test_expect_success 'flux.shape.parser parses complex example from RFC 46' '
	cat >expected9 <<-EOF &&
	[{"type": "node", "count": 1, "with": [{"type": "slot", "count": 10, "label": "read-db", "with": [{"type": "core", "count": 1}, {"type": "memory", "count": 4, "unit": "GB"}]}, {"type": "slot", "count": 1, "label": "db", "with": [{"type": "core", "count": 6}, {"type": "memory", "count": 24, "unit": "GB"}]}]}]
	EOF
	$parser -- "node/[slot=10{read-db}/[core;memory=4{unit:GB}];slot{db}/[core=6;memory=24{unit:GB}]]" >output9 &&
	test_cmp expected9 output9
'
test_expect_success 'flux.shape.parser --yaml produces yaml output' '
	cat >expected10 <<-EOF &&
	- type: slot
	  count: 4
	  label: task
	  with:
	  - type: node
	    count: 1

	EOF
	$parser --yaml -- slot=4/node >output10 &&
	test_cmp expected10 output10
'
test_expect_success 'flux.shape.parser --debug shows token information' '
	$parser --debug -- slot=4/node >output11 2>&1 &&
	grep "LexToken" output11
'
test_expect_success 'flux.shape.parser --wrapresources wraps output' '
	cat >expected12 <<-EOF &&
	{"resources": [{"type": "slot", "count": 4, "label": "task", "with": [{"type": "node", "count": 1}]}]}
	EOF
	$parser --wrapresources -- slot=4/node >output12 &&
	test_cmp expected12 output12
'
test_expect_success 'flux.shape.parser --label changes default slot label' '
	cat >expected13 <<-EOF &&
	[{"type": "slot", "count": 1, "label": "mytask", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser --label=mytask -- slot/node >output13 &&
	test_cmp expected13 output13
'
test_expect_success 'flux.shape.parser --rangedict outputs range as dict' '
	cat >expected14 <<-EOF &&
	[{"type": "slot", "count": {"min": 3, "max": 30, "operand": 1, "operator": "+"}, "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser --rangedict -- slot=3-30/node >output14 &&
	test_cmp expected14 output14
'
test_expect_success 'flux.shape.parser rejects invalid syntax' '
	test_must_fail $parser -- "slot/" >error15.out 2>&1 &&
	grep -i "error\|invalid" error15.out
'
test_expect_success 'flux.shape.parser rejects unclosed brace' '
	test_must_fail $parser -- "slot=2{blah" >error16.out 2>&1 &&
	grep -i "error\|unclosed" error16.out
'
test_expect_success 'flux.shape.parser rejects mismatched braces' '
	test_must_fail $parser -- "slot=2{bla}}" >error17.out 2>&1 &&
	grep -i "error\|mismatch" error17.out
'
test_expect_success 'flux.shape.parser rejects mismatched brackets' '
	test_must_fail $parser -- "node/[slot=10/core]]]" >error18.out 2>&1 &&
	grep -i "error\|mismatch" error18.out
'
test_expect_success 'flux.shape.parser requires label for multiple slots' '
	test_must_fail $parser -- "[slot=2/node;slot=3/node]" >error19.out 2>&1 &&
	grep -i "error\|label" error19.out
'
test_expect_success 'flux.shape.parser parses quoted strings' '
	cat >expected20 <<-EOF &&
	[{"type": "my type", "count": 1, "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- "\"my type\"/node" >output20 &&
	test_cmp expected20 output20
'
test_expect_success 'flux.shape.parser parses single-quoted strings' '
	cat >expected21 <<-EOF &&
	[{"type": "my type", "count": 1, "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser -- "'"'my type'"'/node" >output21 &&
	test_cmp expected21 output21
'
test_expect_success 'flux.shape.parser handles empty quotes' '
	cat >expected22 <<-EOF &&
	[{"type": "node", "count": 1, "unit": ""}]
	EOF
	$parser -- "node{unit:\"\"}" >output22 &&
	test_cmp expected22 output22
'
test_expect_success 'flux.shape.parser parses boolean values in dict' '
	cat >expected23 <<-EOF &&
	[{"type": "node", "count": 1, "key": true, "flag": false}]
	EOF
	$parser -- "node{key:true,flag:false}" >output23 &&
	test_cmp expected23 output23
'
test_expect_success 'flux.shape.parser parses null value in dict' '
	cat >expected24 <<-EOF &&
	[{"type": "node", "count": 1, "key": null}]
	EOF
	$parser -- "node{key:null}" >output24 &&
	test_cmp expected24 output24
'
test_expect_success 'flux.shape.parser parses number values in dict' '
	cat >expected25 <<-EOF &&
	[{"type": "node", "count": 1, "int": 42, "float": 3.14, "sci": 100000.0}]
	EOF
	$parser -- "node{int:42,float:3.14,sci:1e5}" >output25 &&
	test_cmp expected25 output25
'
test_expect_success 'flux.shape.parser parses array values in dict' '
	cat >expected26 <<-EOF &&
	[{"type": "node", "count": 1, "ports": [8080, 8081]}]
	EOF
	$parser -- "node{ports:[8080,8081]}" >output26 &&
	test_cmp expected26 output26
'
test_expect_success 'flux.shape.parser shorthand x expands to exclusive' '
	cat >expected27 <<-EOF &&
	[{"type": "node", "count": 1, "exclusive": true}]
	EOF
	$parser -- "node{x}" >output27 &&
	test_cmp expected27 output27
'
test_expect_success 'flux.shape.parser +key shorthand for true' '
	cat >expected28 <<-EOF &&
	[{"type": "node", "count": 1, "key": true}]
	EOF
	$parser -- "node{+key}" >output28 &&
	test_cmp expected28 output28
'
test_expect_success 'flux.shape.parser -key shorthand for false' '
	cat >expected29 <<-EOF &&
	[{"type": "node", "count": 1, "key": false}]
	EOF
	$parser -- "node{-key}" >output29 &&
	test_cmp expected29 output29
'
test_expect_success 'flux.shape.parser handles range with operand' '
	cat >expected30 <<-EOF &&
	[{"type": "slot", "count": {"min": 3, "max": 30, "operand": 2, "operator": "+"}, "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser --rangedict -- "slot=3-30:2/node" >output30 &&
	test_cmp expected30 output30
'
test_expect_success 'flux.shape.parser handles range with operator' '
	cat >expected31 <<-EOF &&
	[{"type": "slot", "count": {"min": 3, "max": 30, "operand": 2, "operator": "+"}, "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser --rangedict -- "slot=3-30:2:+/node" >output31 &&
	test_cmp expected31 output31
'
test_expect_success 'flux.shape.parser handles open range with operand' '
	cat >expected32 <<-EOF &&
	[{"type": "slot", "count": {"min": 3, "max": Infinity, "operand": 2, "operator": "+"}, "label": "task", "with": [{"type": "node", "count": 1}]}]
	EOF
	$parser --rangedict -- "slot=3+:2/node" >output32 &&
	test_cmp expected32 output32
'
test_done
