#!/bin/sh
#

test_description='Test flux-module-python-exec'

. `dirname $0`/sharness.sh
test_under_flux 1 minimal

testmod=$(realpath ${SHARNESS_TEST_SRCDIR}/module/testmod.py)

# check_hello NAME - send a hello RPC to NAME.hello and verify the response
check_hello() {
	flux python -c "
import flux, sys
h = flux.Flux()
result = h.rpc('$1.hello').get()
if result.get('name') != '$1':
    print('unexpected name: ' + str(result), file=sys.stderr)
    sys.exit(1)
"
}

# trigger_die NAME - send a die RPC to NAME.die (no response expected)
trigger_die() {
	flux python -c "
import flux
h = flux.Flux()
try:
    h.rpc('$1.die').get()
except Exception:
    pass
"
}

##
# Error cases (direct invocation without FLUX_MODULE_URI)
##

test_expect_success 'flux-module-python-exec fails without FLUX_MODULE_URI' '
	test_must_fail env -u FLUX_MODULE_URI \
	    flux module-python-exec $testmod 2>nouri.err &&
	grep "FLUX_MODULE_URI" nouri.err
'
test_expect_success 'flux-module-python-exec fails with no path argument' '
	test_must_fail env -u FLUX_MODULE_URI \
	    flux module-python-exec 2>noarg.err &&
	grep "Usage" noarg.err
'
test_expect_success 'flux-module-python-exec fails with too many arguments' '
	test_must_fail env -u FLUX_MODULE_URI \
	    flux module-python-exec $testmod extra 2>extraarg.err &&
	grep "Usage" extraarg.err
'

##
# Broker mode
##

test_expect_success 'flux module load testmod.py works' '
	flux module load $testmod
'
test_expect_success 'flux ping testmod works' '
	flux ping -c 1 testmod
'
test_expect_success 'flux module stats testmod works' '
	flux module stats testmod
'
test_expect_success 'flux module list shows testmod' '
	flux module list | grep testmod
'
test_expect_success 'flux module remove testmod works' '
	flux module remove testmod
'
test_expect_success 'flux module load testmod.py with args works' '
	flux module load $testmod somearg &&
	flux module remove testmod
'
test_expect_success 'flux module load testmod.py --init-failure fails' '
	test_must_fail flux module load $testmod --init-failure
'
test_expect_success 'flux module load testmod.py under alternate name works' '
	flux module load --name altmod $testmod &&
	flux ping -c 1 altmod &&
	check_hello altmod &&
	flux module remove altmod
'
test_expect_success 'handler exception causes module exit with error' '
	flux setattr broker.module-nopanic 1 &&
	flux module load $testmod &&
	flux dmesg -C &&
	trigger_die testmod &&
	sh -c "while flux module stats testmod 2>/dev/null; do true; done" &&
	flux dmesg | grep "module runtime failure"
'
test_expect_success 'early module exit with error is reported by broker' '
	flux setattr broker.module-nopanic 1 &&
	flux module load $testmod &&
	flux dmesg -C &&
	flux event pub testmod.panic &&
	sh -c "while flux module stats testmod 2>/dev/null; do true; done" &&
	flux dmesg | grep "module runtime failure"
'
test_expect_success 'modprobe can load and remove testmod.py declared in TOML' '
	mkdir -p modprobe.d &&
	test_when_finished "rm -rf modprobe.d" &&
	cat >modprobe.d/testmod.toml <<-EOF &&
	[[modules]]
	name = "testmod"
	module = "$testmod"
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe load testmod &&
	flux ping -c 1 testmod &&
	check_hello testmod &&
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe remove testmod
'

test_done
