#!/bin/sh

test_description='Test content-sqlite checkpointing'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=2
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"
WRITECMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-write.py"

test_expect_success 'load content, content-sqlite module' '
	flux exec flux module load content &&
	flux module load content-sqlite
'

test_expect_success 'checkpoint-get returns ENOENT if there is no checkpoint' '
	test_must_fail checkpoint_get 2> get.err &&
	grep "No such file or directory" get.err
'

test_expect_success 'checkpoint-put w/ rootref bar' '
	checkpoint_put bar
'

test_expect_success 'checkpoint-get returned rootref bar' '
	echo bar >rootref.exp &&
	checkpoint_get | jq -r .value[0].rootref >rootref.out &&
	test_cmp rootref.exp rootref.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (1 default)' '
        flux content checkpoint list > checkpoints1.out &&
        count=$(cat checkpoints1.out | wc -l) &&
        test $count -eq 2 &&
        tail -n 1 checkpoints1.out | grep bar
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (1 no-header)' '
        flux content checkpoint list --no-header > checkpoints1n.out &&
        count=$(cat checkpoints1n.out | wc -l) &&
        test $count -eq 1 &&
        head -n 1 checkpoints1n.out | grep bar
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (1 json)' '
        flux content checkpoint list --json > checkpoints1j.out &&
        count=$(cat checkpoints1j.out | wc -l) &&
        test $count -eq 1 &&
        head -n 1 checkpoints1j.out | jq -e ".rootref == \"bar\""
'

test_expect_success 'checkpoint-put on rank 1 forwards to rank 0' '
       o=$(checkpoint_put_msg rankref) &&
       jq -j -c -n ${o} | flux exec -r 1 ${RPC} content.checkpoint-put
'

test_expect_success 'checkpoint-get on rank 1 forwards to rank 0' '
       echo rankref >rankref.exp &&
       o=$(checkpoint_get_msg kvs-primary) &&
       jq -j -c -n ${o} \
	   | flux exec -r 1 ${RPC} content.checkpoint-get \
	   | jq -r .value[0].rootref > rankref.out &&
       test_cmp rankref.exp rankref.out
'

# use grep instead of compare, incase of floating point rounding
test_expect_success 'checkpoint-get returned correct timestamp' '
        checkpoint_get | jq -r .value[0].timestamp >timestamp.out &&
        grep 2.2 timestamp.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (2)' '
        flux content checkpoint list --no-header > checkpoints2.out &&
        count=$(cat checkpoints2.out | wc -l) &&
        test $count -eq 2 &&
        head -n 1 checkpoints2.out | grep rankref
'

test_expect_success 'checkpoint-put updates rootref to baz' '
	checkpoint_put baz
'

test_expect_success 'checkpoint-get returned rootref baz' '
	echo baz >rootref2.exp &&
	checkpoint_get | jq -r .value[0].rootref >rootref2.out &&
	test_cmp rootref2.exp rootref2.out
'

test_expect_success 'flush + reload content-sqlite module on rank 0' '
	flux content flush &&
	flux module reload content-sqlite
'

test_expect_success 'checkpoint-get still returns rootref baz' '
	echo baz >rootref3.exp &&
	checkpoint_get | jq -r .value[0].rootref >rootref3.out &&
	test_cmp rootref3.exp rootref3.out
'

test_expect_success 'checkpoint-backing-get returns rootref baz' '
	echo baz >rootref_backing.exp &&
	checkpoint_backing_get \
            | jq -r .value[0].rootref >rootref_backing.out &&
	test_cmp rootref_backing.exp rootref_backing.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (3)' '
        flux content checkpoint list --no-header > checkpoints3.out &&
        count=$(cat checkpoints3.out | wc -l) &&
        test $count -eq 3 &&
        head -n 1 checkpoints3.out | grep baz
'

test_expect_success 'checkpoint-backing-put w/ rootref boof' '
	checkpoint_backing_put boof
'

test_expect_success 'checkpoint-get returned rootref boof' '
	echo boof >rootref4.exp &&
	checkpoint_get | jq -r .value[0].rootref >rootref4.out &&
	test_cmp rootref4.exp rootref4.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (4)' '
        flux content checkpoint list --no-header > checkpoints4.out &&
        count=$(cat checkpoints4.out | wc -l) &&
        test $count -eq 4 &&
        head -n 1 checkpoints4.out | grep boof
'

test_expect_success 'content-backing.load wrong size hash fails with EPROTO' '
	echo -n xxx >badhash &&
	$RPC content-backing.load 71 <badhash 2>load.err
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
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
	checkpoint_get | jq -r .value[0].rootref >rootref.out &&
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

test_expect_success 'flux content checkpoint update works' '
	echo sha1-1234567890123456789012345678901234567890 > updatedcheckpt.exp &&
	flux content checkpoint update $(cat updatedcheckpt.exp) &&
	checkpoint_get | jq -r .value[0].rootref > updatedcheckpt.out &&
	test_cmp updatedcheckpt.exp updatedcheckpt.out
'

test_expect_success 'flux content checkpoint update fails on invalid ref' '
	test_must_fail flux content checkpoint update foo1-1234
'

test_expect_success 'load kvs' '
	flux module load kvs
'

test_expect_success 'flux content checkpoint update fails if KVS loaded' '
	test_must_fail flux content checkpoint update \
		sha1-1234567890123456789012345678901234567890
'

test_expect_success 'remove kvs' '
	flux module remove kvs
'

test_expect_success 'remove content-sqlite module' '
	flux module remove content-sqlite
'

test_expect_success 'checkpoint-put w/ rootref spoon fails without backing' '
       test_must_fail checkpoint_put spoon
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
