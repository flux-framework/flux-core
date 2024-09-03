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

test_expect_success 'checkpoint-get fails, no backing store' '
        test_must_fail checkpoint_get foo
'

test_expect_success 'checkpoint-put fails, no backing store' '
        test_must_fail checkpoint_put foo bar
'

test_expect_success 'flux-dump --checkpoint with no backing store' '
        test_must_fail flux dump --checkpoint foo.tar
'

test_expect_success 'content.backing-module input of none works' '
        flux start -Scontent.backing-module=none true
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
