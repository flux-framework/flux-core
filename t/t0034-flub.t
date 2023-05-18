#!/bin/sh
#

test_description='Test flub bootstrap method'

. `dirname $0`/sharness.sh

test_under_flux 8 full

export FLUX_URI_RESOLVE_LOCAL=t

# usage: get_job_uri id
get_job_uri() {
	flux job wait-event -t10 $1 memo >/dev/null && flux uri $1
}

# usage: wait_for_service uri name
wait_for_service() {
	flux proxy $1 bash -c \""while ! flux ping -c 1 $2 >/dev/null 2>&1; do sleep 0.5; done"\"
}

test_expect_success 'broker fails with bad broker.boot-server' '
	test_must_fail flux broker \
	    -Sbroker.rc1_path= -Sbroker.rc3_path= \
	    -Sbroker.boot-server=local://noexist/path \
	    /bin/true 2>server.err &&
	grep "was not found" server.err
'

test_expect_success 'start a 1 node job with 0 extra ranks' '
	id=$(flux batch -N1 --wrap sleep inf) &&
	get_job_uri $id >test1.uri
'
test_expect_success 'job has size 1' '
	size=$(flux proxy $(cat test1.uri) flux getattr size) &&
	test $size -eq 1
'
test_expect_success 'flub bootstrap fails with no available ranks' '
	test_must_fail flux broker \
	    -Sbroker.boot-server=$(cat test1.uri) 2>noranks.err &&
	grep "no available ranks" noranks.err
'
test_expect_success 'clean up' '
	flux cancel --all
'


#
# Start 2 node batch job with one extra slot.
# Submit 1 node broker job that fills the slot.
# Run a parallel job across all three nodes in the batch job.
# This test is constrained so that all flubbed nodes are leaf nodes,
# and the flubbed nodes connect to rank 0 only.

test_expect_success 'create config with 3 fake nodes' '
	cat >fake3.toml <<-EOT
	[resource]
	noverify = true
	[[resource.config]]
	hosts = "a,b,c"
	cores = "0-3"
	EOT
'
test_expect_success 'start a 2 node job with 1 extra rank' '
	id=$(flux batch -N2 \
	    --broker-opts=--config-path=fake3.toml \
	    --broker-opts=-Ssize=3 \
	    --broker-opts=-Sbroker.quorum=2 \
	    --broker-opts=-Stbon.topo=kary:0 \
	    --wrap sleep inf) &&
	get_job_uri $id >test2.uri
'
test_expect_success 'job has size 3' '
	size=$(flux proxy $(cat test2.uri) flux getattr size) &&
	test $size -eq 3
'
test_expect_success 'overlay status shows extra node offline' '
	flux proxy $(cat test2.uri) \
	    flux overlay status --no-pretty >ov2.out &&
	grep "2 extra0: offline" ov2.out
'
test_expect_success 'run a 2 node job in the initial instance' '
	wait_for_service $(cat test2.uri) job-ingest &&
	run_timeout 30 flux proxy $(cat test2.uri) \
	    flux run --label-io -N2 flux pmi barrier
'
test_expect_success 'submit a job that starts 1 extra broker' '
	id=$(flux submit -N1 flux broker \
	    --config-path=fake3.toml \
	    -Stbon.topo=kary:0 \
	    -Sbroker.boot-server=$(cat test2.uri)) &&
	flux job wait-event -p guest.exec.eventlog $id shell.start
'
test_expect_success 'wait for overlay status to be full' '
	flux proxy $(cat test2.uri) \
	    flux overlay status --summary --wait full --timeout 30s
'
test_expect_success 'run a 3 node job in the expanded instance' '
	run_timeout 30 flux proxy $(cat test2.uri) \
	    flux run --label-io -N3 flux pmi barrier
'
test_expect_success 'clean up' '
	flux cancel --all
'

test_expect_success 'create config with 7 fake nodes' '
	cat >fake7.toml <<-EOT
	[resource]
	noverify = true
	[[resource.config]]
	hosts = "a,b,c,d,e,f,g"
	cores = "0-3"
	EOT
'

#
# Start 1 node batch job with 6 extra slots (kary:2).
# Submit 6 node broker job that fills all the slots.
# Run a 7 node parallel job.
#
test_expect_success 'start a 1 node job with 6 extra ranks' '
	id=$(flux batch -N1 \
	    --broker-opts=--config-path=fake7.toml \
	    --broker-opts=-Ssize=7 \
	    --broker-opts=-Sbroker.quorum=1 \
	    --broker-opts=-Stbon.topo=kary:2 \
	    --wrap sleep inf) &&
	get_job_uri $id >test5.uri
'
test_expect_success 'run a 1 node job in the initial instance' '
	wait_for_service $(cat test5.uri) job-ingest &&
	run_timeout 30 flux proxy $(cat test5.uri) \
	    flux run --label-io -N1 flux pmi barrier
'
test_expect_success 'job has size 7' '
	size=$(flux proxy $(cat test5.uri) flux getattr size) &&
	test $size -eq 7
'
# N.B. include exit-timeout=none so we can safely disconnect one node later
test_expect_success 'submit a job that starts 6 extra brokers' '
	id=$(flux submit -N6 -o exit-timeout=none \
	    flux broker \
	    --config-path=fake7.toml \
	    -Stbon.topo=kary:2 \
	    -Sbroker.boot-server=$(cat test5.uri)) &&
	flux job wait-event -p guest.exec.eventlog $id shell.start &&
	echo $id >xtra_id
'
test_expect_success 'wait for overlay status to be full' '
	flux proxy $(cat test5.uri) \
	    flux overlay status --summary --wait full --timeout 10s
'
test_expect_success 'run a 7 node job in the expanded instance' '
	run_timeout 30 flux proxy $(cat test5.uri) \
	    flux run --label-io -N7 flux pmi barrier
'

#
# Show that a node can be replaced

test_expect_success 'disconnect rank 6' '
	flux proxy $(cat test5.uri) \
	    flux overlay disconnect 6
'
test_expect_success 'rank 6 cannot be pinged - trigger EHOSTUNREACH' '
	test_must_fail flux proxy $(cat test5.uri) \
	    flux ping -c1 6
'
test_expect_success 'wait for overlay status to be degraded' '
	flux proxy $(cat test5.uri) \
	    flux overlay status --summary --wait degraded --timeout 10s
'
test_expect_success 'submit a job that starts 1 broker' '
	id=$(flux submit -N1 flux broker \
	    --config-path=fake7.toml \
	    -Stbon.topo=kary:2 \
	    -Sbroker.boot-server=$(cat test5.uri)) &&
	flux job wait-event -p guest.exec.eventlog $id shell.start
'
test_expect_success 'wait for overlay status to be full' '
	flux proxy $(cat test5.uri) \
	    flux overlay status --summary --wait full --timeout 10s
'
test_expect_success 'run a 7 node job in the expanded instance' '
	run_timeout 30 flux proxy $(cat test5.uri) \
	    flux run --label-io -N7 flux pmi barrier
'

test_expect_success 'clean up' '
	flux cancel --all
'

test_done
