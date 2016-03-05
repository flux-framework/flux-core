#!/bin/sh
#

test_description='Test command driver

Verify flux command driver behavior.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

test_expect_success 'baseline works' '
	flux comms info
'

test_expect_success 'flux prepends to FLUX_MODULE_PATH' '
	FLUX_MODULE_PATH=/xyz flux /usr/bin/printenv \
		| grep "FLUX_MODULE_PATH=.*:/xyz"
'

test_expect_success 'flux prepends to FLUX_CONNECTOR_PATH' '
        FLUX_CONNECTOR_PATH=/xyz flux /usr/bin/printenv \
		| grep "FLUX_CONNECTOR_PATH=.*:/xyz"
'

test_expect_success 'flux --uri sets FLUX_URI' '
	flux --uri xyz://foo /usr/bin/printenv \
		| egrep "^FLUX_URI=xyz://foo$"
'

test_expect_success 'flux --trace-handle sets FLUX_HANDLE_TRACE=1' '
	flux --trace-handle /usr/bin/printenv \
		| egrep "^FLUX_HANDLE_TRACE=1$"
'

# ENOENT
test_expect_success 'flux --uri fails for unknown connector' '
	test_must_fail flux --uri 'noexist://' comms info
'

# ENOENT
test_expect_success 'flux --uri fails for unknown path' '
	test_must_fail flux --uri 'local://noexist' comms info
'

# EINVAL
test_expect_success 'cmddriver: --uri fails for non-connector dso' '
	test_must_fail flux --connector-path ${FLUX_BUILD_DIR}/src/modules \
			       --uri kvs:// comms info
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
	flux --uri foo://xyz env | grep "^export FLUX_URI=\"foo://xyz"
'
test_expect_success 'flux env passes cmddriver option to argument' "
	flux --uri foo://xyx env sh -c 'echo \$FLUX_URI' \
		| grep ^foo://xyx$
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
	LUA_PATH='/meh;;;' flux env sh -c 'echo \$LUA_PATH' |
		grep -v ';;;;'
"
readlink --version >/dev/null && test_set_prereq READLINK
test_expect_success READLINK 'cmddriver adds its own path to PATH if called with relative path' "
	fluxcmd=\$(readlink -f \$(which flux)) &&
	fluxdir=\$(dirname \$fluxcmd) &&
	PATH='/bin:/usr/bin' \$fluxcmd env sh -c 'echo \$PATH' | grep ^\$fluxdir
"
test_expect_success 'flux --secdir overrides config' '
	umask 077 && tmpkeydir=`mktemp -d` &&
	flux env flux --secdir="$tmpkeydir" keygen &&
	ls $tmpkeydir >&2 &&
	test -f $tmpkeydir/curve/client_secret &&
	rm -rf $tmpkeydir
'

test_expect_success 'FLUX_*_PREPEND environment variables work' '
	FLUX_CONNECTOR_PATH_PREPEND=/foo \
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
	  flux /usr/bin/printenv | grep "PYTHONPATH=/foo"
'
test_expect_success 'flux-env output can be passed to eval' '
    eval $(flux env)
'
test_done
