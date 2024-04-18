#!/bin/sh

test_description='Test broker log ring buffer'

. `dirname $0`/sharness.sh

RPC=${FLUX_BUILD_DIR}/t/request/rpc
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

test_under_flux 4 minimal

test_expect_success 'module stats --parse count log counts log messages' '
	OLD_VAL=$(flux module stats --parse count log) &&
	flux logger hello &&
	NEW_VAL=$(flux module stats --parse count log) &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'module stats --parse ring-used log counts log messages' '
	OLD_VAL=$(flux module stats --parse ring-used log) &&
	flux logger hello &&
	NEW_VAL=$(flux module stats --parse ring-used log) &&
	test "${OLD_VAL}" -lt "${NEW_VAL}"
'
test_expect_success 'flux dmesg -C clears ring buffer' '
	flux logger hello_dmesg &&
	flux dmesg | grep -q hello_dmesg &&
	flux dmesg -C &&
	! flux dmesg | grep -q hello_dmesg
'
test_expect_success 'flux setattr log-ring-size trims ring buffer' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux logger hello1 &&
	flux logger hello2 &&
	flux logger hello3 &&
	flux logger hello4 &&
	flux logger hello5 &&
	flux logger hello6 &&
	flux setattr log-ring-size 4 &&
	test `flux dmesg | wc -l` -eq 4 &&
	! flux dmesg | grep -q hello_dmesg1 &&
	! flux dmesg | grep -q hello_dmesg2 &&
	flux setattr log-ring-size $OLD_RINGSIZE
'
test_expect_success 'flux dmesg prints, no clear' '
	flux logger hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc &&
	flux dmesg | grep -q hello_dmesg_pnc
'
test_expect_success 'flux dmesg -c prints and clears' '
	flux logger hello_dmesg_pc &&
	flux dmesg -c | grep -q hello_dmesg_pc &&
	! flux dmesg | grep -q hello_dmesg_pc
'
test_expect_success NO_CHAIN_LINT 'flux dmesg -f works' '
	flux logger hello_last &&
	flux dmesg -f > dmesg.out &
	pid=$! &&
	$waitfile -t 20 -p hello_last dmesg.out &&
	flux logger hello_follow &&
	$waitfile -t 20 -p hello_follow dmesg.out &&
	kill $pid
'
test_expect_success NO_CHAIN_LINT 'flux dmesg -f --new works' '
	flux logger hello_old &&
	flux dmesg -f --new > dmesg2.out &
	pid=$! &&
	for i in $(seq 1 10); do flux logger hello_new; done &&
	$waitfile -t 20 -p hello_new dmesg2.out &&
	test_must_fail grep hello_old dmesg2.out &&
	kill $pid
'
test_expect_success 'ring buffer wraps over old entries' '
	OLD_RINGSIZE=`flux getattr log-ring-size` &&
	flux setattr log-ring-size 2 &&
	flux logger hello_wrap1 &&
	flux logger hello_wrap2 &&
	flux logger hello_wrap3 &&
	! flux dmesg | grep -q hello_wrap1 &&
	flux setattr log-ring-size $OLD_RINGSIZE
'

test_expect_success 'multi-line log messages are split' '
	seq 1 8 | flux logger --appname=linesplit1 &&
	test $(flux dmesg | grep linesplit1 | wc -l) -eq 8
'

test_expect_success 'trailing cr/lf are stripped' '
	/bin/echo -n -e "xxx\x0A\x0D\x0A" | flux logger --appname striptest &&
	flux dmesg|grep striptest | sed -e "s/.*: //" >striptest.out &&
	echo "xxx" >striptest.exp &&
	test_cmp striptest.exp striptest.out
'

test_expect_success 'embedded blank log messages are ignored' '
	cat >blank.in <<-EOT &&
	foo
	bar
	baz

	nerp
	EOT
	flux logger --appname=embtest <blank.in &&
	test $(flux dmesg | grep embtest | wc -l) -eq 4
'

# Try to make flux dmesg get an EPROTO error
test_expect_success 'logged non-ascii characters handled ok' '
	/bin/echo -n -e "\xFF\xFE\x82\x00" | flux logger &&
	flux dmesg
'
test_expect_success 'logged non-ascii printable characters are unmodified' '
	flux logger ƒ Φ Ψ Ω Ö &&
	flux dmesg | tail -1 > dmesg.utf-8 &&
	test_debug "cat dmesg.utf-8" &&
	grep "ƒ Φ Ψ Ω Ö" dmesg.utf-8
'
test_expect_success 'dmesg request with empty payload fails with EPROTO(71)' '
	${RPC} log.dmesg 71 </dev/null
'
test_expect_success 'dmesg -H, --human works' '
	#
	#  Note: --human option should format first timestamp of dmesg output
	#   as [MmmDD HH:MM] and second line should be an offset thereof
	#   e.g. [  +0.NNNNNN]. The following regexes attempt to verify
	#   that --human produced this pattern.
	#
	flux dmesg --human | sed -n 1p \
	    | grep "^\[[A-Z][a-z][a-z][0-3][0-9] [0-9][0-9]:[0-9][0-9]\]" &&
	flux dmesg --human | sed -n 2p \
	    | grep "^\[ *+[0-9]*\.[0-9]*\]"
'
test_expect_success 'dmesg -H, --human --delta works' '
	flux dmesg --human --delta | sed -n 1p \
	    | grep "^\[[A-Z][a-z][a-z][0-3][0-9] [0-9][0-9]:[0-9][0-9]\]" &&
	flux dmesg --human --delta | sed -n 2p \
	    | grep "^\[ *+[0-9]*\.[0-9]*\]"
'
test_expect_success 'dmesg --delta without --human fails' '
	test_must_fail flux dmesg --delta
'

for opt in "-L" "-Lalways" "--color" "--color=always"; do
	test_expect_success "dmesg -H, --human $opt works" '
		flux dmesg --human $opt | sed -n 1p | grep "^" &&
		flux dmesg --human $opt | sed -n 2p | grep "^"
	'
	test_expect_success "dmesg colorizes lines by severity" '
		for s in emerg alert crit err warning notice debug; do
		    flux logger --severity=$s severity=$s &&
		    flux dmesg --human $opt | grep "[^ ]*severity=$s"
		done
	'
done

test_expect_success 'dmesg with invalid --color option fails' '
	test_must_fail flux dmesg --color=foo
'

test_done
