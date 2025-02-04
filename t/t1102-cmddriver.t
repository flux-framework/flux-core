#!/bin/sh
#

test_description='Test command driver

Verify flux command driver behavior.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE} minimal
path_printenv=$(which printenv)

test_expect_success 'baseline works' '
	flux getattr size
'

test_expect_success 'flux prepends to FLUX_MODULE_PATH' '
	(FLUX_MODULE_PATH=/xyz \
		flux $path_printenv | grep "FLUX_MODULE_PATH=.*:/xyz")
'

test_expect_success 'flux prepends to FLUX_CONNECTOR_PATH' '
        (FLUX_CONNECTOR_PATH=/xyz \
		flux $path_printenv | grep "FLUX_CONNECTOR_PATH=.*:/xyz")
'

test_expect_success 'flux fails for unknown connector scheme' '
	(FLUX_URI=noexist:// \
		test_must_fail flux getattr size)
'

test_expect_success 'flux fails for unknown connector path' '
	(FLUX_URI=local://noexist \
		test_must_fail flux getattr size)
'

test_expect_success 'flux fails fails for non-connector dso' '
	(FLUX_CONNECTOR_PATH=${FLUX_BUILD_DIR}/src/modules \
	FLUX_URI=kvs:// \
		test_must_fail flux getattr size)
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
test_expect_success 'flux env prepends to PYTHONPATH' '
	expected=$(flux config builtin python_path | sed "s/:.*//") &&
	result=$(PYTHONPATH= flux env sh -c "echo \$PYTHONPATH") &&
	test_debug "echo expecting PYTHONPATH=$expected got $result" &&
	echo "$result" | grep $expected
'
test_expect_success 'flux env outputs PYTHONPATH' '
	expected=$(flux config builtin python_path | sed "s/:.*//") &&
	result=$(PYTHONPATH= flux env | grep PYTHONPATH) &&
	test_debug "echo expecting PYTHONPATH=$expected got $result" &&
	echo "$result" | grep $expected
'
test_expect_success 'flux python prepends to PYTHONPATH' '
	expected=$(flux config builtin python_path | sed "s/:.*//") &&
	test_debug "echo expecting PYTHONPATH=$expected" &&
	PYTHONPATH= \
		flux python -c "import os; print(os.environ[\"PYTHONPATH\"])" \
		| grep $expected
'
test_expect_success 'flux does not prepend to PYTHONPATH' '
	printenv=$(which printenv) &&
	( unset PYTHONPATH &&
	  test_must_fail flux $printenv PYTHONPATH)
'
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


# N.B.: In the tests below we need to ensure that /bin:/usr/bin appear
# in PATH so that core utilities like `ls` can be found by the libtool
# wrappers or other processes invoked by calling `flux`. However, this
# means the results of the test can be influenced by whether or not a
# flux executable appears in /bin or /usr/bin. Therefore, care must be
# taken to ensure consistent results no matter what is in these paths.
#
# Ensure a bogus 'flux' executable occurs first in path, then make sure
# command -v flux finds the right flux:
#
test_expect_success 'cmddriver adds its own path to PATH' '
	mkdir bin &&
	cat <<-EOF >bin/flux &&
	#!/bin/sh
	true
	EOF
	chmod +x bin/flux &&
	fluxcmd=$(command -v flux) &&
	result=$(PATH=$(pwd)/bin:/bin:/usr/bin \
	         $fluxcmd env sh -c "command -v flux") &&
	test_debug "echo result=$result" &&
	test "$result" = "$fluxcmd"
'
# Use bogus flux in PATH and ensure flux cmddriver inserts its own path
# just before this path, not at front of PATH.
test_expect_success 'cmddriver inserts its path at end of PATH' '
	fluxdir=$(dirname $fluxcmd) &&
	result=$(PATH=/foo:$(pwd)/bin:/bin:/usr/bin \
	         $fluxcmd env printenv PATH) &&
	test_debug "echo result=$result" &&
	test "$result" = "/foo:$fluxdir:$(pwd)/bin:/bin:/usr/bin"
'
# Ensure a PATH that already returns current flux first is not modified
# by flux(1)
#
# The following test assumes flux(1) is not in /bin or /usr/bin. Skip the
# test if so.
#
fluxdir=$(dirname $fluxcmd)
test "$fluxdir" = "/bin" -o "$fluxdir" = "/usr/bin" || test_set_prereq NOBINDIR
test_expect_success READLINK,NOBINDIR 'cmddriver does not adjust PATH if unnecessary' '
	fluxdir=$(dirname $fluxcmd) &&
	mypath=/foo:/bar:$fluxdir:/usr/bin:/bin &&
	newpath=$(PATH=$mypath $fluxcmd env $path_printenv PATH) &&
	test_debug "echo PATH=$newpath" &&
	test "$newpath" = "$mypath"
'
test_expect_success 'FLUX_*_PREPEND environment variables work' '
	( FLUX_CONNECTOR_PATH_PREPEND=/foo \
	  flux $path_printenv | grep "FLUX_CONNECTOR_PATH=/foo" &&
	FLUX_EXEC_PATH_PREPEND=/foo \
	  flux $path_printenv | grep "FLUX_EXEC_PATH=/foo" &&
	FLUX_MODULE_PATH_PREPEND=/foo \
	  flux $path_printenv | grep "FLUX_MODULE_PATH=/foo" &&
	FLUX_LUA_PATH_PREPEND=/foo \
	  flux $path_printenv | grep "LUA_PATH=/foo" &&
	FLUX_LUA_CPATH_PREPEND=/foo \
	  flux $path_printenv | grep "LUA_CPATH=/foo" &&
	FLUX_PYTHONPATH_PREPEND=/foo \
	  flux $path_printenv | grep "PYTHONPATH=/foo")
'
test_expect_success 'environment variables are prepended in correct order' '
	( FLUX_EXEC_PATH_PREPEND=/foo:/bar \
	  flux $path_printenv FLUX_EXEC_PATH > prepend.out ) &&
	test_debug "cat prepend.out" &&
	grep "^/foo:/bar:" prepend.out
'
test_expect_success 'flux-env output can be passed to eval' '
    (eval $(flux env))
'
test_done
