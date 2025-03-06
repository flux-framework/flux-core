#!/bin/sh

test_description='Test json-jobspec *cough* parser *cough*'

. `dirname $0`/sharness.sh

jj=${FLUX_BUILD_DIR}/t/sched-simple/jj-reader
y2j="flux python ${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py"

test_expect_success 'jj-reader: unexpected version continues without error' '
	flux run --dry-run hostname \
		| jq ".version = 2" >input.$test_count &&
	test_expect_code 0 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jj-reader: no version throws error' '
	flux run --dry-run hostname \
		| jq "del(.version)" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: at top level: Object item not found: version
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jj-reader: bad count throws error' '
	flux run --dry-run hostname | \
		jq ".resources[0].with[0].count = -1" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: Invalid count -1 for type '\''core'\''
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jj-reader: bad type throws error' '
	flux run --dry-run hostname | \
		jq --arg f beans ".resources[0].type = \$f" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: Unsupported resource type '\''beans'\''
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jj-reader: missing count throws error' '
	flux run --dry-run hostname | \
		jq "del(.resources[0].with[0].count)" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: level 1: Object item not found: count
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jj-reader: wrong count type throws error' '
	flux run --dry-run hostname | \
		jq ".resources[0].with[0].count = 1.5" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: level 1: Expected integer, got real
	EOF
	test_cmp expected.$test_count out.$test_count
'

# Invalid inputs:
# jobspec.yaml ==<expected error>
#
cat <<EOF>invalid.txt
jobspec/valid/basic.yaml        ==jj-reader: at top level: getting duration: Object item not found: system
jobspec/valid/example2.yaml     ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_1.2.yaml ==jj-reader: level 0: Expected integer, got object
jobspec/valid/use_case_1.3.yaml ==jj-reader: level 2: Expected integer, got object
jobspec/valid/use_case_1.4.yaml ==jj-reader: Unsupported resource type 'socket'
jobspec/valid/use_case_1.5.yaml ==jj-reader: Unsupported resource type 'cluster'
jobspec/valid/use_case_1.6.yaml ==jj-reader: Unsupported resource type 'cluster'
jobspec/valid/use_case_1.7.yaml ==jj-reader: Unsupported resource type 'switch'
jobspec/valid/use_case_2.1.yaml ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_2.2.yaml ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_2.5.yaml ==jj-reader: level 1: Expected integer, got object
jobspec/valid/use_case_2.6.yaml ==jj-reader: level 2: Expected integer, got object
EOF

#
# N.B. RFC 14 examples recently bumped the version from 1 to 999.  Since
# the tests below check that certain constructs in those examples trigger
# appropriate libjj errors, we change version back to 1 just for this block
# of tests to retain that coverage.  --Jim G.
#
while read line; do
	yaml=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "jj-reader: $(basename $yaml) gets expected error" '
		echo $expected >expected.$test_count &&
		sed -e 's/999/1/' $SHARNESS_TEST_SRCDIR/$yaml |$y2j >$test_count.json &&
		test_expect_code 1 $jj<$test_count.json > out.$test_count 2>&1 &&
		test_cmp expected.$test_count out.$test_count
	'
done <invalid.txt


# Valid inputs:
# <jobspec command args> == <expected result>
#
cat <<EOF >inputs.txt
run              ==nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
run -N1 -n1      ==nnodes=1 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
run -N1 -n4      ==nnodes=1 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
run -N1 -n4 -c4  ==nnodes=1 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
run -n4 -c4      ==nnodes=0 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
run -n4 -c4      ==nnodes=0 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
run -n4 -c1      ==nnodes=0 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
run -N4 -n4 -c4  ==nnodes=4 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
run -t 1m -N4 -n4 ==nnodes=4 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=60.0
run -t 5s -N4 -n4 ==nnodes=4 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=5.0
run -t 1h -N4 -n4 ==nnodes=4 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
run -g1           ==nnodes=0 nslots=1 slot_size=1 slot_gpus=1 exclusive=false duration=0.0
run -N1 -n2 -c2 -g1 ==nnodes=1 nslots=2 slot_size=2 slot_gpus=1 exclusive=false duration=0.0
EOF

while read line; do

	args=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "jj-reader: $args returns $expected" '
		echo $expected >expected.$test_count &&
		flux $args --dry-run hostname | $jj > output.$test_count &&
		test_cmp expected.$test_count output.$test_count
	'
done < inputs.txt


test_done
