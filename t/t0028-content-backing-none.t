#!/bin/sh

test_description='Test broker content checkpoint w/o backing module'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 2 minimal
echo "# $0: flux session size will be ${SIZE}"

RPC=${FLUX_BUILD_DIR}/t/request/rpc

test_expect_success 'loaded content module' '
	flux exec flux module load content
'

test_expect_success 'checkpoint-get fails, no checkpoints yet' '
        test_must_fail checkpoint_get foo
'

test_expect_success 'checkpoint-put foo w/ rootref bar' '
        checkpoint_put foo bar
'

test_expect_success 'checkpoint-get foo returned rootref bar' '
        echo bar >rootref.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref.out &&
        test_cmp rootref.exp rootref.out
'

test_expect_success 'checkpoint-put on rank 1 forwards to rank 0' '
        o=$(checkpoint_put_msg rankone rankref) &&
        jq -j -c -n ${o} | flux exec -r 1 ${RPC} content.checkpoint-put
'

test_expect_success 'checkpoint-get on rank 1 forwards to rank 0' '
        echo rankref >rankref.exp &&
        o=$(checkpoint_get_msg rankone) &&
        jq -j -c -n ${o} \
            | flux exec -r 1 ${RPC} content.checkpoint-get \
            | jq -r .value | jq -r .rootref > rankref.out &&
        test_cmp rankref.exp rankref.out
'

test_expect_success 'flux-dump --checkpoint with missing checkpoint fails' '
        test_must_fail flux dump --checkpoint foo.tar
'

test_expect_success 'load kvs and create some kvs data' '
        flux module load kvs &&
        flux kvs put a=1 &&
        flux kvs put b=foo
'

test_expect_success 'reload kvs' '
        flux module reload kvs &&
        test $(flux kvs get a) = "1" &&
        test $(flux kvs get b) = "foo"
'

test_expect_success 'unload kvs' '
        flux module remove kvs
'

test_expect_success 'dump default=kvs-primary checkpoint works' '
        flux dump --checkpoint foo.tar
'

test_expect_success 'restore content' '
        flux restore --checkpoint foo.tar
'

test_expect_success 'reload kvs' '
        flux module load kvs
'

test_expect_success 'verify KVS content restored' '
        test $(flux kvs get a) = "1" &&
        test $(flux kvs get b) = "foo"
'

test_expect_success 'unload kvs' '
        flux module remove kvs
'

test_expect_success 'content.backing-module input of none works' '
        flux start -Scontent.backing-module=none true
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
