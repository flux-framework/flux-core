#!/bin/sh

test_description='Test that libflux ignores the environment when setuid'

. `dirname $0`/sharness.sh

handle_open=${FLUX_BUILD_DIR}/t/util/handle-open

if test "$(id -u)" -eq 0; then
	setuid_owner=nobody
	SUDO=
elif sudo -n true 2>/dev/null; then
	setuid_owner=root
	SUDO="sudo -n"
else
	skip_all='skipping setuid tests, requires root or passwordless sudo'
	test_done
fi

test_expect_success 'create setuid copy of handle-open' '
	cp $handle_open ./handle-open-setuid &&
	$SUDO chown $setuid_owner ./handle-open-setuid &&
	$SUDO chmod 4755 ./handle-open-setuid
'
# The copy may not run setuid, e.g. on a nosuid file system
if test_have_prereq NO_ASAN \
	&& ./handle-open-setuid --is-setuid 2>/dev/null; then
	test_set_prereq SETUID
fi

test_expect_success 'create empty connector DSO' '
	mkdir fake &&
	touch fake/bogus.so
'
test_expect_success 'FLUX_URI and FLUX_CONNECTOR_PATH are used normally' '
	test_must_fail env FLUX_URI=bogus://x FLUX_CONNECTOR_PATH=$(pwd)/fake \
		$handle_open 2>normal.err &&
	test_debug "cat normal.err" &&
	grep "^flux_open: dlopen: " normal.err
'
test_expect_success SETUID 'FLUX_URI is ignored when setuid' '
	test_might_fail env FLUX_URI=bogus://x \
		./handle-open-setuid 2>uri.err &&
	test_debug "cat uri.err" &&
	test_must_fail grep bogus uri.err
'
test_expect_success SETUID 'FLUX_CONNECTOR_PATH is ignored when setuid' '
	test_must_fail env FLUX_CONNECTOR_PATH=$(pwd)/fake \
		./handle-open-setuid bogus://x 2>path.err &&
	test_debug "cat path.err" &&
	grep "Unable to find connector name" path.err &&
	test_must_fail grep fake path.err
'
# Run unconditionally so the setuid copy is removed even after a failure
test_expect_success 'remove setuid copy of handle-open' '
	$SUDO rm -f ./handle-open-setuid
'
test_done
