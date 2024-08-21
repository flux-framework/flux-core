#!/bin/sh

test_description='Test prolog/epilog jobtap functionality'

. $(dirname $0)/sharness.sh

mkdir config
cat <<EOF >config/perilog.toml
[job-manager.prolog]
command = [
  "flux", "perilog-run", "prolog", "-d", "$(pwd)/prolog.d"
]
[job-manager.epilog]
command = [
  "flux", "perilog-run", "epilog", "-d", "$(pwd)/epilog.d"
]
EOF

test_under_flux 4 full \
    -o,--config-path=$(pwd)/config,-Stbon.topo=kary:4 \
    --test-exit-mode=leader

OFFLINE_PLUGIN=${FLUX_BUILD_DIR}/t/job-manager/plugins/.libs/offline.so
startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

flux setattr log-stderr-level 1

# In case the testsuite is running as a Flux job
unset FLUX_JOB_ID

drained_ranks() { flux resource status -no {ranks} -s drain; }

no_drained_ranks() { test "$(drained_ranks)" = ""; }

undrain_all() {
	ranks="$(drained_ranks)"
	if test -n "$ranks"; then
	    flux resource undrain $ranks
	fi
}

test_expect_success 'perilog: setup rundirs' '
	mkdir prolog.d epilog.d &&
	for f in prolog epilog prolog.2 epilog.2; do
		cat <<-EOF >${f%.*}.d/${f}.sh
		#!/bin/sh
		echo in ${f}
		EOF
	done &&
	chmod +x prolog.d/*.sh epilog.d/*.sh
'
test_expect_success 'perilog: perilog-run fails without FLUX_JOB_ID' '
	test_expect_code 1 flux perilog-run prolog
'
test_expect_success 'perilog: perilog-run script works' '
	FLUX_JOB_ID=f1 flux perilog-run prolog -v -d prolog.d > prolog.output &&
	FLUX_JOB_ID=f1 flux perilog-run epilog -v -d epilog.d > epilog.output &&
	grep "in prolog" prolog.output &&
	grep "in epilog" epilog.output &&
	grep "in prolog.2" prolog.output &&
	grep "in epilog.2" epilog.output
'
test_expect_success 'perilog: perilog-run fails if any local script fails' '
	printf "#!/bin/sh\nexit 12" >prolog.d/fail.sh &&
	chmod +x prolog.d/fail.sh &&
	( export FLUX_JOB_ID=f1 &&
	  test_expect_code 12 \
	    flux perilog-run prolog -v -d prolog.d 
	) &&
	rm -f prolog.d/fail.sh
'
test_expect_success 'perilog: perilog-run timeout works with local scripts' '
	printf "#!/bin/sh\nsleep 60" >prolog.d/timeout.sh &&
	chmod +x prolog.d/timeout.sh &&
	( export FLUX_JOB_ID=f1 &&
	  test_expect_code 143 \
	    flux perilog-run prolog --timeout=0.5s -vv -d prolog.d
	) &&
	rm -f prolog.d/timeout.sh
'
test_expect_success 'perilog: perilog-run --exec-per-rank works' '
	jobid=$(flux submit -n4 -N4 true) &&
	flux job wait-event $jobid alloc &&
	FLUX_JOB_ID=${jobid} \
	  flux perilog-run prolog -veflux,getattr,rank \
	  >prolog-e.out &&
	$(no_drained_ranks) &&
	test_debug "cat prolog-e.out" &&
	sort prolog-e.out > prolog-e.sorted &&
	seq 0 3 > prolog-e.expected &&
	test_cmp prolog-e.expected prolog-e.sorted
'
test_expect_success 'perilog: failed ranks are drained with --exec-per-rank' '
	printf "#!/bin/sh\n! test \$(flux getattr rank) -eq \"\$1\"" \
	  >fail-on.sh &&
	chmod +x fail-on.sh &&
	(export FLUX_JOB_ID=${jobid}  &&
	 test_expect_code 1 flux perilog-run prolog -ve./fail-on.sh,1
	) &&
	test "$(drained_ranks)" = "1" &&
	undrain_all
'
test_expect_success 'perilog: failed ranks generate useful error messages' '
	cat <<-EOF >fail-err.sh &&
	#!/bin/sh
	if test \$(flux getattr rank) -eq \$1; then
		echo failing on \$1 >&2
		exit 1
	fi
	EOF
	chmod +x fail-err.sh &&
	(export FLUX_JOB_ID=${jobid}  &&
	 test_expect_code 1 flux perilog-run prolog -ve./fail-err.sh,1 \
	 >fail-err.out 2>&1
	) &&
	test_debug "cat fail-err.out" &&
	test "$(drained_ranks)" = "1" &&
	grep "$(hostname) (rank 1): failing on 1" fail-err.out &&
	undrain_all

'
test_expect_success 'perilog: can be run with timeout' '
	cat <<-EOF >fail-timeout.sh &&
	#!/bin/sh
	if test \$(flux getattr rank) -eq \$1; then
		sleep 120
	fi
	EOF
	chmod +x fail-timeout.sh &&
	(export FLUX_JOB_ID=${jobid} &&
	 test_expect_code 143 flux perilog-run prolog --timeout=1s \
		-ve./fail-timeout.sh,1 \
		>fail-timeout.out 2>&1
	) &&
	test_debug "cat fail-timeout.out" &&
	flux resource drain &&
	test "$(drained_ranks)" = "1" &&
	grep "timeout" fail-timeout.out &&
	undrain_all
'
test_expect_success 'perilog: load perilog.so plugin' '
	flux jobtap load perilog.so
'
test_expect_success 'perilog: configured prolog/epilog works for jobs' '
	undrain_all &&
	jobid=$(flux submit --job-name=works hostname) &&
	flux job attach -vE -w clean $jobid > submit1.log 2>&1 &&
	test_debug "cat submit1.log" &&
	grep prolog-start submit1.log &&
	grep prolog-finish submit1.log &&
	grep epilog-start submit1.log &&
	grep epilog-finish submit1.log
'
test_expect_success 'perilog: job can be canceled while prolog is running' '
	printf "#!/bin/sh\nsleep 60" > prolog.d/sleep.sh &&
	chmod +x prolog.d/sleep.sh &&
	test_when_finished "rm -f prolog.d/sleep.sh" &&
	jobid=$(flux submit --job-name=cancel hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux cancel $jobid &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux job wait-event -t 15 $jobid exception &&
	test_must_fail flux job attach -vE $jobid
'
test_expect_success 'perilog: nonfatal exception does not terminate prolog' '
	cat <<-EOF >prolog.d/cancel.sh &&
	#!/bin/sh
	flux job raise --severity=1 --type=test \$FLUX_JOB_ID
	flux job wait-event \$FLUX_JOB_ID exception
	EOF
	chmod +x prolog.d/cancel.sh &&
	test_when_finished "rm -f prolog.d/cancel.sh" &&
	jobid=$(flux submit --job-name=nocancel hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux job wait-event -vt 15 $jobid clean &&
	flux jobs $(flux job last) &&
	flux job status -vvv $jobid
'
test_expect_success 'perilog: job can timeout after prolog' '
	printf "#!/bin/sh\nsleep 1" > prolog.d/sleep.sh &&
	chmod +x prolog.d/sleep.sh &&
	test_when_finished "rm -f prolog.d/sleep.sh" &&
	jobid=$(flux submit --job-name=timeout -t 0.5s sleep 10) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux job wait-event -vt 15 $jobid exception &&
	flux job wait-event -t 15 $jobid clean
'
test_expect_success 'perilog: job can be canceled after prolog is complete' '
	printf "#!/bin/sh\nsleep 0" > prolog.d/sleep.sh &&
	chmod +x prolog.d/sleep.sh &&
	test_when_finished "rm -f prolog.d/sleep.sh" &&
	jobid=$(flux submit --job-name=cancel2 sleep 300) &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux cancel $jobid &&
	flux job wait-event -t 15 $jobid exception &&
	flux job wait-event -t 15 $jobid clean
'
test_expect_success 'perilog: prolog failure raises job exception' '
	printf "#!/bin/sh\n/bin/false" > prolog.d/fail.sh &&
	chmod +x prolog.d/fail.sh &&
	test_when_finished "rm -f prolog.d/fail.sh" &&
	jobid=$(flux submit --job-name=prolog-failure hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux job wait-event -m type=prolog -t 15 $jobid exception &&
	flux job eventlog -f json $jobid | jq -r -S .name \
	  > fail.events &&
	test_must_fail grep ^start$ fail.events
'
test_expect_success 'perilog: prolog/epilog output is logged' '
	printf "#!/bin/sh\necho this is the prolog" > prolog.d/log.sh &&
	chmod +x prolog.d/log.sh &&
	test_when_finished "rm -f prolog.d/log.sh" &&
	jobid=$(flux submit --job-name=output-test hostname) &&
	flux job wait-event -t 15 $jobid prolog-finish &&
	flux dmesg | grep "prolog: stdout: this is the prolog" &&
	flux job wait-event -vt 15 $jobid clean
'
test_expect_success 'perilog: fails if configuration is not valid' '
	flux jobtap remove perilog.so &&
	cat <<-EOF >config/perilog.toml &&
	[job-manager.prolog]
	command = "flux perilog-run prolog"
	EOF
	flux config reload &&
	test_must_fail flux jobtap load perilog.so &&
	cat <<-EOF2 >config/perilog.toml &&
	[job-manager.epilog]
	command = "flux perilog-run epilog"
	EOF2
	flux config reload &&
	test_must_fail flux jobtap load perilog.so &&
	cat <<-EOF3 >config/perilog.toml &&
	[job-manager.epilog]
	extra = 1
	command = [ "flux",  "perilog-run",  "epilog" ]
	EOF3
	flux config reload &&
	test_must_fail flux jobtap load perilog.so
'
test_expect_success 'perilog: fails if command not found' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.prolog]
	command = [ "/noexist" ]
	EOF
	flux config reload &&
	flux jobtap load perilog.so &&
	jobid=$(flux submit --job-name=prolog-failure hostname) &&
	test_must_fail flux job attach -vEX $jobid
'
test_expect_success 'perilog: prolog is killed even if it ignores SIGTERM' '
	cat <<-EOF >trap-sigterm.sh &&
	#!/bin/sh
	trap "echo trap-sigterm got SIGTERM" 15
	flux kvs put trap-ready=1
	sleep 60 &
	pid=\$!
	wait \$pid
	sleep 60
	EOF
	chmod +x trap-sigterm.sh &&
	cat <<-EOT >config/perilog.toml &&
	[job-manager.prolog]
	kill-timeout = 0.5
	command = [ "$(pwd)/trap-sigterm.sh" ]
	EOT
	flux config reload &&
	flux jobtap load --remove=*.so perilog.so &&
	jobid=$(flux submit --job-name=prolog-sigkill hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux cancel $jobid &&
	flux job wait-event -t 15 -m status=9 $jobid prolog-finish &&
	test_must_fail flux job attach -vEX $jobid
'

test_expect_success 'perilog: epilog can be specified without a prolog' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.epilog]
	command = [ "/bin/true" ]
	EOF
	flux config reload &&
	flux jobtap load --remove=*.so perilog.so &&
	jobid=$(flux submit hostname) &&
	test_must_fail flux job wait-event -t 15 $jobid prolog-start &&
	flux job wait-event -t 15 $jobid epilog-start &&
	flux job wait-event -t 15 $jobid epilog-finish
'
test_expect_success 'perilog: canceled prolog does not drain ranks' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.prolog]
	command = [ "flux", "perilog-run", "prolog", "-vesleep,30" ]
	EOF
	flux config reload &&
	flux jobtap load --remove=*.so perilog.so &&
	jobid=$(flux submit hostname) &&
	flux job wait-event -t 15 $jobid prolog-start &&
	flux cancel $jobid &&
	flux job wait-event -vt 15 $jobid prolog-finish &&
	flux resource drain &&
	test "$(drained_ranks)" = ""
'
test_expect_success 'perilog: log-ignore works' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.prolog]
	command = [ "printf", "foo: whee!\nbar: woo!\nbaz: important!\n" ]
	[job-manager.perilog]
	log-ignore = [ "^foo:.*", "^bar:" ]
	EOF
	flux config reload &&
	flux jobtap load --remove=*.so perilog.so &&
	flux dmesg -c >/dev/null &&
	flux run hostname &&
	flux dmesg -H > dmesg.out &&
	test_debug "cat dmesg.out" &&
	test_must_fail grep foo: dmesg.out &&
	test_must_fail grep bar: dmesg.out &&
	grep baz: dmesg.out
'
test_expect_success 'perilog: bad log-ignore entry is caught' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.perilog]
	log-ignore = "foo"
	EOF
	test_must_fail flux config reload 2>bad-log-ignore.err &&
	test_debug "cat bad-log-ignore.err" &&
	grep "not an array" bad-log-ignore.err
'
test_expect_success 'perilog: bad log-ignore regexp is caught' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.perilog]
	log-ignore = [ "[" ]
	EOF
	test_must_fail flux config reload 2>bad-log-ignore-regexp.err &&
	test_debug "cat bad-log-ignore-regexp.err" &&
	grep "[fF]ailed to compile" bad-log-ignore-regexp.err
'

#  Note: run this job before taking rank 3 offline below
test_expect_success 'perilog: run job across all 4 ranks' '
	jobid=$(flux submit --wait-event=clean -N4 -n4 true)
'
#  Note: rank 3 is taken offline after this point for testing handling
#  of offline ranks.
#
#  The rank is taken offline via the test jobtap plugin offline.so, which
#  explicitly sets rank 3 offline in RUN state before allowing the job to
#  proceed. This is required to simulate a rank going offline between
#  the scheduler assigning resources and the prolog starting.
#
test_expect_success 'perilog: create config to run flux-perilog-run' '
	cat <<-EOF >config/perilog.toml &&
	[job-manager.prolog]
	command = [ "flux", "perilog-run", "prolog", "-e", "true" ]
	EOF
	flux config reload
'
test_expect_success 'perilog: load offline.so before perilog.so' '
	flux jobtap load $OFFLINE_PLUGIN &&
	flux jobtap load --remove=perilog.so perilog.so
'
test_expect_success 'perilog: prolog with offline ranks raises sev 1 exception' '
	id=$(flux submit -N4 -n4 true) &&
	flux job wait-event -vt 15 -m severity=1 $id exception &&
	flux job wait-event -t 15 $id clean &&
	test_must_fail flux job attach $id &&
	flux jobtap remove offline.so
'
test_expect_success 'perilog: offline ranks are logged by prolog' '
	flux dmesg -HL | grep "rank 3 offline"
'
test_expect_success 'perilog: check that rank 3 is now offline' '
	flux resource status &&
	test_must_fail $startctl wait 3 &&
	test "$(flux resource status -s offline -no {ranks})" = "3"
'
test_expect_success 'perilog: offline ranks are skipped with --exec-per-rank' '
	(export FLUX_JOB_ID=${jobid}  &&
	 test_expect_code 1 \
		flux perilog-run prolog -ve flux,getattr,rank \
		| sort >offline.out
	) &&
	test "$(drained_ranks)" = "" &&
	cat <<-EOF >offline.expected &&
	0
	1
	2
	EOF
	test_cmp offline.expected offline.out
'
test_expect_success 'perilog: offline ranks are drained with --drain-offline' '
	(export FLUX_JOB_ID=${jobid}  &&
	 test_expect_code 1 \
		flux perilog-run prolog --drain-offline -ve flux,getattr,rank \
		| sort >offline-drain.out
	) &&
	flux resource drain &&
	test "$(drained_ranks)" = "3" &&
	test_cmp offline.expected offline-drain.out

'
test_done
