#!/bin/sh
#
# ci=system

test_description='test handle reconnection
'

. `dirname $0`/sharness.sh

test_under_flux 2 system -Slog-stderr-mode=local

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

# Usage: geturi rank
geturi () {
	local rank=$1
	echo $FLUX_URI | sed -e "s/local-0/local-$rank/"
}

test_expect_success 'flux-proxy works on rank 1 broker' '
	flux proxy $(geturi 1) flux getattr instance-level
'

test_expect_success 'flux-proxy --reconnect survives broker restart' '
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
		sh -c "FLUX_HANDLE_TRACE=1 flux proxy --reconnect $(geturi 1) ./test1.sh"
'

test_expect_success 'handle purges RPCs pending across broker restart' '
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
		sh -c "FLUX_HANDLE_TRACE=1 flux proxy --reconnect $(geturi 1) ./test2.sh"
'

test_done
