#!/bin/sh
#

test_description='Test that Flux can launch Flux'

. `dirname $0`/sharness.sh

mock_bootstrap_instance
test_under_flux 4 wreck

test_expect_success 'recurse: Flux launches Flux ' '
	printenv FLUX_URI >old_uri &&
	test -s old_uri &&
	flux wreckrun -n1 -N1 flux broker \
		printenv FLUX_URI >new_uri &&
	test -s new_uri &&
	! test_cmp old_uri new_uri
'

test_expect_success 'recurse: local-uri is identical to FLUX_URI' '
	printenv FLUX_URI >cur_uri &&
	test -s cur_uri &&
	flux getattr local-uri >attr_uri &&
	test -s attr_uri &&
	test_cmp cur_uri attr_uri
'

test_expect_success 'recurse: parent-uri is unset in bootstrap instance' '
	! flux getattr parent-uri
'

test_expect_success 'recurse: local-uri != local-uri in enclosing instance' '
	flux getattr local-uri >enc_uri &&
	test -s enc_uri &&
	flux wreckrun -n1 -N1 flux broker \
		flux getattr local-uri >attr_uri &&
	test -s attr_uri &&
	! test_cmp enc_uri attr_uri
'

test_expect_success 'recurse: parent-uri == local-uri in enclosing instance' '
	flux getattr local-uri >enc_uri &&
	test -s enc_uri &&
	flux wreckrun -n1 -N1 flux broker \
		flux getattr parent-uri >attr_uri &&
	test -s attr_uri &&
	test_cmp enc_uri attr_uri
'

test_expect_success 'recurse: Flux launches Flux launches Flux' '
	flux wreckrun -n1 -N1 flux broker \
		flux wreckrun -n1 -N1 flux broker \
			echo hello >hello_out &&
	echo hello >hello_expected &&
	test_cmp hello_expected hello_out
'

test_done
