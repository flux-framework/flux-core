#!/bin/sh

test_description='Test content-sqlite checkpointing'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=1
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"
WRITECMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-write.py"

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
	flux module remove content-sqlite &&
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

test_expect_success 'checkpoint-put w/ different rootrefs' '
	checkpoint_put ref1 &&
	checkpoint_put ref2 &&
	checkpoint_put ref3 &&
	checkpoint_put ref4 &&
	checkpoint_put ref5 &&
	checkpoint_put ref6
'

test_expect_success 'checkpoint-get returns most recent checkpoint' '
	echo ref6 >rootref.exp &&
	checkpoint_get | jq -r .value | jq -r .rootref >rootref.out &&
	test_cmp rootref.exp rootref.out
'

test_expect_success 'content-sqlite stores only max of 5 checkpoints' '
	count=`flux module stats content-sqlite | jq ".checkpoints | length"` &&
	test $count -eq 5
'

test_expect_success 'content-sqlite the first checkpoint is the most recent' '
	flux module stats content-sqlite | jq ".checkpoints[0].value" | grep ref6
'

test_expect_success 'content-sqlite the oldest checkpoint is not listed' '
	flux module stats content-sqlite | jq ".checkpoints" > checkpoints.out &&
	test_must_fail grep ref1 checkpoints.out
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
