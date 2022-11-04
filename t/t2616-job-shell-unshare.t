#!/bin/sh
#
test_description='Test flux-shell unshare support'

. `dirname $0`/sharness.sh

if ! unshare -U /bin/true; then
    skip_all='skipping unshare tests since system does not permit unshare(2)'
    test_done
fi

test_under_flux 1

test_expect_success 'flux-shell: unshare=user,maproot sets uid to 0' '
	NEWUID=$(flux mini run -o unshare=user,maproot id -u) &&
	test $NEWUID -eq 0
'
test_expect_success 'flux-shell: unshare=user,maproot,mount enables bind mounting' '
	cat >bindtest.sh <<-EOT &&
	#!/bin/bash -e
	mkdir src dst
	mount --bind $(pwd)/src $(pwd)/dst
	touch src/foo
	test -f dst/foo
	EOT
	chmod +x bindtest.sh &&
	flux mini run -o unshare=user,maproot,mount ./bindtest.sh
'
test_expect_success 'flux-shell: unshare works without arguments' '
	flux mini run -o unshare /bin/true
'
test_expect_success 'flux-shell: unshare rejects unknown argument' '
	test_must_fail flux mini run -o unshare=badarg /bin/true
'

test_done
