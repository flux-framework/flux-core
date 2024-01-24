#!/bin/sh

test_description='Test the flux-watch command'

. $(dirname $0)/sharness.sh

test_under_flux 2 job

export FLUX_PYCLI_LOGLEVEL=10
export SHELL=/bin/sh
waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"
runpty="${SHARNESS_TEST_SRCDIR}/scripts/runpty.py -f asciicast --line-buffer"

test_expect_success 'flux-watch: does nothing with no args' '
	test_expect_code 1 flux watch >no-args.log 2>&1 &&
	test_debug "cat no-args.log" &&
	grep "one job selection option is required" no-args.log
'
test_expect_success 'flux-watch: start some active jobs to watch' '
	cat <<-EOF >test.sh &&
	#!/bin/sh
	echo \$TEST_OUTPUT
	EOF
	chmod +x test.sh &&
	flux submit --urgency=hold --cc=1-2 --env=TEST_OUTPUT=test{cc} \
	    ./test.sh >active.ids
'
test_expect_success NO_CHAIN_LINT 'flux-watch: --active watches all active jobs' '
	flux watch -vv --active >active.log 2>&1 &
	$waitfile -t 30 -v -p "Watching 2 jobs" active.log &&
	for id in $(cat active.ids); do
	    flux job urgency $id default
	done &&
	wait &&
	test_debug "cat active.log" &&
	grep test1 active.log &&
	grep test2 active.log
'
test_expect_success 'flux-watch: ensure previous jobs are inactive (in case of chain-lint)' '
	for id in $(cat active.ids); do
            flux job urgency $id default || true
        done &&
	flux watch $(cat active.ids)
'
test_expect_success 'flux-watch: --all watches inactive jobs' '
	flux watch --all  >all.log 2>&1 &&
	test_debug "cat all.log" &&
	grep test1 all.log &&
	grep test2 all.log
'
test_expect_success 'flux-watch: run some failed jobs' '
	id=$(flux submit sh -c "echo test3; exit 2") &&
	id2=$(flux submit --urgency hold true) &&
	flux cancel $id2
'
test_expect_success 'flux-watch: exits with highest job exit code' '
	test_expect_code 2 flux watch --all
'
test_expect_success 'flux-watch: can specify multiple jobids' '
	flux watch $(cat active.ids) >by-jobid.out 2>&1 &&
	test_debug "cat by-jobid.out" &&
	grep test1 by-jobid.out &&
	grep test2 by-jobid.out
'
test_expect_success 'flux-watch: emits errors for invalid jobs' '
	flux watch f123 2>invalid-jobid.err &&
	grep "JobID f123 unknown" invalid-jobid.err
'
test_expect_success 'flux-watch: issues warning with jobids and filtering option' '
	flux watch -v --all $(cat active.ids) >all-error.out 2>&1 &&
	test_debug "cat all-error.out" &&
	grep "Filtering options ignored with jobid list" all-error.out
'
# Note: test with --since by using the t_submit of the last submitted job.
# This should only match up to the last two jobs, which were failed and
# canceled respectively, so the test must fail and the tool should output
# it watched only 1 or 2 jobs (we can't guarantee when the first failed
# job finished relative to submission of the second)
test_expect_success 'flux-watch: works with --since' '
	t_since=$(flux jobs -ac 1 -no {t_submit}) &&
	test_must_fail flux watch -v --since=$t_since >since.out 2>&1 &&
	test_debug "cat since.out" &&
	grep "Watching [12] job" since.out
'
# -1h should get us all 4 jobs
test_expect_success 'flux-watch: --since takes negative offset' '
	test_must_fail flux watch -v --since=-1h >since1h.out 2>&1 &&
	test_debug "cat since1h.out" &&
	grep "Watching 4 job" since1h.out
'
test_expect_success 'flux-watch: invalid --since fails' '
	test_expect_code 2 flux watch --since=-1g 2>since.err &&
	grep "invalid value" since.err
'
test_expect_success 'flux-watch: --since in future fails' '
	test_expect_code 2 flux watch --since=+1d 2>since2.err &&
	grep "appears to be in the future" since2.err
'
test_expect_success 'flux-watch: --count works' '
	test_might_fail flux watch -v --all --count=1 >count.out 2>&1 &&
	grep "Watching 1 job" count.out
'
test_expect_success 'flux-watch: --progress works' '
	test_might_fail $runpty flux watch --all --progress >progress.out &&
	test_debug "cat progress.out" &&
	grep "PD:0 *R:0 *CD:2 *F:2 .*100.0%" progress.out
'
test_expect_success 'flux-watch: --progress --jps works' '
	test_might_fail $runpty flux watch --all --progress --jps >jps.out &&
	test_debug "cat jps.out" &&
	grep "PD:0 *R:0 *CD:2 *F:2 .*job/s" jps.out
'

test_expect_success 'flux-watch: --progress is ignored with no tty' '
	test_might_fail flux watch --all --progress >progress-nopty.out 2>&1 &&
	test_debug "cat progress-nopty.out" &&
	grep "Ignoring --progress" progress-nopty.out
'
test_expect_success 'flux-watch: --label-io works' '
	test_might_fail flux watch --all --label-io >labelio.out 2>&1 &&
	test_debug "cat labelio.out" &&
	grep "0: test3" labelio.out &&
	grep "0: test2" labelio.out &&
	grep "0: test1" labelio.out
'
test_expect_success 'flux-watch: --filter works' '
	nfailed=$(flux jobs -no {id} -f failed,canceled | wc -l) &&
	test_expect_code 2 flux watch -v --filter=failed,canceled \
	    >failed.out 2>&1 &&
	test_debug "cat failed.out" &&
	grep "Watching ${nfailed} job" failed.out
'
test_expect_success 'flux-watch: handles binary data' '
	id=$(flux submit dd if=/dev/urandom count=1) &&
	test_debug "flux job eventlog -p guest.output -HL $id" &&
	flux job attach $id >binary.expected &&
	flux watch $id >binary.output &&
	test_cmp binary.expected binary.output
'
test_done
