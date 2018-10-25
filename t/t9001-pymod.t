#!/bin/sh
#

test_description='Test pymod

Simple sanity check that pymod can load example script.'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

if ! python -c ''; then
	skip_all='skipping pymod tests, python not available'
	test_done
fi

test_under_flux 1

pymodpath="${SHARNESS_TEST_SRCDIR}/../src/modules/pymod"

test_expect_success 'load pymod with echo.py' '
	flux module load pymod --verbose --path=$pymodpath echo
'
test_expect_success 'pymod echo.py function works' '
	cat <<-EOF | flux python -
	import flux,sys
	p = { "data": "foo" }
	f = flux.Flux()
	r = f.rpc_send ("echo.foo", p)
	sys.exit (not all(item in r.items() for item in p.items()))
EOF
'
test_expect_success 'unload pymod' '
	flux module remove pymod
'

test_done
