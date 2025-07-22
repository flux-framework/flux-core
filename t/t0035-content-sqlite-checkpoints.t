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
	checkpoint_get | jq -r .value[0] | jq -r .rootref >rootref.out &&
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

#
# test conversion from checkpt 1.0 database
#

test_expect_success 'run an instance to create a checkpt_v2 table' '
	statedir=$(mktemp -d --tmpdir=${TMPDIR:-/tmp}) &&
	flux start --setattr=statedir=${statedir} "flux kvs put --sync foo=bar"
'

test_expect_success 'get most recent checkpoint' '
        $QUERYCMD -t 100 --nokey ${statedir}/content.sqlite \
                "select value from checkpt_v2 ORDER BY id DESC LIMIT 1" > checkptvalue.out &&
        cat checkptvalue.out | jq .rootref > checkptvaluerootref.out
'

test_expect_success 'drop checkpt_v2 table' '
        $WRITECMD -t 100 ${statedir}/content.sqlite \
                "DROP TABLE IF EXISTS checkpt_v2" &&
        $QUERYCMD -t 100 ${statedir}/content.sqlite \
                "select tbl_name from sqlite_master where type = \"table\"" > tables1.out &&
        test_must_fail grep checkpt_v2 tables1.out
'

test_expect_success 'create checkpt v1 table' '
        $WRITECMD -t 100 ${statedir}/content.sqlite \
                "create table checkpt(key TEXT, value TEXT)" &&
        $QUERYCMD -t 100 ${statedir}/content.sqlite \
                "select tbl_name from sqlite_master where type = \"table\"" > tables2.out &&
        grep "checkpt$" tables2.out
'

# setup insert outside of test to avoid tons of escape json text trickery
value=$(cat checkptvalue.out)
insert="insert into checkpt (key, value) values ('kvs-primary', '${value}')"
test_expect_success 'put checkpoint into checkpt v1 table' '
        $WRITECMD -t 100 ${statedir}/content.sqlite "${insert}" &&
        $QUERYCMD -t 100 ${statedir}/content.sqlite \
             "select * from checkpt" > checkpt.out &&
        grep $(cat checkptvaluerootref.out) checkpt.out
'

test_expect_success 'instance loads from v1 checkpt and converts to v2 checkpt table' '
	flux start --setattr=statedir=${statedir} "flux kvs get foo" > restart.out &&
        grep bar restart.out &&
        $QUERYCMD -t 100 ${statedir}/content.sqlite \
                "select tbl_name from sqlite_master where type = \"table\"" > tables3.out &&
        grep "checkpt_v2" tables3.out
'

test_expect_success 'old checkpoint is in v2 table' '
        $QUERYCMD -t 100 --nokey ${statedir}/content.sqlite \
                "select * from checkpt_v2" > checkptvalues.out &&
        grep $(cat checkptvaluerootref.out) checkptvalues.out
'

test_expect_success 'new checkpoint is storing multiple checkpoints' '
	flux start --setattr=statedir=${statedir} "flux kvs sync" &&
        $QUERYCMD -t 100 --nokey ${statedir}/content.sqlite \
                "select * from checkpt_v2" > checkptvalues2.out &&
	count=`grep rootref checkptvalues2.out | wc -l` &&
	test $count -gt 1
'

test_done
