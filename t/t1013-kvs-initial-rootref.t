#!/bin/sh
#

test_description='Test kvs module initial-rootref option'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc

export FLUX_CONF_DIR=$(pwd)
SIZE=4
test_under_flux ${SIZE} minimal -Sstatedir=$(pwd)

test_expect_success 'load content, content-sqlite, and kvs' '
	flux module load content &&
	flux module load content-sqlite &&
	flux module load kvs
'

test_expect_success 'kvs: put some data to kvs' '
	flux kvs put --blobref data=1 > blob1.out &&
	flux kvs put --blobref data=2 > blob2.out &&
	flux kvs put --blobref data=3 > blob3.out &&
	flux kvs put --blobref data=4 > blob4.out &&
	flux kvs put --blobref data=5 > blob5.out
'

test_expect_success 'kvs: reload kvs' '
	flux module reload kvs
'

test_expect_success 'kvs: data should be last written' '
	echo "5" > data1.exp &&
	flux kvs get data > data1.out &&
	test_cmp data1.out data1.exp
'

test_expect_success 'kvs: root should be last one' '
	flux kvs getroot -b > getroot1.out &&
	test_cmp getroot1.out blob5.out
'

test_expect_success 'kvs: reload kvs with different initial rootref' '
        ref=$(cat blob3.out) &&
	flux module reload kvs initial-rootref="$ref"
'

test_expect_success 'kvs: data should be previous one' '
	echo "3" > data2.exp &&
	flux kvs get data > data2.out &&
	test_cmp data2.out data2.exp
'

test_expect_success 'kvs: root should be previous one' '
	flux kvs getroot -b > getroot2.out &&
	test_cmp getroot2.out blob3.out
'

test_expect_success 'kvs: remove kvs module' '
	flux module remove kvs
'

test_expect_success 'kvs: load kvs with bad rootref' '
	test_must_fail flux module reload kvs initial-rootref="abcdefghijklmnop"
'

test_expect_success 'kvs: remove modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
