#!/bin/sh

test_description='Test flux shutdown command'

. $(dirname $0)/sharness.sh

test_under_flux 4

runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py"
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

export FLUX_URI_RESOLVE_LOCAL=t

test_expect_success 'flux-shutdown -h prints custom usage' '
	flux shutdown -h 2>usage &&
	grep "Usage:.*TARGET" usage
'
test_expect_success 'flux-shutdown fails on unknown option' '
	test_must_fail flux shutdown --notopt 2>notopt.err &&
	grep "unrecognized option" notopt.err
'
test_expect_success 'flux-shutdown fails if FLUX_URI is set wrong' '
	(FLUX_URI=noturi test_must_fail flux shutdown 2>baduri.err) &&
	grep "connecting to Flux" baduri.err
'
test_expect_success 'flux-shutdown fails if job argument is not a valid URI' '
	test_must_fail flux shutdown baduri 2>baduri2.err &&
	test_debug "cat baduri2.err" &&
	grep "failed to resolve" baduri2.err
'
test_expect_success 'flux-shutdown fails if job argument is unknown' '
	test_must_fail flux shutdown 12345 2>unkjobid.err &&
	grep "jobid 12345 not found" unkjobid.err
'

test_expect_success 'run a test job to completion' '
	flux submit --wait -n1 flux start /bin/true >jobid
'
test_expect_success 'flux-shutdown fails if job is not running' '
	test_must_fail flux shutdown $(cat jobid) 2>notrun.err &&
	test_debug "cat notrun.err" &&
	grep "jobid $(cat jobid) is not running" notrun.err
'

test_expect_success 'submit batch script and wait for it to start' '
	cat >batch.sh <<-EOT &&
	#!/bin/sh
	touch job2-has-started
	flux run sleep 300
	EOT
	chmod +x batch.sh &&
	flux batch -t30m -n1 batch.sh >jobid2 &&
	$waitfile job2-has-started
'
test_expect_success 'flux-shutdown JOBID works' '
	flux shutdown $(cat jobid2) 2>batch.err
'
test_expect_success 'shutdown output contains rc3 exit code (0)' '
	grep "rc3.*Exited (rc=0)" batch.err
'
test_expect_success 'shutdown output contains state transition to goodbye' '
	grep "rc3-success: finalize->goodbye" batch.err
'
test_expect_success 'job exit code indicates SIGHUP termination' '
	test_expect_code 129 flux job status $(cat jobid2)
'

test_expect_success 'submit non-batch job and wait for it to start' '
	flux submit -n1 \
		bash -c "touch job3-has-started && sleep 300" >jobid3 &&
	$waitfile job3-has-started
'
test_expect_success 'flux-shutdown JOBID fails when JOBID is not a flux instance' '
	test_must_fail flux shutdown $(cat jobid3) 2>notflux.err &&
	test_debug "cat notflux.err" &&
	grep "URI not found" notflux.err
'
test_expect_success 'cancel that job' '
	flux cancel $(cat jobid3)
'

test_expect_success 'run instance with no initial program and wait for it to start' '
	flux submit --wait-event=start \
		flux start -o,-Sbroker.rc2_none >jobid3 &&
	run_timeout 30 bash -c "while ! flux uri $(cat jobid3) >uri3; do \
		sleep 0.1; \
	done"
'
# Retries required here because shutdown fails if instance is not yet
# in RUN state
test_expect_success 'flux-shutdown --quiet works' '
	while ! flux shutdown --quiet $(cat uri3) 2>shut3.err \
		&& grep "cannot be initiated in state" shut3.err; do \
		: ; \
	done
'
test_expect_success 'successful shutdown output is empty' '
	count=$(wc -l <shut3.err) &&
	test $count -eq 0
'

test_expect_success 'run batch job and wait for it to start' '
	cat >batch4.sh <<-EOT &&
	#!/bin/sh
	touch job4-has-started
	flux run sleep 300
	EOT
	chmod +x batch4.sh &&
	flux batch -n1 batch4.sh >jobid4 &&
	$waitfile job4-has-started
'
test_expect_success 'flux-shutdown --verbose works' '
	flux shutdown --verbose $(cat jobid4) 2>shut4.err
'
test_expect_success 'shutdown output contains debug log messages' '
	grep -q "debug\[0\]:" shut4.err
'

test_expect_success 'run multi-node batch job and wait for it to start' '
	cat >batch5.sh <<-EOT &&
	#!/bin/sh
	touch job5-has-started
	flux run sleep 300
	EOT
	chmod +x batch5.sh &&
	flux batch -N2 batch5.sh >jobid5 &&
	$waitfile job5-has-started
'
test_expect_success 'flux-shutdown --background works' '
	flux shutdown --background $(cat jobid5) 2>shut5.err
'
test_expect_success 'job exit code indicates SIGHUP termination' '
	test_expect_code 129 flux job status $(cat jobid5)
'

test_expect_success 'flux-shutdown as initial program does not hang' '
	test_expect_code 129 run_timeout 30 flux start flux shutdown
'

test_expect_success 'submit batch script and wait for it to start' '
	rm -f job6-has-started &&
	cat >batch6.sh <<-EOT &&
	#!/bin/sh
	flux run /bin/true
	touch job6-has-started
	sleep 300
	EOT
	chmod +x batch6.sh &&
	flux batch -t30m -n1 batch6.sh >jobid6 &&
	$waitfile job6-has-started
'

test_expect_success 'one job has run in the batch job' '
	(FLUX_URI=$(flux uri --local $(cat jobid6)) \
	    flux jobs -n -a -o {id}) >job6_list &&
	test $(wc -l <job6_list) -eq 1
'

test_expect_success 'shutdown batch script with --dump' '
	(FLUX_URI=$(flux uri --local $(cat jobid6)) \
	    flux shutdown --dump=dump.tgz)
'
test_expect_success 'dump file was created' '
	tar tvf dump.tgz
'
test_expect_success 'restart batch script from dump and wait for it to start' '
	rm -f job6-has-started &&
	flux batch -t30m -n1 \
	    --broker-opts=-Scontent.restore=dump.tgz \
	    batch6.sh >jobid6_try2 &&
	$waitfile job6-has-started
'
test_expect_success 'two jobs have been run in batch job' '
	(FLUX_URI=$(flux uri --local $(cat jobid6_try2)) \
	    flux jobs -n -a -o {id}) >job6_list_try2 &&
	test $(wc -l <job6_list_try2) -eq 2
'
test_expect_success 'job id from before restart is in job listing' '
	grep $(cat job6_list) job6_list_try2
'

test_expect_success 'shutdown batch script with --gc' '
	(FLUX_URI=$(flux uri --local $(cat jobid6_try2)) \
	    flux shutdown --gc)
'
test_expect_success 'dump file was created with RESTORE link' '
	tar tvf dump/RESTORE
'
test_expect_success 'clean up dump files from previous tests' '
	rm -f dump.tgz &&
	rm -f dump/RESTORE
'
test_expect_success 'create config with large gc-threshold config' '
	cat >kvs.toml <<-EOT
	[kvs]
	gc-threshold = 10000
	EOT
'
test_expect_success 'submit batch script and wait for it to start (1)' '
	rm -f job7-has-started &&
	cat >batch7.sh <<-EOT &&
	#!/bin/sh
	flux run /bin/true
	touch job7-has-started
	sleep 300
	EOT
	chmod +x batch7.sh &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7 &&
	$waitfile job7-has-started
'
test_expect_success 'shutdown batch script' '
	(FLUX_URI=$(flux uri --local $(cat jobid7)) \
	    flux shutdown)
'
test_expect_success 'RESTORE dump not created, gc-threshold not crossed yet' '
	test_must_fail ls dump/RESTORE
'
test_expect_success 'create config with small gc-threshold config' '
	cat >kvs.toml <<-EOT
	[kvs]
	gc-threshold = 5
	EOT
'
test_expect_success 'submit batch script and wait for it to start (2)' '
	rm -f job7-has-started &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7_try2 &&
	$waitfile job7-has-started
'
# this test goes into a script, so we can pass to test_must_fail
test_expect_success 'shutdown, gc threshold crossed, error no -y or -n' '
	cat >test_no_y_or_n_cmdline.sh <<-EOT &&
	#!/bin/sh
	FLUX_URI=$(flux uri --local $(cat jobid7_try2))
	flux shutdown
	EOT
	chmod +x test_no_y_or_n_cmdline.sh &&
	test_must_fail ./test_no_y_or_n_cmdline.sh
'
test_expect_success 'shutdown, gc threshold crossed, user specifies -n' '
	(FLUX_URI=$(flux uri --local $(cat jobid7_try2)) \
	    flux shutdown -n)
'
test_expect_success 'RESTORE dump not created, user declined' '
	test_must_fail ls dump/RESTORE
'
test_expect_success 'submit batch script and wait for it to start (3)' '
	rm -f job7-has-started &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7_try3 &&
	$waitfile job7-has-started
'
test_expect_success 'shutdown, gc threshold crossed, user specifies -y' '
	(FLUX_URI=$(flux uri --local $(cat jobid7_try3)) \
	    flux shutdown -y)
'
test_expect_success 'RESTORE dump created after user accepted' '
	tar tvf dump/RESTORE
'
test_expect_success 'clean up dump files from previous tests' '
	rm -f dump.tgz &&
	rm -f dump/RESTORE
'
test_expect_success 'submit batch script and wait for it to start (4)' '
	rm -f job7-has-started &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7_try4 &&
	$waitfile job7-has-started
'
test_expect_success 'shutdown, gc threshold crossed, user input n' '
	cat <<-EOF >jobid7_try4.in &&
	{ "version": 2 }
	[0.5, "i", "n\n"]
	EOF
	(FLUX_URI=$(flux uri --local $(cat jobid7_try4)) \
	    $runpty --input=jobid7_try4.in flux shutdown)
'
test_expect_success 'RESTORE dump not created, user declined' '
	test_must_fail ls dump/RESTORE
'
test_expect_success 'submit batch script and wait for it to start (5)' '
	rm -f job7-has-started &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7_try5 &&
	$waitfile job7-has-started
'
test_expect_success 'shutdown, gc threshold crossed, user input y' '
	cat <<-EOF >jobid7_try5.in &&
	{ "version": 2 }
	[0.5, "i", "y\n"]
	EOF
	(FLUX_URI=$(flux uri --local $(cat jobid7_try5)) \
	    $runpty --input=jobid7_try5.in flux shutdown)
'
test_expect_success 'RESTORE dump created after user accepted' '
	tar tvf dump/RESTORE
'
test_expect_success 'clean up dump files from previous tests' '
	rm -f dump.tgz &&
	rm -f dump/RESTORE
'
test_expect_success 'submit batch script and wait for it to start (6)' '
	rm -f job7-has-started &&
	FLUX_CONF_DIR=$(pwd) flux batch -t30m -n1 batch7.sh >jobid7_try6 &&
	$waitfile job7-has-started
'
test_expect_success 'shutdown, gc threshold crossed, user input default' '
	cat <<-EOF >jobid7_try6.in &&
	{ "version": 2 }
	[0.5, "i", "\n"]
	EOF
	(FLUX_URI=$(flux uri --local $(cat jobid7_try6)) \
	    $runpty --input=jobid7_try6.in flux shutdown)
'
test_expect_success 'RESTORE dump created after user accepted' '
	tar tvf dump/RESTORE
'
test_expect_success 'clean up dump files from previous tests' '
	rm -f dump.tgz &&
	rm -f dump/RESTORE
'
test_expect_success 'submit batch with dump=auto and wait for it to start (8)' '
	cat >batch.sh <<-EOT &&
	#!/bin/sh
	touch job8-has-started
	flux run sleep 300
	EOT
	chmod +x batch.sh &&
	flux batch -t30m -n1 \
	    --broker-opts=-Scontent.dump=auto batch.sh >jobid8 &&
	$waitfile job8-has-started
'
test_expect_success 'shutdown --skip-gc does not produce dump' '
	FLUX_URI=$(flux uri --local $(cat jobid8)) flux shutdown --skip-gc &&
	test_must_fail tar tvf dump/RESTORE
'

test_done
