#!/bin/sh

test_description='Test content-files checkpointing'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=1
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'load content, content-files module' '
	flux module load content &&
	flux module load content-files
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

# use grep instead of compare, incase of floating point rounding
test_expect_success 'checkpoint-get returned correct timestamp' '
        checkpoint_get | jq -r .value[0].timestamp >timestamp.out &&
        grep 2.2 timestamp.out
'

test_expect_success 'checkpoint-put updates rootref to baz' '
        checkpoint_put baz
'

test_expect_success 'checkpoint-get returned rootref baz' '
        echo baz >rootref2.exp &&
        checkpoint_get | jq -r .value[0].rootref >rootref2.out &&
        test_cmp rootref2.exp rootref2.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (2)' '
        flux content checkpoint list --no-header > checkpoints2.out &&
        count=$(cat checkpoints2.out | wc -l) &&
        test $count -eq 1 &&
        grep baz checkpoints2.out
'

test_expect_success 'reload content-files module' '
	flux module reload content-files
'

test_expect_success 'checkpoint-get still returns rootref baz' '
        echo baz >rootref3.exp &&
        checkpoint_get | jq -r .value[0].rootref >rootref3.out &&
        test_cmp rootref3.exp rootref3.out
'

test_expect_success 'checkpoint-put updates rootref with longer rootref' '
        checkpoint_put abcdefghijklmnopqrstuvwxyz
'

test_expect_success 'checkpoint-get returned rootref with longer rootref' '
        echo abcdefghijklmnopqrstuvwxyz >rootref4.exp &&
        checkpoint_get | jq -r .value[0].rootref >rootref4.out &&
        test_cmp rootref4.exp rootref4.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (3)' '
        flux content checkpoint list --no-header > checkpoints3.out &&
        count=$(cat checkpoints3.out | wc -l) &&
        test $count -eq 1 &&
        grep abcdefghijklmnopqrstuvwxyz checkpoints3.out
'

test_expect_success 'checkpoint-put updates rootref to shorter rootref' '
        checkpoint_put foobar
'

test_expect_success 'checkpoint-get returned rootref with shorter rootref' '
        echo foobar >rootref5.exp &&
        checkpoint_get | jq -r .value[0].rootref >rootref5.out &&
        test_cmp rootref5.exp rootref5.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (4)' '
        flux content checkpoint list --no-header > checkpoints4.out &&
        count=$(cat checkpoints4.out | wc -l) &&
        test $count -eq 1 &&
        grep foobar checkpoints4.out
'

test_expect_success 'checkpoint-put updates rootref to boof' '
        checkpoint_put boof
'

test_expect_success 'checkpoint-backing-get returns rootref boof' '
        echo boof >rootref_backing.exp &&
        checkpoint_backing_get \
            | jq -r .value[0].rootref >rootref_backing.out &&
        test_cmp rootref_backing.exp rootref_backing.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (5)' '
        flux content checkpoint list --no-header > checkpoints5.out &&
        count=$(cat checkpoints5.out | wc -l) &&
        test $count -eq 1 &&
        grep boof checkpoints5.out
'

test_expect_success 'checkpoint-backing-put w/ rootref poof' '
        checkpoint_backing_put poof
'

test_expect_success 'checkpoint-get returned rootref boof' '
        echo poof >rootref6.exp &&
        checkpoint_get | jq -r .value[0].rootref >rootref6.out &&
        test_cmp rootref6.exp rootref6.out
'

test_expect_success 'flux content checkpoint list shows correct checkpoints (6)' '
        flux content checkpoint list --no-header > checkpoints6.out &&
        count=$(cat checkpoints6.out | wc -l) &&
        test $count -eq 1 &&
        grep poof checkpoints6.out
'

test_expect_success 'checkpoint-put bad request fails with EPROTO' '
	$RPC content.checkpoint-put 71 "Protocol error" </dev/null
'

test_expect_success 'reload content-files module' '
	flux module reload content-files
'

test_expect_success 'checkpoint-put works' '
	checkpoint_put ref1
'

test_expect_success 'checkpoint-get returns most recent checkpoint' '
	echo ref1 >rootref.exp &&
	checkpoint_get | jq -r .value[0].rootref >rootref.out &&
	test_cmp rootref.exp rootref.out
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

test_expect_success 'remove content-files module' '
	flux module remove content-files
'

test_expect_success 'checkpoint-put w/ rootref spoon fails without backing' '
       test_must_fail checkpoint_put spoon
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
