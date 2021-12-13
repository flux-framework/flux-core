#!/bin/sh
#

test_description='test rlocal:// basic functionality
'

. `dirname $0`/sharness.sh

test_under_flux 2 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

# Usage: geturi rank
geturi () {
	local rank=$1
	echo r$FLUX_URI | sed -e "s/local-0/local-$rank/"
}

test_expect_success 'tell brokers to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'

test_expect_success 'rlocal:// works on rank 0' '
	(FLUX_HANDLE_TRACE=1 FLUX_URI=$(geturi 0) flux getattr instance-level)
'
# N.B. throw a job submission in to exercise flux::owner connector option
test_expect_success 'rlocal:// works on rank 1' '
	(FLUX_URI=$(geturi 1) flux mini run -N2 -n2 hostname)
'
test_expect_success 'rlocal:// works on rank 1 with flux-proxy' '
	flux proxy $(geturi 1) printenv FLUX_URI >proxy.out &&
	test_must_fail grep rlocal:// proxy.out
'

# In the tests below, flux-proxy uses the rlocal:// connector,
# while the test scripts use the proxy local:// connector.

test_expect_success 'rlocal:// is still usable after broker restart' '
	cat >test1.sh <<-EOT &&
	#!/bin/sh -e
	unset FLUX_HANDLE_TRACE
	flux getattr instance-level
	$startctl kill 1 15
	$startctl wait 1
	$startctl run 1
	flux getattr instance-level
	EOT
	chmod +x test1.sh &&
	run_timeout 60 \
		sh -c "FLUX_HANDLE_TRACE=1 flux proxy $(geturi 1) ./test1.sh"
'

# Make sure broker 1 is has completed rc1 before next test
test_expect_success 'barrier' '
	flux mini run -N2 -n2 hostname
'

test_expect_success 'rlocal:// purges RPCs pending across broker restart' '
	cat >test2.sh <<-EOT &&
	#!/bin/sh -e
	unset FLUX_HANDLE_TRACE
	flux kvs get --waitcreate notakey 2>kvsget.err &
	$startctl kill 1 15
	$startctl wait 1
	$startctl run 1
	wait
	grep "Connection reset by peer" kvsget.err
	flux getattr instance-level
	EOT
	chmod +x test2.sh &&
	run_timeout 60 \
		sh -c "FLUX_HANDLE_TRACE=1 flux proxy $(geturi 1) ./test2.sh"
'

test_expect_success 'barrier' '
	flux mini run -N2 -n2 hostname
'

test_done
