#!/bin/sh
#

test_description='Test kvs checkpointing works'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

if ! test "$TEST_LONG" = "t" && ! test "$LONGTEST" = "t"; then
    skip_all='kvs checkpoint valid only run under LONGTEST'
    test_done
fi

LOOPAPPEND=${FLUX_BUILD_DIR}/t/kvs/loop_append
RPC=${FLUX_BUILD_DIR}/t/request/rpc
THREADS=16

export FLUX_CONF_DIR=$(pwd)

test_expect_success 'configure checkpoint-period, place initial value' '
	cat >kvs.toml <<-EOT
	[kvs]
	checkpoint-period = "5s"
	EOT
'

# N.B. if wish to hand test with more "stress", incrase threads,
# batchcount, and/or sleep.

test_expect_success NO_CHAIN_LINT 'kvs: start instance that creates tons of appended data' '
	statedir=$(mktemp -d --tmpdir=${TMPDIR:-/tmp}) &&
	echo $statedir &&
	flux start --setattr=statedir=${statedir} ${LOOPAPPEND} --batch-count=50 --threads=${THREADS} mydata &
	pid=$! &&
	sleep 60 &&
	kill -s 9 $pid
'

test_expect_success 'kvs: create get checkpoint script' '
	cat >checkpointget.sh <<-EOT &&
	jq -j -c -n  "{key:\"kvs-primary\"}" | $RPC content.checkpoint-get
	EOT
	chmod 700 checkpointget.sh
'

test_expect_success 'kvs: make sure there was atleast one checkpoint' '
	flux start --recovery=${statedir} ./checkpointget.sh
'

test_expect_success 'kvs: create get data script' '
	cat >dataget.sh <<-EOT &&
	N=\$((${THREADS} - 1))
	for i in \$(seq 0 \${N})
	do
		flux kvs get mydata\${i} > ${statedir}/data\${i}.out 2> ${statedir}/data\${i}.err
		if [[ \$? -ne 0 ]]
		then
			exit 1
		fi
	done
	EOT
	chmod 700 dataget.sh
'

test_expect_success 'kvs: make sure we can get all the data' '
	flux start --recovery=${statedir} ./dataget.sh &&
	num=$(wc -l ${statedir}/data*.out | tail -n 1 | awk "{print \$1}") &&
	echo "total output lines = $num" &&
	test ${num} -ne 0 &&
	num=$(wc -l ${statedir}/data*.err | tail -n 1 | awk "{print \$1}") &&
	echo "total error lines = $num" &&
	test ${num} -eq 0
'

test_done
