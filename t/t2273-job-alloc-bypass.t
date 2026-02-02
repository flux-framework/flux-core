#!/bin/sh

test_description='Test alloc-bypass job manager plugin'

. $(dirname $0)/sharness.sh

test_under_flux 2 job -Slog-stderr-level=1


flux version | grep -q libflux-security && test_set_prereq FLUX_SECURITY

submit_as_alternate_user()
{
        FAKE_USERID=42
        test_debug "echo running flux run $@ as userid $FAKE_USERID"
        flux run --dry-run "$@" | \
	  flux python ${SHARNESS_TEST_SRCDIR}/scripts/sign-as.py $FAKE_USERID \
            >job.signed
        FLUX_HANDLE_USERID=$FAKE_USERID \
          flux job submit --flags=signed job.signed
}

test_expect_success 'alloc-bypass: start a job using all resources' '
	SLEEPID=$(flux submit --wait-event=start \
	            -n $(flux resource list -s up -no {ncores}) \
	            sleep 300)
'
test_expect_success 'alloc-bypass: load alloc-bypass plugin' '
	flux jobtap load alloc-bypass.so
'
test_expect_success 'alloc-bypass: works' '
	flux resource list &&
	run_timeout 15 \
	    flux run \
	        --setattr=system.alloc-bypass.R="$(flux kvs get resource.R)" \
	        -o per-resource.type=node hostname
'
test_expect_success 'alloc-bypass: scheduler still running' '
	flux ping -c 1 sched-simple
'
test_expect_success 'alloc-bypass: works with per-resource.type=core' '
	flux run \
	    --setattr=system.alloc-bypass.R="$(flux kvs get resource.R)" \
	    -o per-resource.type=core hostname
'
test_expect_success 'alloc-bypass: works with jobid' '
	flux run \
	    --setattr=system.alloc-bypass.R="$(flux job info $SLEEPID R)" \
	    -o per-resource.type=node flux getattr rank
'
test_expect_success FLUX_SECURITY 'alloc-bypass: guest user not allowed' '
	test_must_fail submit_as_alternate_user \
	  --setattr=system.alloc-bypass.R="$(flux kvs get resource.R)" \
          hostname
'
test_expect_success 'alloc-bypass; invalid alloc-bypass.R type fails' '
	test_must_fail flux run \
	    --setattr=system.alloc-bypass.R=1 \
	    -o per-resource.type=node hostname
'
test_expect_success 'alloc-bypass: invalid alloc-bypass.R object fails' '
	test_must_fail flux run \
	    --setattr=system.alloc-bypass.R="{\"version\": 1}" \
	    -o per-resource.type=node hostname
'
test_expect_success 'alloc-bypass: handles exception before alloc event' '
	jobid=$(flux submit \
	        --setattr=system.alloc-bypass.R="$(flux kvs get resource.R)" \
		--dependency=afterok:$SLEEPID \
		-o per-resource.type=node hostname) &&
	flux job wait-event -vt 15 $jobid dependency-add &&
	flux cancel $jobid &&
	test_must_fail flux job attach -vEX $jobid
'
test_expect_success 'alloc-bypass: kill sleep job' '
	flux cancel --all &&
	flux job wait-event $SLEEPID clean
'
test_expect_success 'alloc-bypass: submit an alloc-bypass job' '
	flux submit -vvv --wait-event=start --job-name=bypass \
		--setattr=alloc-bypass.R="$(flux R encode -r 0)" \
		-n 1 \
		sleep 300
'
test_expect_success 'alloc-bypass: a full system job can still be run' '
	run_timeout 15 \
	  flux run -n $(flux resource list -s up -no {ncores}) hostname
'
test_expect_success 'kill bypass job' '
	flux pkill bypass &&
	flux queue drain
'
test_expect_success 'alloc-bypass: submit an alloc-bypass job' '
	flux submit -vvv --wait-event=start --job-name=bypass2 \
		--setattr=alloc-bypass.R="$(flux R encode -r 0)" \
		-n 1 \
		sleep 300
'
test_expect_success 'alloc-bypass: scheduler has no nodes allocated' '
	test $(FLUX_RESOURCE_LIST_RPC=sched.resource-status flux resource list -s allocated -no {nnodes}) -eq 0
'
test_expect_success 'alloc-bypass: reload scheduler' '
	flux module reload sched-simple
'
# issue #5797 - bypass jobs must not be included in scheduler hello message
test_expect_success 'alloc-bypass: scheduler still has no nodes allocated' '
	test $(FLUX_RESOURCE_LIST_RPC=sched.resource-status flux resource list -s allocated -no {nnodes}) -eq 0
'
#
# now try a bypass job that persists across a flux restart
#
test_expect_success 'generate a config that loads alloc-bypass' '
	cat <<-EOT >config.toml
	[[job-manager.plugins]]
	load = "alloc-bypass.so"
	EOT
'
test_expect_success 'generate a script to submit a testexec+alloc-bypass job' '
	cat >prog.sh <<-EOT &&
	#!/bin/sh
	flux submit -vvv -N 1 \
            --flags=debug \
            --setattr=system.exec.test.run_duration=100s \
            --setattr=system.alloc-bypass.R="\$(flux resource R)" \
            --wait-event=start \
	    sleep 300
	EOT
	chmod +x prog.sh
'
test_expect_success 'run test job in a persistent instance' '
	FLUX_DISABLE_JOB_CLEANUP=t flux start -s1 \
	    --config-path=config.toml \
	    -Sstatedir=$(pwd) \
	    ./prog.sh
'
test_expect_success 'restart that instance and get the resource alloc count' '
	FLUX_DISABLE_JOB_CLEANUP=t flux start -s1 \
	    --config-path=config.toml \
	    -Sstatedir=$(pwd) \
	    sh -c "flux jobs -no {state} \$(flux job last); \
	        FLUX_RESOURCE_LIST_RPC=sched.resource-status \
		flux resource list -s allocated -no {nnodes}" >restart.out
'
test_expect_success 'the job was running and resources were not allocated' '
	cat >restart.exp <<-EOT &&
	RUN
	0
	EOT
	test_cmp restart.exp restart.out
'
test_expect_success 'restart that instance and cancel the job' '
	flux start -s1 \
	    --config-path=config.toml \
	    -Sstatedir=$(pwd) \
	    sh -c "flux cancel \$(flux job last); \
	    flux job wait-event -vvv \$(flux job last) clean"
'

test_done
