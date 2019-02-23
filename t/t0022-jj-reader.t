#!/bin/sh

test_description='Test json-jobspec *cough* parser *cough*'

. `dirname $0`/sharness.sh

#  Set path to jq
#
jq=$(which jq 2>/dev/null)
test -n "$jq" && test_set_prereq HAVE_JQ
jj=${FLUX_BUILD_DIR}/t/sched-simple/jj-reader
y2j=${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py

test_expect_success HAVE_JQ 'jj-reader: unexpected version throws error' '
	flux jobspec srun hostname | jq ".version = 2" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: Invalid version: expected 1, got 2
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success HAVE_JQ 'jj-reader: no version throws error' '
	flux jobspec srun hostname | jq "del(.version)" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: at top level: Object item not found: version
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success HAVE_JQ 'jj-reader: bad count throws error' '
	flux jobspec srun hostname | \
	  jq ".resources[0].with[0].count = -1" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: Invalid count -1 for type '\''core'\''
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success HAVE_JQ 'jj-reader: bad type throws error' '
	flux jobspec srun hostname | \
	  jq --arg f beans ".resources[0].type = \$f" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: Invalid type '\''beans'\''
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success HAVE_JQ 'jj-reader: missing count throws error' '
	flux jobspec srun hostname | \
	  jq "del(.resources[0].with[0].count)" >input.$test_count &&
	test_expect_code 1 $jj<input.$test_count >out.$test_count 2>&1 &&
	cat >expected.$test_count <<-EOF &&
	jj-reader: level 1: Object item not found: count
	EOF
	test_cmp expected.$test_count out.$test_count
'
test_expect_success HAVE_JQ 'jj-reader: wrong count type throws error' '
	flux jobspec srun hostname | \
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
jobspec/valid/basic.yaml        ==jj-reader: Unable to determine slot size
jobspec/valid/example2.yaml     ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_1.2.yaml ==jj-reader: level 0: Expected integer, got object
jobspec/valid/use_case_1.3.yaml ==jj-reader: level 2: Expected integer, got object
jobspec/valid/use_case_1.4.yaml ==jj-reader: Invalid type 'socket'
jobspec/valid/use_case_1.5.yaml ==jj-reader: Invalid type 'cluster'
jobspec/valid/use_case_1.6.yaml ==jj-reader: Invalid type 'cluster'
jobspec/valid/use_case_1.7.yaml ==jj-reader: Invalid type 'switch'
jobspec/valid/use_case_2.1.yaml ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_2.2.yaml ==jj-reader: Unable to determine slot size
jobspec/valid/use_case_2.5.yaml ==jj-reader: level 1: Expected integer, got object
jobspec/valid/use_case_2.6.yaml ==jj-reader: level 2: Expected integer, got object
EOF

while read line; do
  yaml=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
  expected=$(echo $line | awk -F== '{print $2}')

  test_expect_success "jj-reader: $(basename $yaml) gets expected error" '
	echo $expected >expected.$test_count &&
	$y2j<$SHARNESS_TEST_SRCDIR/$yaml >$test_count.json &&
	test_expect_code 1 $jj<$test_count.json > out.$test_count 2>&1 &&
	test_cmp expected.$test_count out.$test_count
  '
done <invalid.txt


# Valid inputs:
# <jobspec command args> == <expected result>
#
cat <<EOF >inputs.txt
srun              ==nnodes=0 nslots=1 slot_size=1
srun -N1          ==nnodes=1 nslots=1 slot_size=1
srun -N1 -n4      ==nnodes=1 nslots=4 slot_size=1
srun -N1 -n4 -c4  ==nnodes=1 nslots=4 slot_size=4
srun -n4 -c4      ==nnodes=0 nslots=4 slot_size=4
srun -n4 -c4      ==nnodes=0 nslots=4 slot_size=4
srun -n4 -c1      ==nnodes=0 nslots=4 slot_size=1
srun -N4 -n4 -c4  ==nnodes=4 nslots=4 slot_size=4
EOF

while read line; do

  args=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//')
  expected=$(echo $line | awk -F== '{print $2}')

  test_expect_success "jj-reader: $args returns $expected" '
	echo $expected >expected.$test_count &&
	flux jobspec $args hostname | $jj > output.$test_count &&
	test_cmp expected.$test_count output.$test_count
  '
done < inputs.txt

test_done
