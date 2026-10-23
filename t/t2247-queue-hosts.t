#!/bin/sh
test_description='Test RFC 33 queue membership by hostlist (queues.NAME.hosts)'

. $(dirname $0)/sharness.sh

test_under_flux 4 full -Slog-stderr-level=1

# ------------------------------------------------------------------
# conf_policy.c validation (fatal config cases)
# ------------------------------------------------------------------

test_expect_success 'well formed queues.NAME.hosts works' '
	flux config load <<-EOT
	[queues.batch]
	hosts = "test[0-1]"
	[queues.debug]
	hosts = "all"
	EOT
'
test_expect_success 'queues.NAME.hosts must be a string' '
	test_must_fail flux config load 2>hoststype.err <<-EOT &&
	queues.batch.hosts = 1
	EOT
	grep "hosts. must be a string" hoststype.err
'
test_expect_success 'hosts and requires are mutually exclusive' '
	test_must_fail flux config load 2>hostsreq.err <<-EOT &&
	[queues.batch]
	hosts = "test0"
	requires = [ "batch" ]
	EOT
	grep "hosts. and .requires. are mutually exclusive" hostsreq.err
'
test_expect_success 'a virtual queue must not set hosts' '
	test_must_fail flux config load 2>vqhosts.err <<-EOT &&
	[queues.batch]
	hosts = "test[0-3]"
	[queues.sub]
	parent = "batch"
	hosts = "test0"
	EOT
	grep "a virtual queue must not set .hosts." vqhosts.err
'
test_expect_success 'a hosts queue name must be a valid property string' '
	test_must_fail flux config load 2>badname.err <<-EOT &&
	[queues."a&b"]
	hosts = "test0"
	EOT
	grep "queue name is not a valid property string" badname.err
'
test_expect_success 'a hosts queue name must not contain a caret' '
	test_must_fail flux config load 2>caret.err <<-EOT &&
	[queues."a^b"]
	hosts = "test0"
	EOT
	grep "queue name is not a valid property string" caret.err
'
test_expect_success 'a hosts queue name must not exceed the property size' '
	name=$(printf "q%.0s" $(seq 127)) &&
	test_must_fail flux config load 2>longname.err <<-EOT &&
	[queues.$name]
	hosts = "test0"
	EOT
	grep "characters or less" longname.err
'
test_expect_success 'hosts must not be empty' '
	test_must_fail flux config load 2>emptyhosts.err <<-EOT &&
	[queues.batch]
	hosts = ""
	EOT
	grep "hosts. must not be empty" emptyhosts.err
'
test_expect_success 'a queue name is unrestricted without hosts' '
	flux config load <<-EOT
	[queues."a^b"]
	EOT
'
test_expect_success 'a caret is still valid in requires' '
	flux config load <<-EOT
	[queues.batch]
	requires = [ "^foo" ]
	EOT
'

# ------------------------------------------------------------------
# Resource module: derive +NAME property from hosts (at module load).
# Seed resource.R with distinct fake hostnames so 'hosts' resolves.
# ------------------------------------------------------------------

reload_resource () {
	flux config load <"$1" &&
	flux module remove sched-simple &&
	flux module reload resource &&
	flux module load sched-simple
}

test_expect_success 'seed resource.R with named hosts' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 \
	   | flux kvs put -r resource.R=-
'
test_expect_success 'configure hosts queues and reload resource' '
	cat >hosts.toml <<-EOT &&
	[queues.batch]
	hosts = "test[0-1]"
	[queues.debug]
	hosts = "test[2-3]"
	[queues.allq]
	hosts = "all"
	EOT
	reload_resource hosts.toml
'
# The membership is derived into the R the module serves, not the one it
# stores, so check it through the resource RPCs rather than the KVS.
queue_nodes () {
	flux resource list -s all -no "{nodelist}" -q "$1"
}

test_expect_success 'the derived membership selects each queue hosts' '
	test_debug "flux resource list -s all -o \"{queue} {nodelist}\"" &&
	test "$(queue_nodes batch)" = "test[0-1]" &&
	test "$(queue_nodes debug)" = "test[2-3]" &&
	test "$(queue_nodes allq)" = "test[0-3]"
'

# A queue's membership is derived afresh on every module load, so a
# changed 'hosts', a dropped 'hosts', or a queue deleted from the config
# leaves nothing behind. R is deliberately NOT re-seeded below.

test_expect_success 'changed hosts replaces the previous membership' '
	cat >hosts2.toml <<-EOT &&
	[queues.batch]
	hosts = "test[2-3]"
	[queues.debug]
	hosts = "test[0-1]"
	[queues.allq]
	hosts = "test0"
	EOT
	reload_resource hosts2.toml &&
	test_debug "flux resource list -s all -o \"{queue} {nodelist}\"" &&
	test "$(queue_nodes batch)" = "test[2-3]" &&
	test "$(queue_nodes debug)" = "test[0-1]" &&
	test "$(queue_nodes allq)" = "test0"
'
test_expect_success 'a queue that drops hosts loses its membership' '
	cat >nohosts.toml <<-EOT &&
	[queues.batch]
	[queues.debug]
	[queues.allq]
	EOT
	reload_resource nohosts.toml &&
	test_debug "flux resource list -s all -o \"{propertiesx}\"" &&
	test -z "$(flux resource list -s all -no "{propertiesx}" | tr -d "\n")"
'
test_expect_success 'bare queue (no hosts) materializes no membership property' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 | flux kvs put -r resource.R=- &&
	cat >bare.toml <<-EOT &&
	[queues.batch]
	EOT
	reload_resource bare.toml &&
	test -z "$(flux resource list -s all -no "{propertiesx}" | tr -d "\n")"
'
# Only a hosts queue has a derived membership property. An instance-local
# property applied by other means is not ours to remove, even when a queue
# selects it with 'requires' and so shares its name.
test_expect_success 'an instance-local property already on R is kept' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 \
	   | flux R set-property +batch:0-1 \
	   | flux kvs put -r resource.R=- &&
	cat >req.toml <<-EOT &&
	[queues.batch]
	requires = [ "+batch" ]
	EOT
	reload_resource req.toml &&
	test_debug "flux resource list -s all -o \"{queue} {nodelist}\"" &&
	test "$(queue_nodes batch)" = "test[0-1]"
'
# An instance-local property this module did not derive is stored like any
# other, so it survives however many times the module reloads from the KVS.
# Only the derived membership is kept out of the stored R.
test_expect_success 'an instance-local property survives repeated reloads' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 \
	   | flux R set-property +foo:0-1 \
	   | flux kvs put -r resource.R=- &&
	cat >foo.toml <<-EOT &&
	[queues.batch]
	hosts = "test[2-3]"
	EOT
	reload_resource foo.toml &&
	reload_resource foo.toml &&
	reload_resource foo.toml &&
	test_debug "flux resource R | jq -S .execution.properties" &&
	flux resource R >fooR.json &&
	jq -e ".execution.properties[\"+foo\"] == \"0-1\"" <fooR.json &&
	jq -e ".execution.properties[\"+batch\"] == \"2-3\"" <fooR.json &&
	flux kvs get resource.R >fooKVS.json &&
	jq -e ".execution.properties[\"+foo\"] == \"0-1\"" <fooKVS.json &&
	jq -e ".execution.properties | has(\"+batch\") | not" <fooKVS.json
'
test_expect_success 'unknown host is a fatal resource module error' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 | flux kvs put -r resource.R=- &&
	cat >badconf.toml <<-EOT &&
	[queues.batch]
	hosts = "nosuchhost"
	EOT
	flux config load <badconf.toml &&
	flux module remove sched-simple &&
	test_must_fail flux module reload resource
'
test_expect_success 'restore a working resource module' '
	flux config load </dev/null &&
	flux module load resource &&
	flux module load sched-simple
'
# The reload request responds with the membership error, rather than a
# bare "Invalid argument" from the errno alone.
test_expect_success 'resource.reload reports the membership error' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 >reload.json &&
	cat >badreload.toml <<-EOT &&
	[queues.batch]
	hosts = "nosuchhost"
	EOT
	flux config load <badreload.toml &&
	flux module remove sched-simple &&
	test_must_fail flux resource reload reload.json 2>badreload.err &&
	test_debug "cat badreload.err" &&
	grep "invalid hosts: nosuchhost" badreload.err
'
test_expect_success 'restore a working resource module again' '
	flux config load </dev/null &&
	flux module reload resource &&
	flux module load sched-simple
'

# ------------------------------------------------------------------
# job-manager: effective membership in the queue-list conf object
# ------------------------------------------------------------------

test_expect_success 'queue-list reports +NAME as a hosts queue requires' '
	flux config load <<-EOT &&
	[queues.batch]
	hosts = "test[0-1]"
	[queues.debug]
	requires = [ "dbg" ]
	[queues.expedite]
	parent = "batch"
	EOT
	flux python -c "
import flux
from flux.queue import queue_config_fetch
conf = queue_config_fetch(flux.Flux()).get()
assert conf.requires(\"batch\") == [\"+batch\"], conf.requires(\"batch\")
assert conf.requires(\"debug\") == [\"dbg\"], conf.requires(\"debug\")
assert conf.requires(\"expedite\") == [\"+batch\"], conf.requires(\"expedite\")
"
'

# ------------------------------------------------------------------
# Frobnicator: inject the +NAME constraint for jobs to a hosts queue
# ------------------------------------------------------------------

test_expect_success 'frobnicator injects +NAME constraint for a hosts queue' '
	cat >conf.toml <<-EOT &&
	[queues.batch]
	hosts = "test[0-1]"
	EOT
	flux config load <conf.toml &&
	flux run --env=-* --dry-run --queue=batch hostname \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	   >frob.out &&
	test_debug "jq -S .data.attributes.system.constraints <frob.out" &&
	jq -e ".data.attributes.system.constraints.properties == [ \"+batch\" ]" \
	   <frob.out
'
test_expect_success 'a vqueue job inherits the parent membership property' '
	flux config load <<-EOT &&
	[queues.batch]
	hosts = "test[0-1]"
	[queues.expedite]
	parent = "batch"
	EOT
	flux run --env=-* --dry-run --queue=expedite hostname \
	   | flux job-frobnicator --jobspec-only --plugins=constraints \
	   >frobvq.out &&
	jq -e ".data.attributes.system.constraints.properties == [ \"+batch\" ]" \
	   <frobvq.out
'

# ------------------------------------------------------------------
# End to end: a job lands on its queue's hosts
# ------------------------------------------------------------------

test_expect_success 'configure two hosts queues and reload' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 | flux kvs put -r resource.R=- &&
	cat >e2e.toml <<-EOT &&
	[queues.batch]
	hosts = "test[0-1]"
	[queues.debug]
	hosts = "test[2-3]"
	EOT
	reload_resource e2e.toml &&
	flux queue start --all
'
test_expect_success 'a job runs on the hosts of its queue' '
	id=$(flux submit -N2 --queue=debug true) &&
	flux job wait-event -t30 $id clean &&
	flux job info $id R >e2eR.json &&
	test_debug "jq -S .execution.nodelist <e2eR.json" &&
	jq -e ".execution.nodelist == [ \"test[2-3]\" ]" <e2eR.json
'
test_expect_success 'the membership property is omitted from the job R' '
	test_must_fail jq -e ".execution.properties" <e2eR.json
'

# ------------------------------------------------------------------
# flux resource: the PROPERTIES column hides membership properties only
# ------------------------------------------------------------------

test_expect_success 'flux resource hides +NAME from the PROPERTIES column' '
	flux resource list -s all -no "{propertiesx}" >propx.out &&
	test_debug "cat propx.out" &&
	test_must_fail grep -q "+" propx.out
'
# Only the properties used to express queue membership are hidden: the
# queue names, and the "+NAME" derived from 'hosts'. A property that
# merely happens to be named by 'requires' describes the resource itself
# and is still shown.
test_expect_success 'flux resource shows a property that is not a queue name' '
	flux R encode -r 0-3 -H test[0-3] -c 0-1 \
	   | flux R set-property gpu:0-1 \
	   | flux kvs put -r resource.R=- &&
	cat >req2.toml <<-EOT &&
	[queues.gpuq]
	requires = [ "gpu" ]
	EOT
	reload_resource req2.toml &&
	flux resource list -s all -no "{propertiesx}" >propx2.out &&
	test_debug "cat propx2.out" &&
	grep -q "gpu" propx2.out
'
test_expect_success 'flux resource hides a property that is a queue name' '
	cat >req3.toml <<-EOT &&
	[queues.gpu]
	requires = [ "gpu" ]
	EOT
	reload_resource req3.toml &&
	flux resource list -s all -no "{propertiesx}" >propx3.out &&
	test_debug "cat propx3.out" &&
	test_must_fail grep -q "gpu" propx3.out
'

test_done
