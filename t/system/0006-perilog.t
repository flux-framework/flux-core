#
#  Check prolog run via IMP
#
IMP=$(flux config get exec.imp)
test -n "$IMP" && test_set_prereq HAVE_IMP

fluxcmddir=$(pkg-config --variable=fluxcmddir flux-core)
libexecdir=$(pkg-config --variable=fluxlibexecdir flux-core)
confdir="/etc/flux/imp/conf.d"

test_expect_success HAVE_IMP 'configure a system prolog' '
	cleanup "sudo rm -f $confdir/imp-prolog.toml" &&
	cat <<-EOF >imp-prolog.toml &&
	[run.prolog]
	allowed-users = ["flux"]
	allowed-environment = ["FLUX_*"]
	path = "$fluxcmddir/flux-run-prolog"
	EOF
	sudo cp imp-prolog.toml $confdir &&
	sudo chmod 644 $confdir/imp-prolog.toml &&
	ls -l $confdir/imp-prolog.toml &&
	cat <<-EOF >site-prolog &&
	env
	EOF
	sudo mkdir -p /etc/flux/system/prolog.d &&
	cleanup "sudo rm -rf /etc/flux/system/prolog.d" &&
	sudo cp site-prolog /etc/flux/system/prolog.d &&
	sudo chmod 755 /etc/flux/system/prolog.d/site-prolog &&
	cat <<-EOF >pkg-prolog &&
	sleep 0
	EOF
	sudo cp pkg-prolog $libexecdir/prolog.d &&
	cleanup "sudo rm -f $libexecdir/prolog.d/pkg-prolog" &&
	sudo chmod 755 $libexecdir/prolog.d/pkg-prolog
'
test_expect_success HAVE_IMP 'configure the perilog plugin to run prolog' '
	cleanup "sudo flux config reload" &&
	flux config get |
	    jq ".\"job-manager\".prolog.\"per-rank\"=true" |
	    sudo flux config load &&
	flux config get | jq
'
test_expect_success HAVE_IMP 'load jobtap perilog.so plugin' '
	sudo flux jobtap load perilog.so || : &&
	cleanup "sudo flux jobtap remove perilog.so"
'
test_expect_success HAVE_IMP 'run a job' '
	flux run -vvv hostname
'
test_expect_success HAVE_IMP 'get prolog output' '
	id=$(flux job last | flux job id --to=f58plain) &&
	sudo journalctl -u flux-prolog@${id} >prolog.log
'
test_expect_success HAVE_IMP 'prolog ran site prolog' '
	grep site-prolog prolog.log
'
test_expect_success HAVE_IMP 'prolog ran package prolog' '
	grep pkg-prolog prolog.log
'
test_expect_success HAVE_IMP 'failed pkg prolog script fails prolog' '
	test_when_finished "sudo flux resource undrain 0 || :" &&
	cat <<-EOF >00failed-prolog &&
	exit 1
	EOF
	sudo cp 00failed-prolog $libexecdir/prolog.d &&
	sudo chmod 755 $libexecdir/prolog.d/00failed-prolog &&
	test_must_fail flux run hostname &&
	id=$(flux job last | flux job id --to=f58plain) &&
	sudo journalctl -u flux-prolog@${id} >prolog-failed.log
'
test_expect_success HAVE_IMP 'failed prolog ran other prolog scripts' '
	grep site-prolog prolog-failed.log
'
test_expect_success HAVE_IMP 'prolog ran package prolog' '
	grep pkg-prolog prolog-failed.log
'
# Note: set exit-on-first-error for prolog,epilog,housekeeping to test that
# the value is accepted, even though it is only used for the prolog here.
test_expect_success HAVE_IMP 'set job-manager.*.exit-on-first-error=true' '
	flux config get |
	    jq ".\"job-manager\".prolog.\"exit-on-first-error\"=true" |
	    jq ".\"job-manager\".epilog.\"exit-on-first-error\"=true" |
	    jq ".\"job-manager\".housekeeping.\"exit-on-first-error\"=true" |
	    sudo flux config load
'
test_expect_success HAVE_IMP 'failed prolog skips other prolog scripts' '
	test_when_finished "sudo flux resource undrain 0 || :" &&
	test_must_fail flux run -vvv hostname &&
	id=$(flux job last | flux job id --to=f58plain) &&
	sudo journalctl -u flux-prolog@${id} >prolog-failed-exit-on-error.log &&
	test_must_fail grep site-prolog prolog-failed-exit-on-error.log &&
	test_must_fail grep pkg-prolog prolog-failed-exit-on-error.log
'
test_expect_success HAVE_IMP 'remove failing prolog script (for future tests)' '
	sudo rm -f $libexecdir/prolog.d/00failed-prolog
'
