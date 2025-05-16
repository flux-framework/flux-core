#!/bin/sh

test_description='Test content-sqlite checkpointing'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=1
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'load content module' '
	flux module load content
'

test_expect_success 'load content-sqlite with invalid max-checkpoints config' '
	cat >maxcheckpoints.toml <<-EOT &&
	[content-sqlite]
	max_checkpoints = -1
	EOT
	flux config load maxcheckpoints.toml &&
	test_must_fail flux module load content-sqlite
'

test_expect_success 'load content-sqlite with valid max-checkpoints config' '
	cat >maxcheckpoints.toml <<-EOT &&
	[content-sqlite]
	max_checkpoints = 5
	EOT
	flux config load maxcheckpoints.toml &&
	flux module load content-sqlite
'

test_expect_success 'remove content-sqlite & content module' '
	flux module remove content-sqlite
	flux module remove content
'

test_expect_success 'load content module' '
	flux module load content
'

test_expect_success 'load content-sqlite module with invalid max-checkpoints' '
	test_must_fail flux module load content-sqlite max-checkpoints=-1
'

test_expect_success 'load content-sqlite module with max-checkpoints' '
	flux module load content-sqlite max-checkpoints=5
'

test_expect_success 'checkpoint-put w/ different rootrefs (key1)' '
	checkpoint_put key1 ref1 &&
	checkpoint_put key1 ref2 &&
	checkpoint_put key1 ref3 &&
	checkpoint_put key1 ref4 &&
	checkpoint_put key1 ref5 &&
	checkpoint_put key1 ref6
'

test_expect_success 'checkpoint-get returns most recent checkpoint (key1)' '
	echo ref6 >rootrefkey1.exp &&
	checkpoint_get key1 | jq -r .value | jq -r .rootref >rootrefkey1.out &&
	test_cmp rootrefkey1.exp rootrefkey1.out
'

test_expect_success 'content-sqlite stores only max of 5 checkpoints (key1)' '
	count=`flux module stats content-sqlite | jq ".checkpoints.key1 | length"`
	test $count -eq 5
'

test_expect_success 'content-sqlite the first checkpoint is the most recent (key1)' '
	flux module stats content-sqlite | jq ".checkpoints.key1[0].value" | grep ref6
'

test_expect_success 'content-sqlite the oldest checkpoint is not listed (key1)' '
	flux module stats content-sqlite | jq ".checkpoints.key1" > key1checkpoints.out &&
	test_must_fail grep ref1 key1checkpoints.out
'

test_expect_success 'checkpoint-put w/ different rootrefs (key2)' '
	checkpoint_put key2 ref10 &&
	checkpoint_put key2 ref11 &&
	checkpoint_put key2 ref12 &&
	checkpoint_put key2 ref13 &&
	checkpoint_put key2 ref14 &&
	checkpoint_put key2 ref15
'

test_expect_success 'checkpoint-get returns most recent checkpoint (key2)' '
	echo ref15 >rootrefkey2.exp &&
	checkpoint_get key2 | jq -r .value | jq -r .rootref >rootrefkey2.out &&
	test_cmp rootrefkey2.exp rootrefkey2.out
'

test_expect_success 'checkpoint-get still returns most recent checkpoint (key1)' '
	echo ref6 >rootrefkey1B.exp &&
	checkpoint_get key1 | jq -r .value | jq -r .rootref >rootrefkey1B.out &&
	test_cmp rootrefkey1B.exp rootrefkey1B.out
'

test_expect_success 'content-sqlite stores only max of 5 checkpoints (key2)' '
	count=`flux module stats content-sqlite | jq ".checkpoints.key2 | length"`
	test $count -eq 5
'

test_expect_success 'content-sqlite still stores only max of 5 checkpoints (key1)' '
	count=`flux module stats content-sqlite | jq ".checkpoints.key1 | length"`
	test $count -eq 5
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
