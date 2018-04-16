#!/bin/sh
#

test_description='Test KVS job schema adherence

Test that KVS job schema adheres to RFC 16
'

. `dirname $0`/sharness.sh
SIZE=${FLUX_TEST_SIZE:-4}
test_under_flux ${SIZE} wreck

GUESTKVS=${FLUX_SOURCE_DIR}/t/kvs/guestkvs

#  Return the previous jobid
last_job_id() {
	flux wreck last-jobid
}
#  Return previous job path in kvs
last_job_path() {
	flux wreck last-jobid -p
}

test_expect_success 'schema: FLUX_JOB_KVSPATH points to private namespace' '
	flux wreckrun printenv FLUX_JOB_KVSPATH >kvspath.out &&
	echo "ns:job$(last_job_id)/." >kvspath.exp
	test_cmp kvspath.exp kvspath.out
'

# during execution guest is a symlink
# after execution guest is a directory
test_expect_success 'schema: guest namespace converted on completion' '
	flux wreckrun /bin/true &&
	test_must_fail flux kvs readlink $(last_job_path).guest
'

test_expect_success 'schema: guest namespace content preserved' '
	flux wreckrun ${GUESTKVS} put a=42 &&
	echo 42 >guesta.exp &&
	flux kvs get $(last_job_path).guest.a >guesta.out &&
	test_cmp guesta.exp guesta.out
'

test_done
