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
	flux logger hello_last
	flux dmesg -f > dmesg.out &
	pid=$! &&
	$waitfile -t 20 -p hello_last dmesg.out &&
	flux logger hello_follow &&
	$waitfile -t 20 -p hello_follow dmesg.out &&
	kill $pid
'
test_expect_success NO_CHAIN_LINT 'flux dmesg -f --new works' '
	flux logger hello_old
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
# Note: TZ=UTC is set by default for sharness
test_expect_success 'dmesg with TZ=UTC uses "Z" as offset' '
	flux dmesg \
	    | grep -E \
	    "^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+Z"
'

test "$(TZ=America/Los_Angeles date +%z)" != "+0000" &&
  test "$(TZ=America/Los_Angeles date +%Z)" != "UTC" &&
  test_set_prereq HAVE_TZ
test_expect_success HAVE_TZ 'dmesg with TZ=America/Los_Angeles uses tz offset' '
	TZ=America/Los_Angeles flux dmesg >dmesgLA.out &&
	test_debug "cat dmesgLA.out" &&
	grep -E "^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+-[0-9]{2}:[0-9]{2}" dmesgLA.out
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

test_expect_success 'dmesg: long lines are not truncated' '
	python3 -c "print(\"x\"*3000)" | flux logger --appname=truncate &&
	result=$(flux dmesg | sed -n "/truncate/s/^.*: //p" | wc -c) &&
	test_debug "echo logged 3001 characters, got $result" &&
	test $result -eq 3001
'

test_expect_success 'setting log-stderr-mode to empty value fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-stderr-mode= true
'
test_expect_success 'setting log-stderr-mode=wrong fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-stderr-mode=wrong \
	    true
'
test_expect_success 'setting log-ring-size to empty value fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-ring-size= true
'
test_expect_success 'setting log-ring-size to negative value fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-ring-size=-1 true
'
test_expect_success 'setting log-ring-size to value with extra text fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-ring-size=4xx true
'
test_expect_success 'setting log-filename to empty value fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-filename= true
'
test_expect_success 'setting log-syslog-enable=1 works' '
	test $(flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-enable=1 \
	    flux getattr log-syslog-enable) -eq 1
'
test_expect_success 'setting log-syslog-enable=0 works' '
	test $(flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-enable=0 \
	    flux getattr log-syslog-enable) -eq 0
'
test_expect_success 'setting log-syslog-enable=foo is true' '
	test $(flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-enable=foo \
	    flux getattr log-syslog-enable) -eq 1
'
test_expect_success 'setting log-syslog-level=7 works' '
	test $(flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-level=7 \
	    flux getattr log-syslog-level) -eq 7
'
test_expect_success 'setting log-syslog-level=0 works' '
	test $(flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-level=0 \
	    flux getattr log-syslog-level) -eq 0
'
test_expect_success 'setting log-syslog-level=8 fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-level=8 \
	    true
'
test_expect_success 'setting log-syslog-level to value with extra text fails' '
	test_must_fail flux start \
	    -o,-Sbroker.rc1_path=,-Sbroker.rc3_path=,-Slog-syslog-level=5xx \
	    true
'
test_expect_success 'log-forward-level can be set to -1' '
	echo "-1" >forward.exp &&
	flux start \
	    -Sbroker.rc1_path= -Sbroker.rc3_path= \
	    -Slog-forward-level=-1 \
	    flux getattr log-forward-level >forward.out &&
	test_cmp forward.exp forward.out
'
test_expect_success 'setting log-forward-level to -1 works' '
	rm -f forward.log &&
	flux start --test-size=2 \
	    -Sbroker.rc1_path= -Sbroker.rc3_path= \
	    -Slog-forward-level=-1 \
	    -Slog-critical-level=-1 \
	    -Slog-filename=forward.log \
	    flux exec -r 1 flux logger --severity=emerg "smurfs" &&
	test_must_fail grep "logger.emerg" forward.log
'
test_expect_success 'flux dmesg --stderr-level=0 works' '
	echo 0 >stderr.exp &&
	flux dmesg --stderr-level=0 &&
	flux getattr log-stderr-level >stderr.out &&
	test_cmp stderr.exp stderr.out
'
test_expect_success 'flux dmesg --stderr-level=-1 works' '
	echo -1 >stderr2.exp &&
	flux dmesg --stderr-level=-1 &&
	flux getattr log-stderr-level >stderr2.out &&
	test_cmp stderr2.exp stderr2.out
'
test_expect_success 'flux dmesg --stderr-level=8 fails' '
	test_must_fail flux dmesg --stderr-level=8
'
test_expect_success 'restore stderr-level to 3 (LOG_ERR)' '
	flux dmesg --stderr-level=3
'
test_done
