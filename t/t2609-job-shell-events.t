#!/bin/sh
#
test_description='Test flux-shell emitted events'

. `dirname $0`/sharness.sh

test_under_flux 2 job

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/flux-shell"

INITRC_TESTDIR="${SHARNESS_TEST_SRCDIR}/shell/initrc"
INITRC_PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"

test_expect_success 'flux-shell: 1N: init and start shell events are emitted' '
	id=$(flux submit -n1 -N1 true)  &&
	flux job wait-event -vt 5 -p exec \
		-m leader-rank=0 ${id} shell.init &&
	flux job wait-event -vt 5 -p exec \
		${id} shell.init  &&
	flux job wait-event -vt 5 -p exec \
		${id} shell.start
'
test_expect_success 'flux-shell: 2N: init and start shell events are emitted' '
	id=$(flux submit -n4 -N2 true)  &&
	flux job wait-event -vt 5 -p exec \
		-m leader-rank=0  ${id} shell.init &&
	flux job wait-event -vt 5 -p exec \
		-m size=2 ${id} shell.init  &&
	flux job wait-event -vt 5 -p exec \
		${id} shell.start
'
test_expect_success 'flux-shell: plugin can add event context' '
	cat >test-event.lua <<-EOT &&
	plugin.searchpath = "${INITRC_PLUGINPATH}"
	plugin.load { file = "test-event.so" }
	EOT
	id=$(flux submit -o initrc=test-event.lua -n2 -N2 hostname) &&
	flux job wait-event -vt 5 -p exec \
		-m event-test=foo ${id} shell.init

'
test_done
