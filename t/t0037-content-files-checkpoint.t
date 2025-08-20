#!/bin/sh

test_description='Test content-files checkpointing'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=1
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} minimal

test_expect_success 'load content module' '
	flux module load content
'

test_expect_success 'load content-files module' '
	flux module load content-files
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

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
