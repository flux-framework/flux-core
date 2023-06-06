#!/bin/sh

test_description='flux batch --conf tests'

. $(dirname $0)/sharness.sh


# Start an instance with 16 cores across 4 ranks
export TEST_UNDER_FLUX_CORES_PER_RANK=4
test_under_flux 4 job

flux setattr log-stderr-level 1

NCORES=$(flux kvs get resource.R | flux R decode --count=core)
test ${NCORES} -gt 4 && test_set_prereq MULTICORE

test_expect_success 'flux-batch: create test configs' '
	cat <<-EOF >conf.json &&
	{"resource": {"noverify": true}}
	EOF
	cat <<-EOF >conf.toml
	[resource]
	noverify = true
	EOF
'
test_expect_success 'flux-batch --conf=FILE works with TOML file' '
	flux batch --conf=conf.toml -n1 --dry-run --wrap hostname \
		>conf1.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf1.json
'
test_expect_success 'flux-batch --conf=FILE works with JSON file' '
	flux batch --conf=conf.json -n1 --dry-run --wrap hostname \
		>conf2.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf2.json
'
test_expect_success 'flux-batch --conf=FILE errors if FILE does not exist' '
	test_must_fail flux batch --conf=/nofile.json -n1 --dry-run \
		--wrap hostname
'
test_expect_success 'flux-batch --conf=FILE detects invalid TOML syntax' '
	cat <<-EOF >conf-bad.toml &&
	[resource]
	noverify = foo
	EOF
	test_must_fail flux batch --conf=conf-bad.toml -n1 --dry-run \
		--wrap hostname >parse-error.out 2>&1 &&
	test_debug "cat parse-error.out" &&
	grep "parse error" parse-error.out
'
test_expect_success 'flux-batch --conf=FILE detects invalid JSON syntax' '
	cat <<-EOF >conf-bad.json &&
	{"resource": {"noverify": foo}}
	EOF
	test_must_fail flux batch --conf=conf-bad.json -n1 --dry-run \
		--wrap hostname >parse-error2.out 2>&1 &&
	test_debug "cat parse-error2.out" &&
	grep "parse error" parse-error2.out
'
test_expect_success 'flux-batch --conf=NAME raises NotImplemented' '
	test_must_fail flux batch --conf=named -n1 --dry-run \
		--wrap hostname >named.out 2>&1 &&
	test_debug "cat named.out" &&
	grep "no named config support" named.out
'
test_expect_success 'flux-batch --conf=KEY=VAL works' '
	flux batch --conf=resource.noverify=true -n1 --dry-run \
		--wrap hostname >conf3.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf3.json
'
test_expect_success 'flux-batch --conf=KEY=VAL multiple use is merged' '
	flux batch --conf=resource.noverify=true \
		--conf=resource.exclude=0 -n1 --dry-run \
		--wrap hostname >conf4.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf4.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.exclude == 0" \
		<conf4.json
'
test_expect_success 'flux-batch --conf=KEY=VAL VAL can be JSON' '
	flux batch --conf=foo={} \
		--conf=resource.exclude=0 -n1 --dry-run \
		--wrap hostname >conf4.1.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.foo == {}" \
		<conf4.1.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.exclude == 0" \
		<conf4.1.json
'
test_expect_success 'flux-batch --conf=FILE --conf=KEY=VAL works' '
	flux batch --conf=conf.toml \
		--conf=resource.exclude=0 -n1 --dry-run \
		--wrap hostname >conf5.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf5.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.exclude == 0" \
		<conf5.json
'
test_expect_success 'flux-batch multiline --conf directive works' '
	cat <<-EOF >batch.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --conf="""
	# flux: [resource]
	# flux: noverify = true
	# flux: """
	flux config get
	EOF
	flux batch --dry-run batch.sh >conf6.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf6.json
'
test_expect_success 'flux-batch multiline --conf directive in JSON works' '
	cat <<-EOF >batch2.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --conf="""
	# flux: {"resource": {"noverify": true}}
	# flux: """
	flux config get
	EOF
	flux batch --dry-run batch.sh >conf7.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf7.json
'
test_expect_success 'flux-batch --conf directive syntax error detected' '
	cat <<-EOF >bad-batch.sh &&
	#!/bin/sh
	# flux: -n1
	# flux: --conf="""
	# flux: [resource]
	# flux: noverify = foo
	# flux: """
	flux config get
	EOF
	test_must_fail flux batch --dry-run bad-batch.sh >ml-syntax.out 2>&1 &&
	test_debug "cat ml-syntax.out" &&
	grep "failed to parse" ml-syntax.out
'
test_expect_success 'flux-batch multiline --conf + --conf=KEY=VAL works' '
	flux batch --conf=resource.exclude=0 --dry-run batch.sh >conf8.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify" \
		<conf8.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.exclude == 0" \
		<conf8.json
'
test_expect_success 'flux-batch multiline --conf + --conf=KEY=VAL override works' '
	flux batch --conf=resource.noverify=false --dry-run batch.sh \
		>conf9.json &&
	jq -e ".attributes.system.files[\"conf.json\"].data.resource.noverify == false" \
		<conf9.json
'
test_expect_success 'flux-batch --conf end-to-end test' '
	jobid=$(flux batch --conf=resource.exclude=\"0\" \
		--output=batchtest.out batch.sh) &&
	flux job wait-event $jobid clean &&
	jq -e ".resource.noverify" <batchtest.out &&
	jq -e ".resource.exclude == \"0\"" <batchtest.out
'
test_done
