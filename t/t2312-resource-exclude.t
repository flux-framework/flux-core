#!/bin/sh

test_description='Test resource exclusion'

. `dirname $0`/sharness.sh

cat >exclude.toml <<-EOT
[resource]
exclude = "0"
EOT

SIZE=4
test_under_flux $SIZE full --config-path=$(pwd)/exclude.toml

# Usage: waitup N
#   where N is a count of online ranks
waitup () {
    run_timeout 5 flux python -c "import flux; print(flux.Flux().rpc(\"resource.monitor-waitup\",{\"up\":$1}).get())"
}
waitdown () {
    waitup $(($SIZE-$1))
}

has_resource_event () {
    flux kvs eventlog get resource.eventlog | awk '{ print $2 }' | grep $1
}

test_expect_success 'wait for monitor to declare all nodes are up' '
    waitdown 0
'

test_expect_success 'flux resource list shows no nodes down' '
	test $(flux resource list -n -s down -o {nnodes}) -eq 0
'

test_expect_success 'flux resource status shows one node excluded' '
	test $(flux resource status -s exclude -no {nnodes}) -eq 1 &&
	test $(flux resource status -s exclude -no {ranks}) = "0"
'

test_expect_success 'flux resource status shows all nodes online' '
	test $(flux resource status -s online -no {nnodes}) -eq ${SIZE}
'

test_expect_success 'flux resource status shows correct nodes avail' '
	NAVAIL=$((SIZE-1)) &&
	test $(flux resource status -s avail -no {nnodes}) -eq $NAVAIL
'

test_expect_success 'but monitor still says all nodes are up' '
	waitdown 0
'

test_expect_success 'config with bad exclude idset fails' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "xxzz"
	EOT
	test_must_fail flux start --config-path=resource.toml true
'

test_expect_success 'config with out of range exclude idset fails' '
	cat >resource.toml <<-EOT &&
	[resource]
	exclude = "1"
	EOT
	test_must_fail flux start --config-path=resource.toml true
'

# See flux-framework/flux-core#5337
test_expect_success 'test instance can exclude ranks' '
	cat >exclude.toml <<-EOT &&
	[resource]
	exclude = "1"
	EOT
	test $(flux start -s2 --config-path=exclude.toml \
	    flux resource status -s exclude -no {nnodes}) -eq 1
'
test_expect_success 'test instance fails to exclude hostnames' '
	cat >exclude2.toml <<-EOT &&
	[resource]
	exclude = "$(hostname -s)"
	EOT
	test_must_fail flux start -s2 --config-path=exclude2.toml \
	    true 2>exclude2.err &&
	grep "R is unavailable" exclude2.err
'
test_expect_success 'instance with configured R can exclude hostnames' '
	cat >exclude3.toml <<-EOT &&
	[resource]
	exclude = "$(hostname -s)"
	noverify = true
	[[resource.config]]
	hosts = "$(hostname -s)"
	cores = "0"
	EOT
	test $(flux start -s1 --config-path=exclude3.toml \
	    flux resource status -s exclude -no {nnodes}) -eq 1
'
test_expect_success 'incorrect excluded hostnames raises correct error' '
	cat >exclude4.toml <<-EOT &&
	[resource]
	exclude = "badhost"
	noverify = true
	[[resource.config]]
	hosts = "$(hostname -s)"
	cores = "0"
	EOT
	test_must_fail flux start --config-path=exclude4.toml true \
		2>exclude4.err &&
	test_debug "cat exclude4.err" &&
	grep "invalid hosts: badhost" exclude4.err
'

test_done
