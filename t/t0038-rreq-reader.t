#!/bin/sh

test_description='Test Python ResourceRequest.from_jobspec via rreq-reader'

. `dirname $0`/sharness.sh

rreq="flux python ${SHARNESS_TEST_SRCDIR}/scripts/rreq-reader.py"
y2j="flux python ${SHARNESS_TEST_SRCDIR}/jobspec/y2j.py"

# ---------------------------------------------------------------------------
# Inline error cases
# ---------------------------------------------------------------------------

test_expect_success 'rreq-reader: V1 missing attributes.system raises error' '
	cat >input.$test_count <<-EOF &&
	{"version":1,"resources":[{"type":"slot","count":1,"with":[{"type":"core","count":1}]}],"tasks":[],"attributes":{}}
	EOF
	test_expect_code 1 $rreq <input.$test_count >out.$test_count 2>err.$test_count &&
	grep -q "system" err.$test_count
'

test_expect_success 'rreq-reader: V1 missing duration raises error' '
	cat >input.$test_count <<-EOF &&
	{"version":1,"resources":[{"type":"slot","count":1,"with":[{"type":"core","count":1}]}],"tasks":[],"attributes":{"system":{}}}
	EOF
	test_expect_code 1 $rreq <input.$test_count >out.$test_count 2>err.$test_count &&
	grep -q "duration" err.$test_count
'

test_expect_success 'rreq-reader: unsupported range operator raises error' '
	cat >input.$test_count <<-EOF &&
	{"version":1,"resources":[{"type":"node","count":{"min":2,"max":8,"operator":"*","operand":2},"with":[{"type":"slot","count":1,"with":[{"type":"core","count":1}]}]}],"tasks":[],"attributes":{"system":{"duration":60}}}
	EOF
	test_expect_code 1 $rreq <input.$test_count >out.$test_count 2>err.$test_count &&
	grep -q "not yet supported" err.$test_count
'

test_expect_success 'rreq-reader: no resources raises error' '
	cat >input.$test_count <<-EOF &&
	{"version":1,"resources":[],"tasks":[],"attributes":{"system":{"duration":60}}}
	EOF
	test_expect_code 1 $rreq <input.$test_count >out.$test_count 2>err.$test_count
'

# ---------------------------------------------------------------------------
# Canonical jobspec YAML files that produce errors
#
# <filename-without-extension> ==<expected error substring>
# ---------------------------------------------------------------------------

cat <<EOF >invalid.txt
attributes_system        ==Unable to determine slot size
attributes_user          ==Unable to determine slot size
basic                    ==Unable to determine slot size
example2                 ==Unable to determine slot size
resource_count_string_min_only ==Unable to determine slot size
resource_count_string_range    ==Unable to determine slot size
use_case_1.1             ==Unable to determine slot size
use_case_1.2             ==Unable to determine slot size
use_case_1.8             ==Unable to determine slot size
use_case_2.1             ==Unable to determine slot size
use_case_2.2             ==Unable to determine slot size
use_case_2.6             ==Unable to determine slot size
use_case_2.7             ==Unable to determine slot size
EOF

while read line; do
	yaml=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//').yaml
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "rreq-reader: $yaml gets expected error" '
		echo $expected >expected.$test_count &&
		cat $SHARNESS_TEST_SRCDIR/jobspec/valid/$yaml | $y2j >$test_count.json &&
		test_expect_code 1 $rreq <$test_count.json >out.$test_count 2>err.$test_count &&
		grep -qF "$expected" err.$test_count
	'
done <invalid.txt

# ---------------------------------------------------------------------------
# Canonical jobspec YAML files that parse successfully
#
# <filename-without-extension> ==<expected output>
# ---------------------------------------------------------------------------

cat <<EOF >valid.txt
basic_v1    ==nnodes=0 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=0.0
example1    ==nnodes=4 nslots=4 slot_size=2 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.3  ==nnodes=4 nslots=16 slot_size=4 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.4  ==nnodes=4 nslots=16 slot_size=4 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.5  ==nnodes=2 nslots=4 slot_size=2 slot_gpus=0 exclusive=false duration=14400.0
use_case_1.6  ==nnodes=2 nslots=2 slot_size=30 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.7  ==nnodes=3 nslots=3 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.9  ==nnodes=1 nslots=1 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
use_case_1.10 ==nnodes=10 nslots=10 slot_size=2 slot_gpus=0 exclusive=true duration=3600.0
use_case_1.11 ==nnodes=2 nslots=2 slot_size=5 slot_gpus=7 exclusive=true duration=3600.0
use_case_2.3  ==nnodes=0 nslots=10 slot_size=2 slot_gpus=0 exclusive=false duration=3600.0
use_case_2.4  ==nnodes=1 nslots=1 slot_size=6 slot_gpus=0 exclusive=false duration=3600.0
use_case_2.5  ==nnodes=0 nslots=10 slot_size=1 slot_gpus=0 exclusive=false duration=3600.0
EOF

while read line; do
	yaml=$(echo $line | awk -F== '{print $1}' | sed 's/  *$//').yaml
	expected=$(echo $line | awk -F== '{print $2}')

	test_expect_success "rreq-reader: $yaml returns $expected" '
		echo $expected >expected.$test_count &&
		cat $SHARNESS_TEST_SRCDIR/jobspec/valid/$yaml | $y2j | $rreq >out.$test_count &&
		test_cmp expected.$test_count out.$test_count
	'
done <valid.txt

test_done
