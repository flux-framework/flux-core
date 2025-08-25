#
#  Check prolog,epilog,housekeeping run via IMP->systemd
#
IMP=$(flux config get exec.imp)
test -n "$IMP" && test_set_prereq HAVE_IMP

fluxcmddir=$(pkg-config --variable=fluxcmddir flux-core)
libexecdir=$(pkg-config --variable=fluxlibexecdir flux-core)
confdir="/etc/flux/imp/conf.d"

# Note: many tests below need to wait for script output in the systemd
# journal. However, some flux-${name}@<id> units may run asynchronously
# (e.g. for housekeeping) and also the journal output appears to be
# buffered such that data is not available immediately. This convenience
# function:
#
# 1. waits for the flux-{name}@{id} unit to become inactive
# 2. flushes the journal which appears to ensure data is available from
#    subsequent journalctl request
#
# Usage: journal_output NAME ID
journal_output() {
	unit=flux-${1}@${2}
	while systemctl is-active --quiet $unit; do
	    sleep 0.2
	done
	sudo journalctl --flush &&
	sudo journalctl -u $unit
}

test_expect_success HAVE_IMP 'configure IMP for prolog,epilog,housekeeping' '
	cleanup "sudo rm -f $confdir/imp-perilog.toml" &&
	cat <<-EOF >imp-perilog.toml &&
	[run.prolog]
	allowed-users = ["flux"]
	allowed-environment = ["FLUX_*"]
	path = "$fluxcmddir/flux-run-prolog"
	[run.epilog]
	allowed-users = ["flux"]
	allowed-environment = ["FLUX_*"]
	path = "$fluxcmddir/flux-run-epilog"
	[run.housekeeping]
	allowed-users = ["flux"]
	allowed-environment = ["FLUX_*"]
	path = "$fluxcmddir/flux-run-housekeeping"
	EOF
	sudo cp imp-perilog.toml $confdir &&
	sudo chmod 644 $confdir/imp-perilog.toml
'
test_expect_success HAVE_IMP 'load perilog plugin' '
	cleanup "sudo flux jobtap remove perilog.so" &&
	cleanup "sudo flux config reload" &&
	sudo flux jobtap load perilog.so || :
'

lastid() { flux job last | flux job id --to=f58plain; }

for name in prolog epilog housekeeping; do

# clean up stale files after test failure
sudo rm -f /etc/flux/system/${name}.d/* $libexecdir/${name}.d/*

test_expect_success HAVE_IMP "reload broker config to clear any stale config" '
	sudo flux config reload
'
test_expect_success HAVE_IMP "configure job-manager.${name}" '
	if test "$name" = "housekeeping"; then
	    flux config get |
	        jq ".\"job-manager\".${name}={}" |
	    	sudo flux config load
	else
	    flux config get |
	        jq ".\"job-manager\".${name}.\"per-rank\"=true" |
	        sudo flux config load
	fi &&
	flux config get | jq
'
test_expect_success HAVE_IMP "$name works with zero configured scripts" '
	flux run -vvv hostname &&
	journal_output $name $(lastid)
'
test_expect_success HAVE_IMP "configure a site-provided $name" '
	cat <<-EOF >site-provided
	env
	EOF
	sudo mkdir -p /etc/flux/system/${name}.d &&
	cleanup "sudo rm -rf /etc/flux/system/${name}.d" &&
	sudo cp site-provided /etc/flux/system/${name}.d &&
	sudo chmod 755 /etc/flux/system/${name}.d/site-provided &&
	ls -l /etc/flux/system/${name}.d/
'
test_expect_success HAVE_IMP "job runs with only site-provided $name" '
	flux run -vvv hostname
'
test_expect_success HAVE_IMP "expected output is present in journal" '
	journal_output ${name} $(lastid) > site-${name}.out &&
	grep FLUX_JOB_ID site-${name}.out &&
	grep FLUX_JOB_USERID site-${name}.out &&
	grep ${libexecdir}/${name}.d site-${name}.out &&
	grep /etc/flux/system site-${name}.out
'
test_expect_success HAVE_IMP "configure a pkg-provided $name" '
	cat <<-EOF >pkg-provided &&
	sleep 0
	EOF
	sudo cp pkg-provided $libexecdir/${name}.d &&
	cleanup "sudo rm -f $libexecdir/${name}.d/pkg-provided" &&
	sudo chmod 755 $libexecdir/${name}.d/pkg-provided
'
test_expect_success HAVE_IMP "job runs with multiple provided ${name} scripts" '
	flux run -vvv hostname &&
	journal_output ${name} $(lastid) >${name}-both.log
'
test_expect_success HAVE_IMP "${name} ran expected ${name} scripts" '
	grep pkg-provided ${name}-both.log &&
	grep site-provided ${name}-both.log
'
test_expect_success HAVE_IMP "failed pkg ${name} script fails ${name}" '
	test_when_finished "sudo flux resource undrain 0 || :" &&
	cat <<-EOF >00failed &&
	exit 1
	EOF
	sudo cp 00failed $libexecdir/${name}.d &&
	sudo chmod 755 $libexecdir/${name}.d/00failed &&
	if test "$name" = "prolog"; then
	    test_must_fail flux run hostname
	else
	    flux run -vvv hostname
	fi &&
        id=$(lastid) &&
	if test $name = housekeeping; then
	    while systemctl is-active flux-${name}@${id}; do
	        sleep 0.1
            done
	fi &&
	journal_output ${name} ${id} >${name}-failed.log &&
	test_debug "cat ${name}-failed.log"
'
test_expect_success HAVE_IMP "failed ${name} ran other ${name} scripts" '
	grep site-provided ${name}-failed.log &&
	grep pkg-provided ${name}-failed.log
'
test_expect_success HAVE_IMP "set job-manager.${name}.exit-on-first-error=true" '
	flux config get |
	    jq ".\"job-manager\".${name}.\"exit-on-first-error\"=true" |
	    sudo flux config load
'
test_expect_success HAVE_IMP "failed ${name} skips other ${name} scripts" '
	test_when_finished "sudo flux resource undrain 0 || :" &&
	test_when_finished "sudo rm -f $libexecdir/${name}.d/00-failed" &&
	if test "$name" = "prolog"; then
	    test_must_fail flux run hostname
	else
	    flux run -vvv hostname
	fi &&
        id=$(lastid) &&
	if test "$name" = "housekeeping"; then
	    while systemctl is-active flux-${name}@${id}; do
	        sleep 0.1
            done
	fi &&
	journal_output $name $(lastid) >${name}-failed-exit-on-error.log &&
	test_debug "cat ${name}-failed-exit-on-error.log" &&
	test_must_fail grep site-${name} ${name}-failed-exit-on-error.log &&
	test_must_fail grep pkg-${name} ${name}-failed-exit-on-error.log
'
done
