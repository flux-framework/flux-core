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

# check_debug NAME FLAG [clear] - return true if debug bit FLAG is set in NAME
# Passes clear=True if a third argument is given.
check_debug() {
	local name="$1" flag="$2"
	local clear="False"; [ $# -ge 3 ] && clear="True"
	flux python -c "
import flux, sys
h = flux.Flux()
result = h.rpc('$name.debug_check', {'flag': $flag, 'clear': $clear}).get()
sys.exit(0 if result.get('set') else 1)
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
# flux.proctitle unit tests
##

test_expect_success 'set_proctitle sets /proc/self/comm on Linux' '
	flux python -c "
import platform, sys
from flux.proctitle import set_proctitle
set_proctitle(\"flux-test-proc\")
if platform.system() == \"Linux\":
    with open(\"/proc/self/comm\") as f:
        comm = f.read().strip()
    if comm != \"flux-test-proc\":
        print(f\"expected flux-test-proc, got {comm!r}\", file=sys.stderr)
        sys.exit(1)
"
'
test_expect_success 'set_proctitle truncates long names to 15 chars' '
	flux python -c "
import platform, sys
from flux.proctitle import set_proctitle
set_proctitle(\"flux-this-name-is-too-long\")
if platform.system() == \"Linux\":
    with open(\"/proc/self/comm\") as f:
        comm = f.read().strip()
    if len(comm) > 15:
        print(f\"expected truncation to 15 chars, got {comm!r}\", file=sys.stderr)
        sys.exit(1)
"
'

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

##
# BrokerModule.debug_test()
##

test_expect_success 'debug_test: load testmod' '
	flux module load $testmod
'
test_expect_success 'debug_test: bit is clear before setbit' '
	test_must_fail check_debug testmod 1
'
test_expect_success 'debug_test: bit is set after flux module debug --setbit' '
	flux module debug --setbit 1 testmod &&
	check_debug testmod 1
'
test_expect_success 'debug_test: clear=True reads and clears the bit atomically' '
	check_debug testmod 1 true &&
	test_must_fail check_debug testmod 1
'
test_expect_success 'debug_test: remove testmod' '
	flux module remove testmod
'

test_expect_success 'OSError errno is propagated on module init failure' '
	test_must_fail flux module load $testmod --oserror-failure 2>oserr.err &&
	grep -i "File exists" oserr.err
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
