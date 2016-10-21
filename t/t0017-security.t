#!/bin/sh

test_description='Test broker security' 

. `dirname $0`/sharness.sh

test_under_flux 4 minimal

test_expect_success 'verify fake munge encoding of messages' '
	${FLUX_BUILD_DIR}/src/test/tmunge --fake
'

test_done
