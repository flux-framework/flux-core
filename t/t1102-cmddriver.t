#!/bin/sh
#

test_description='Test command driver

Verify flux command driver behavior.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal

test_expect_success 'baseline works' '
	flux comms info
'

test_expect_success 'flux prepends to FLUX_MODULE_PATH' '
	(FLUX_MODULE_PATH=/xyz \
		flux /usr/bin/printenv | grep "FLUX_MODULE_PATH=.*:/xyz")
'

test_expect_success 'flux prepends to FLUX_CONNECTOR_PATH' '
        (FLUX_CONNECTOR_PATH=/xyz \
		flux /usr/bin/printenv | grep "FLUX_CONNECTOR_PATH=.*:/xyz")
'

test_expect_success 'flux fails for unknown connector scheme' '
	(FLUX_URI=noexist:// \
		test_must_fail flux comms info)
'

test_expect_success 'flux fails for unknown connector path' '
	(FLUX_URI=local://noexist \
		test_must_fail flux comms info)
'

test_expect_success 'flux fails fails for non-connector dso' '
	(FLUX_CONNECTOR_PATH=${FLUX_BUILD_DIR}/src/modules \
	FLUX_URI=kvs:// \
		test_must_fail flux comms info)
'

# Test flux 'env' builtin
test_expect_success 'flux env works' '
	flux env | grep /connectors
'
test_expect_success 'flux env runs argument' "
	flux env sh -c 'echo \$FLUX_CONNECTOR_PATH' \
		| grep /connectors
"
test_expect_success 'flux env passes cmddriver options' '
	(FLUX_URI=foo://xyz \
		flux env | grep "^export FLUX_URI=\"foo://xyz")
'
test_expect_success 'flux env passes cmddriver option to argument' "
	(FLUX_URI=foo://xyx \
		flux env sh -c 'echo \$FLUX_URI' | grep '^foo://xyx$')
"
# push /foo twice onto PYTHONPATH -- ensure it is leftmost position:
#test_expect_success 'cmddriver pushes dup path elements onto front of PATH' "
#	flux -P /foo env flux -P /bar env flux -P /foo env \
#		sh -c 'echo \$PYTHONPATH' | grep '^/foo'
#"
## Ensure PATH-style variables are de-duplicated on push
## Push /foo twice onto PYTHONPATH, ensure it appears only once
#test_expect_success 'cmddriver deduplicates path elements on push' "
#	flux -P /foo env flux -P /foo env sh -c 'echo \$PYTHONPATH' |
#		awk -F '/foo' 'NF-1 != 1 {print; exit 1}'
#"
## Ensure complex PATH-style variables are de-duplicated on push
#test_expect_success 'cmddriver deduplicates complex path elements on push' "
#	flux -P /foo:/foo:/foo:/bar:/foo:/baz env flux -P /foo env sh -c 'echo \$PYTHONPATH' |
#		awk -F '/foo' 'NF-1 != 1 {print; exit 1}'
#"
## External user path elements are preserved
#test_expect_success 'cmddriver preserves user path components' "
#	PYTHONPATH=/meh flux env sh -c 'echo \$PYTHONPATH' |
#		awk -F '/meh' 'NF-1 != 1 {print; exit 1}'
#"
test_expect_success 'cmddriver removes multiple contiguous separators in input' "
	(LUA_PATH='/meh;;;' \
		flux env sh -c 'echo \$LUA_PATH' | grep -v ';;;;')
"
readlink --version >/dev/null && test_set_prereq READLINK
test_expect_success READLINK 'cmddriver adds its own path to PATH if called with relative path' "
	fluxcmd=\$(readlink -f \$(which flux)) &&
	fluxdir=\$(dirname \$fluxcmd) &&
	PATH='/bin:/usr/bin' \$fluxcmd env sh -c 'echo \$PATH' | grep ^\$fluxdir
"

test_expect_success 'FLUX_*_PREPEND environment variables work' '
	( FLUX_CONNECTOR_PATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "FLUX_CONNECTOR_PATH=/foo" &&
	FLUX_EXEC_PATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "FLUX_EXEC_PATH=/foo" &&
	FLUX_MODULE_PATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "FLUX_MODULE_PATH=/foo" &&
	FLUX_LUA_PATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "LUA_PATH=/foo" &&
	FLUX_LUA_CPATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "LUA_CPATH=/foo" &&
	FLUX_PYTHONPATH_PREPEND=/foo \
	  flux /usr/bin/printenv | grep "PYTHONPATH=/foo")
'
test_expect_success 'flux-env output can be passed to eval' '
    (eval $(flux env))
'
test_done
