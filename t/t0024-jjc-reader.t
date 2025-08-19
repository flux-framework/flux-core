#!/bin/sh

test_description='Test json-jobspec *cough* parser *cough*'

. `dirname $0`/sharness.sh

jjc=${FLUX_BUILD_DIR}/t/sched-simple/jjc-reader
y2j="flux python ${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py"

test_expect_success 'jjc-reader: unexpected version continues without error' '
	flux run --dry-run hostname \
		| jq ".version = 2" >input.$test_count &&
	test_expect_code 0 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	nodefactor=0 nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: unexpected type continues without error' '
	flux run --dry-run -N1 -n1 hostname | \
		jq --arg f beans ".resources[0].type = \$f" >input.$test_count &&
	test_expect_code 0 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	nodefactor=0 nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: no version throws error' '
	flux run --dry-run hostname \
		| jq "del(.version)" >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: at top level: Object item not found: version
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: bad count throws error' '
	flux run --dry-run hostname | \
		jq ".resources[0].with[0].count = -1" >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: level 1: create_count: integer count must be >= 1
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: no slot throws error' '
	flux run --dry-run hostname | \
		jq --arg f beans ".resources[0].type = \$f" >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: Unable to determine slot count
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: missing count throws error' '
	flux run --dry-run hostname | \
		jq "del(.resources[0].with[0].count)" >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: level 1: Object item not found: count
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: wrong count type throws error' '
	flux run --dry-run hostname | \
		jq ".resources[0].with[0].count = 1.5" >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: level 1: create_count: Malformed jobspec resource count
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: resource level not array throws error' '
	cat >input.$test_count <<-EOF &&
	{"resources": {}, "attributes": {"system": {"duration": 0}}, "version": 1}
	EOF
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: level 0: must be an array
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success 'jjc-reader: invalid JSON input throws error' '
	echo {"resources":} >input.$test_count &&
	test_expect_code 1 $jjc<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jjc-reader: JSON load: string or '\''}'\'' expected near '\''resources'\''
	EOF
	test_cmp expected.$test_count out.$test_count
'

# Invalid inputs:
# <jobspec.yaml filename> ==<expected error>
#
cat <<EOF>invalid.txt
basic        ==jjc-reader: at top level: getting duration: Object item not found: system
example2     ==jjc-reader: Unable to determine slot size
use_case_1.2 ==jjc-reader: Non-integer count not allowed above node
use_case_2.1 ==jjc-reader: Unable to determine slot size
use_case_2.2 ==jjc-reader: Unable to determine slot size
use_case_2.6 ==jjc-reader: Unable to determine slot size
EOF

while read line; do
	yaml=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//').yaml
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "jjc-reader: $yaml gets expected error" '
		echo $expected >expected.$test_count &&
		cat $SHARNESS_TEST_SRCDIR/jobspec/valid/$yaml | $y2j >$test_count.json &&
		test_expect_code 1 $jjc<$test_count.json > out.$test_count 2>&1 &&
		test_cmp expected.$test_count out.$test_count
	'
done <invalid.txt


# Valid inputs:
# <flux run args> ==<expected result>
#
cat <<EOF >inputs.txt
             ==nodefactor=0 nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
-N1 -n1      ==nodefactor=1 nnodes=1 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
-N1 -n4      ==nodefactor=1 nnodes=1 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
-N1 -n4 -c4  ==nodefactor=1 nnodes=1 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
-n4 -c4      ==nodefactor=0 nnodes=0 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
-n4 -c4      ==nodefactor=0 nnodes=0 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
-n4 -c1      ==nodefactor=0 nnodes=0 nslots=4 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
-N4 -n4 -c4  ==nodefactor=1 nnodes=4 nslots=1 slot_size=4 slot_gpus=0 exclusive=false duration=0.0
-t 1m -N4 -n4 ==nodefactor=1 nnodes=4 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=60.0
-t 5s -N4 -n4 ==nodefactor=1 nnodes=4 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=5.0
-t 1h -N4 -n4 ==nodefactor=1 nnodes=4 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
-g1           ==nodefactor=0 nnodes=0 nslots=1 slot_size=1 slot_gpus=1 exclusive=false duration=0.0
-N1 -n2 -c2 -g1 ==nodefactor=1 nnodes=1 nslots=2 slot_size=2 slot_gpus=1 exclusive=false duration=0.0
EOF

while read line; do

	args=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "jjc-reader: run $args returns $expected" '
		echo $expected >expected.$test_count &&
		flux run $args --dry-run hostname | $jjc > output.$test_count &&
		test_cmp expected.$test_count output.$test_count
	'
done < inputs.txt


# Valid inputs:
# <jobspec use_case #> ==<expected result>
#
cat <<EOF >inputs2.txt
1.3  ==nodefactor=4 nnodes=1 nslots=4 slot_size=4+ slot_gpus=0 exclusive=false duration=3600.0
1.4  ==nodefactor=4 nnodes=1 nslots=4 slot_size=4 slot_gpus=0 exclusive=false duration=3600.0
1.5  ==nodefactor=2 nnodes=1 nslots=2 slot_size=2 slot_gpus=0 exclusive=false duration=14400.0
1.6  ==nodefactor=2 nnodes=1+ nslots=1 slot_size=30 slot_gpus=0 exclusive=false duration=3600.0
1.7  ==nodefactor=3 nnodes=1+ nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
1.9  ==nodefactor=1 nnodes=1 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
1.10 ==nodefactor=10 nnodes=1 nslots=1 slot_size=2 slot_gpus=0 exclusive=true duration=3600.0
1.11 ==nodefactor=2 nnodes=1 nslots=1 slot_size=5 slot_gpus=7 exclusive=true duration=3600.0
2.5  ==nodefactor=0 nnodes=0 nslots=10 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
EOF

while read line; do

	yaml=use_case_$(echo $line | awk -F== '{print $1}' | sed 's/  *$//').yaml
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "jjc-reader: $yaml returns $expected" '
		echo $expected >expected.$test_count &&
		cat $SHARNESS_TEST_SRCDIR/jobspec/valid/$yaml | $y2j | \
            $jjc > output.$test_count &&
		test_cmp expected.$test_count output.$test_count
	'
done < inputs2.txt


test_done
