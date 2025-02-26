#!/bin/sh
# ci=system

test_description='Test flux systemd execution'

. $(dirname $0)/sharness.sh

if ! flux version | grep systemd; then
	skip_all="flux was not built with systemd"
	test_done
fi
if ! systemctl --user show --property Version; then
	skip_all="user systemd is not running"
	test_done
fi
if ! busctl --user status >/dev/null; then
	skip_all="user dbus is not running"
	test_done
fi

test_under_flux 2 minimal

flux setattr log-stderr-level 1

sdexec="flux exec --service sdexec"
lptest="flux lptest"
rkill="flux python ${SHARNESS_TEST_SRCDIR}/scripts/rexec.py kill -s sdexec"
rps="flux python ${SHARNESS_TEST_SRCDIR}/scripts/rexec.py ps -s sdexec"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

# systemd 239 requires commands to be fully qualified, while 249 does not
true=$(which true)
false=$(which false)
sh=$(which sh)
cat=$(which cat)
pwd=$(which pwd)
printenv=$(which printenv)
systemctl=$(which systemctl)

test_expect_success 'enable debug logging' '
	cat >systemd.toml <<-EOF &&
	[systemd]
	sdbus-debug = true
	sdexec-debug = true
	EOF
	flux config load <systemd.toml
'
test_expect_success 'load sdbus,sdexec modules' '
	flux exec flux module load sdbus &&
	flux exec flux module load sdexec
'
test_expect_success 'clear broker logs' '
	flux dmesg -C
'
test_expect_success 'sdexec true succeeds' '
	$sdexec -r 0 $true
'
test_expect_success 'sdexec -n cat succeeds' '
	run_timeout 30 $sdexec -n -r 0 $cat
'
test_expect_success 'sdexec false fails with exit code 1' '
	test_expect_code 1 $sdexec -r 0 $false
'
test_expect_success 'sdexec /notacmd fails with exit code 127' '
	test_expect_code 127 $sdexec -r 0 /notacmd
'
test_expect_success 'capture broker logs' '
	flux dmesg >dmesg.log
'
test_expect_success 'sdexec stdout works' '
	cat >hello.exp <<-EOT &&
	Hello world!
	EOT
	$sdexec -r 0 $sh -c "echo Hello world!" >hello.out &&
	test_cmp hello.exp hello.out
'
test_expect_success 'sdexec stderr works' '
	$sdexec -r 0 $sh -c "echo Hello world! >&2" 2>hello.err &&
	test_cmp hello.exp hello.err
'
test_expect_success 'sdexec stdout+stderr works' '
	$sdexec -r 0 $sh -c "echo Hello world!; echo Hello world! >&2" \
		>hello2.out 2>hello2.err &&
	test_cmp hello.exp hello2.out &&
	test_cmp hello.exp hello2.err
'
test_expect_success 'sdexec stdin works' '
	echo "Hello world!" | $sdexec -r 0 $cat >hello.in &&
	test_cmp hello.exp hello.in
'
test_expect_success 'set up for lptest stdio tests' '
	$lptest 79 10000 >lptest.exp
'
test_expect_success 'sdexec works with 10K lines of stdout' '
	$sdexec -r 0 $cat lptest.exp >lptest.out &&
	test_cmp lptest.exp lptest.out
'
test_expect_success 'sdexec works with 10K lines of stdin' '
	$sdexec -r 0 $sh -c "cat >lptest.in" <lptest.exp &&
	test_cmp lptest.exp lptest.in
'
test_expect_success 'sdexec can set working directory' '
	pwd >dir.exp &&
	$sdexec -r 0 $pwd >dir.out &&
	test_cmp dir.exp dir.out
'
test_expect_success 'sdexec can set environment' '
	echo 42 >env.exp &&
	T2409=42 $sdexec -r 0 $printenv T2409 >env.out &&
	test_cmp env.exp env.out
'
# environment modules sets variables with names like 'BASH_FUNC_ml()'
# https://bugzilla.redhat.com/show_bug.cgi?id=1912046#c5
test_expect_success 'sdexec ignores environment containing parens' '
	env "foo()"=badenv666 $sdexec -r 0 $printenv >badenv.out &&
	test_must_fail grep -q badenv666 badenv.out
'


# each entry: "TESTVAR_xxxxx=" (14) + val (1009) + delim (1) = 1024 bytes
make_exports() {
	local count=$1
	local val=$(for i in $(seq 1009); do printf x; done)
	while test $count -gt 0; do
		printf "export TESTVAR_%.5d=%s\n" $count $val
		count=$(($count-1))
	done
}

# Since the StartTransientUnit request includes the user's environment, the
# request message size is unpredictable.  Let's be sure a large environment
# as might be seen with spack or environment modules is handled OK.
#
# Note the maximum dbus message size is 128 MiB per spec[1].
# Tested on debian 11 aarch64:
# - count=1024 (1MiB) works
# - count=2048 (2MiB) works
# - count=4096 (4MiB) works
# - count=8192 (8MiB) printenv(1) fails with "Argument list too long"
# But sdexec/dbus holds up just fine.
#
# [1] https://dbus.freedesktop.org/doc/dbus-specification.html
test_expect_success 'sdexec accepts supersize environment' '
	count=1024 &&
	make_exports $count >testenv &&
	sed -e "s/^export //" <testenv | sort >testenv.exp
	(. $(pwd)/testenv && $sdexec -r 0 $printenv) >testenv.raw &&
	grep TESTVAR_ testenv.raw | sort >testenv.out &&
	test_cmp testenv.exp testenv.out
'
test_expect_success 'sdexec can set unit name' '
	$sdexec -r 0 --setopt=SDEXEC_NAME=t2409-funfunfun.service \
	    $systemctl --user list-units --type=service >name.out &&
	grep t2409-funfunfun name.out
'
test_expect_success 'sdexec can set unit Description property' '
	cat >desc.exp <<-EOT &&
	Description=fubar
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME=t2409-desc.service \
	    --setopt=SDEXEC_PROP_Description=fubar \
	    $systemctl --user show --property Description \
	        t2409-desc.service >desc.out &&
	test_cmp desc.exp desc.out
'
test_expect_success 'sdexec can set unit SendSIGKILL property to no' '
	cat >sigkill.exp <<-EOT &&
	SendSIGKILL=no
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-sigkill.service" \
	    --setopt=SDEXEC_PROP_SendSIGKILL=no \
	    $systemctl --user show --property SendSIGKILL \
	        t2409-sigkill.service >sigkill.out &&
	test_cmp sigkill.exp sigkill.out
'
test_expect_success 'setting SendSIGKILL to an invalid value fails' '
	test_must_fail $sdexec -r 0 --setopt=SDEXEC_PROP_SendSIGKILL=zzz \
	    $true 2>sigkill_badval.err &&
	grep "error setting property" sigkill_badval.err
'
test_expect_success 'sdexec can set unit KillMode property to process' '
	cat >killmode.exp <<-EOT &&
	KillMode=process
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-killmode.service" \
	    --setopt=SDEXEC_PROP_KillMode=process \
	    $systemctl --user show --property KillMode \
	        t2409-killmode.service >killmode.out &&
	test_cmp killmode.exp killmode.out
'
test_expect_success 'sdexec can set unit TimeoutStopUSec property to infinity' '
	cat >timeoutstop.exp <<-EOT &&
	TimeoutStopUSec=infinity
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-timeoutstop.service" \
	    --setopt=SDEXEC_PROP_TimeoutStopUSec=infinity \
	    $systemctl --user show --property TimeoutStopUSec \
	        t2409-timeoutstop.service >timeoutstop.out &&
	test_cmp timeoutstop.exp timeoutstop.out
'
test_expect_success 'sdexec can set unit TimeoutStopUSec to a numerical value' '
	cat >timeoutstop2.exp <<-EOT &&
	TimeoutStopUSec=42us
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-timeoutstop2.service" \
	    --setopt=SDEXEC_PROP_TimeoutStopUSec=42 \
	    $systemctl --user show --property TimeoutStopUSec \
	        t2409-timeoutstop2.service >timeoutstop2.out &&
	test_cmp timeoutstop2.exp timeoutstop2.out
'
test_expect_success 'setting TimeoutStopUSec to an invalid value fails' '
	test_must_fail $sdexec -r 0 --setopt=SDEXEC_PROP_TimeoutStopUSec=zzz \
	    $true 2>timeoutstop_badval.err &&
	grep "error setting property" timeoutstop_badval.err
'
# Check that we can set resource control attributes on our transient units,
# Check that we can set resource control attributes on our transient units,
# but expect resource control testing to occur elsewhere.
# See also:
# https://www.freedesktop.org/software/systemd/man/systemd.resource-control.html
test_expect_success 'sdexec can set unit MemoryHigh property' '
	cat >memhi.exp <<-EOT &&
	MemoryHigh=1073741824
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-memhi.service" \
	    --setopt=SDEXEC_PROP_MemoryHigh=1G \
	    $systemctl --user show --property MemoryHigh \
	        t2409-memhi.service >memhi.out &&
	test_cmp memhi.exp memhi.out
'
test_expect_success 'sdexec can set unit MemoryMax property' '
	cat >memmax.exp <<-EOT &&
	MemoryMax=infinity
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-memmax.service" \
	    --setopt=SDEXEC_PROP_MemoryMax=infinity \
	    $systemctl --user show --property MemoryMax \
	        t2409-memmax.service >memmax.out &&
	test_cmp memmax.exp memmax.out
'
test_expect_success 'sdexec can set unit MemoryMin property' '
	cat >memmin.exp <<-EOT &&
	MemoryMin=1048576
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-memmin.service" \
	    --setopt=SDEXEC_PROP_MemoryMin=1M \
	    $systemctl --user show --property MemoryMin \
	        t2409-memmin.service >memmin.out &&
	test_cmp memmin.exp memmin.out
'
test_expect_success 'sdexec can set unit AllowedCPUs property' '
	cat >allowedcpus.exp <<-EOT &&
	AllowedCPUs=0 57-99 127
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-allowedcpus.service" \
	    --setopt=SDEXEC_PROP_AllowedCPUs="0,57-99,127" \
	    $systemctl --user show --property AllowedCPUs \
	        t2409-allowedcpus.service >allowedcpus.out &&
	test_cmp allowedcpus.exp allowedcpus.out
'
test_expect_success 'sdexec can set unit AllowedCPUs to an empty bitmask' '
	cat >allowedcpus2.exp <<-EOT &&
	AllowedCPUs=
	EOT
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-allowedcpus2.service" \
	    --setopt=SDEXEC_PROP_AllowedCPUs="" \
	    $systemctl --user show --property AllowedCPUs \
	        t2409-allowedcpus2.service >allowedcpus2.out &&
	test_cmp allowedcpus2.exp allowedcpus2.out
'

memtotal() {
	local kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
	expr $kb \* 1024
}

# There's a rounding error in here somewhere because 50% is off by 2048 bytes,
# but 100% is accurate with respect to /proc/meminfo.  Just use 100% for now.
test_expect_success 'sdexec can set unit MemoryLow property' '
	echo MemoryLow=$(memtotal) >memlow.exp &&
	$sdexec -r 0 \
	    --setopt=SDEXEC_NAME="t2409-memlow.service" \
	    --setopt=SDEXEC_PROP_MemoryLow=100% \
	    $systemctl --user show --property MemoryLow \
	        t2409-memlow.service >memlow.out &&
	test_cmp memlow.exp memlow.out
'
test_expect_success 'sdexec fails on bad property' '
	test_must_fail $sdexec -r 0 --setopt=SDEXEC_PROP_xxx=yyz \
	    $true 2>setunk.err &&
	grep "Cannot set property xxx" setunk.err
'
test_expect_success 'kill fails on unknown pid' '
        test_must_fail $rkill -r 0 15 1234 2>killunk.err &&
	grep "not found" killunk.err
'
test_expect_success 'rank 0 sdexec fails if remote' '
        test_must_fail flux exec -r 1 $sdexec -r 0 $true 2>restrict.err &&
        grep "not allowed on rank 0" restrict.err
'
test_expect_success 'rank 0 kill fails if remote' '
        test_must_fail flux exec -r 1 $rkill -r 0 15 1234 2>killperm.err &&
	grep "not allowed on rank 0" killperm.err
'
test_expect_success 'rank 0 list fails if remote' '
        test_must_fail flux exec -r 1 $rps -r 0 2>psperm.err &&
	grep "not allowed on rank 0" psperm.err
'
# N.B. this "kill" test relies on the fact that flux exec forwards
# signals to the remote process
test_expect_success NO_CHAIN_LINT 'sdexec remote kill works' '
	$sdexec -r 0 $sh -c "echo hello >sleep.out && sleep 60" &
	testpid=$! &&
	$waitfile -q -t 30 sleep.out &&
	kill -15 $testpid &&
	test_expect_code 143 wait $testpid
'
test_expect_success NO_CHAIN_LINT 'sdexec.list works' '
	$sdexec -r 0 $sh -c "echo hello >sleep2.out && sleep 60" &
	testpid=$! &&
	$waitfile -q -t 30 sleep2.out &&
	$rps >list.out &&
	grep sh list.out &&
	kill -15 $testpid &&
	test_expect_code 143 wait $testpid
'
test_expect_success NO_CHAIN_LINT 'sdexec.stats-get works' '
	$sdexec -r 0 --setopt=SDEXEC_NAME=statstest.service \
	    $sh -c "echo hello >sleep3.out && sleep 60" &
	testpid=$! &&
	$waitfile -q -t 30 sleep3.out &&
	flux module stats sdexec >stats.out &&
	grep statstest.service stats.out &&
	kill -15 $testpid &&
	test_expect_code 143 wait $testpid
'
test_expect_success 'sdexec sets FLUX_URI to local broker' '
	echo $FLUX_URI >uri.exp &&
	$sdexec -r 1 $printenv FLUX_URI >uri.out &&
	test_must_fail test_cmp uri.exp uri.out
'
test_expect_success 'sdexec reconfig fails with bad sdexec-debug value' '
	test_must_fail flux config load <<-EOT 2>config.err &&
	[systemd]
	sdexec-debug = 42
	EOT
	grep "Expected true or false" config.err
'
test_expect_success 'remove sdexec,sdbus modules' '
	flux exec flux module remove sdexec &&
	flux exec flux module remove sdbus
'
test_done
