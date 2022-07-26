#!/bin/sh
#

test_description='Test kvs module checkpoint-period config.'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc

skip_all_unless_have jq

export FLUX_CONF_DIR=$(pwd)
SIZE=4
test_under_flux ${SIZE} minimal

checkpoint_get() {
	jq -j -c -n  "{key:\"$1\"}" \
	    | $RPC content-backing.checkpoint-get \
	    | jq -r .value.rootref
}

# arg1 - old ref
# arg2 - timeout (seconds)
checkpoint_changed() {
	old_ref=$1
	local i=0
	local iters=$(($2 * 10))
	while [ $i -lt ${iters} ]
	do
	    ref=$(checkpoint_get kvs-primary)
	    if [ "${old_ref}" != "${ref}" ]
	    then
		return 0
	    fi
	    sleep 0.1
	    i=$((i + 1))
	done
	return 1
}

test_expect_success 'configure bad checkpoint-period timer in kvs' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "1Z"
	EOF
	flux config reload &&
	test_must_fail flux module load kvs
'

test_expect_success 'configure checkpoint-period, place initial value' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "200ms"
	EOF
	flux config reload &&
	flux module load content-sqlite &&
	flux module load kvs &&
	flux kvs put --blobref --sync a=1 > blob1.out
'

test_expect_success 'kvs: put some more data to kvs (1)' '
	flux kvs put --blobref b=1 > blob2.out
'

test_expect_success 'kvs: checkpoint of kvs-primary should change in time (1)' '
	checkpoint_changed $(cat blob1.out) 5 &&
	checkpoint_get kvs-primary > checkpoint2.out &&
	test_cmp checkpoint2.out blob2.out
'

test_expect_success 'kvs: put some more data to kvs (2)' '
	flux kvs put --blobref c=1 > blob3.out
'

test_expect_success 'kvs: checkpoint of kvs-primary should change in time (2)' '
	checkpoint_changed $(cat blob2.out) 5 &&
	checkpoint_get kvs-primary > checkpoint3.out &&
	test_cmp checkpoint3.out blob3.out
'

test_expect_success 'kvs: put some data to non-primary namespace' '
	flux kvs namespace create "test-ns" &&
	flux kvs put --namespace=test-ns d=1
'

test_expect_success 'kvs: checkpoint of kvs-primary should not change (1)' '
	test_must_fail checkpoint_changed $(cat blob3.out) 2
'

test_expect_success 'configure bad checkpoint-period timer in kvs on reload' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "1Z"
	EOF
	test_must_fail flux config reload
'

test_expect_success 're-config checkpoint-period timer, set large period' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "60m"
	EOF
	flux config reload
'

test_expect_success 'kvs: put some more data to kvs (3)' '
	flux kvs put --blobref d=1 > blob4.out
'

test_expect_success 'kvs: checkpoint of kvs-primary should not change (2)' '
	test_must_fail checkpoint_changed $(cat blob3.out) 2
'

test_expect_success 're-config checkpoint-period timer, set small period' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "200ms"
	EOF
	flux config reload
'

test_expect_success 'kvs: checkpoint of kvs-primary should change in time (3)' '
	checkpoint_changed $(cat blob3.out) 5 &&
	checkpoint_get kvs-primary > checkpoint4.out &&
	test_cmp checkpoint4.out blob4.out
'

test_expect_success 're-config checkpoint-period timer, disable it' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "0s"
	EOF
	flux config reload
'

test_expect_success 'kvs: put some more data to kvs (4)' '
	flux kvs put --blobref e=1 > blob5.out
'

test_expect_success 'kvs: checkpoint of kvs-primary should not change (3)' '
	test_must_fail checkpoint_changed $(cat blob4.out) 2
'

test_expect_success 're-config checkpoint-period timer in kvs, re-enable it' '
	cat >kvs.toml <<-EOF &&
	[kvs]
	checkpoint-period = "200ms"
	EOF
	flux config reload
'

test_expect_success 'kvs: checkpoint of kvs-primary should change in time (4)' '
	checkpoint_changed $(cat blob4.out) 5 &&
	checkpoint_get kvs-primary > checkpoint5.out &&
	test_cmp checkpoint5.out blob5.out
'

test_expect_success 'kvs: remove modules' '
	flux module remove kvs &&
	flux module remove content-sqlite
'

test_done
