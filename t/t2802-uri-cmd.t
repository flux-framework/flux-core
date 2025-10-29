#!/bin/sh

test_description='Test flux uri command'

. $(dirname $0)/sharness.sh

# Required to be able to test LSF resolver
export LSB_JOBID=12345

test_under_flux 2

testssh="${SHARNESS_TEST_SRCDIR}/scripts/tssh"


test_expect_success 'flux-uri -h prints list of resolvers' '
	flux uri --help >help.out >help.out 2>&1 &&
	test_debug "cat help.out" &&
	grep "Supported resolver schemes" help.out
'
test_expect_success 'flux-uri rejects invalid URI' '
	test_expect_code 1 flux uri bar &&
	test_expect_code 1 flux uri bar:foo
'
test_expect_success 'flux-uri passes through ssh and local URIs unchanged' '
	ssh_uri="ssh://bongo@foo.com:123/tmp/flux-xyzzy/local-0" &&
	local_uri="local:///tmp/flux-xyzzy/local-0" &&
	result=$(flux uri $ssh_uri) &&
	test_debug "echo flux uri $ssh_uri returns $result" &&
	test "$result" = "$ssh_uri" &&
	result=$(flux uri $local_uri) &&
	test_debug "echo flux uri $local_uri returns $result" &&
	test "$result" = "$local_uri" &&
	result=$(flux uri --local $ssh_uri) &&
	test_debug "echo flux uri --local $ssh_uri returns $result" &&
	test "$result" = "$local_uri" &&
	result=$(flux uri --remote $local_uri) &&
	test_debug "echo flux uri --remote $local_uri returns $result" &&
	test "$result" = "ssh://$(hostname)/tmp/flux-xyzzy/local-0"
'
test_expect_success 'flux-uri pid resolver works' '
	test "$(flux uri pid:$$)" =  "$FLUX_URI"
'
test_expect_success 'flux-uri pid resolver works with ?local' '
	test "$(flux uri pid:$$?local)" =  "$FLUX_URI"
'
test_expect_success 'flux-uri pid resolver works with ?remote' '
	test "$(flux uri pid:$$?remote)" =  "$(flux uri --remote $FLUX_URI)"
'
test_expect_success 'flux-uri pid resolver works on flux-broker' '
	test "$(flux uri pid:$(flux getattr broker.pid))" =  "$FLUX_URI"
'
test_expect_success 'flux-uri pid resolver works on flux-broker with fallback' '
	(
	  export FLUX_FORCE_BROKER_CHILD_FALLBACK=t &&
	  test "$(flux uri pid:$(flux getattr broker.pid))" =  "$FLUX_URI"
	)
'
test_expect_success 'flux-uri pid resolver fails if broker has no child' '
	test_must_fail flux uri pid:$(flux exec -r 1 flux getattr broker.pid) \
		>broker-no-child.out 2>&1 &&
	grep "is a flux-broker and no child found" broker-no-child.out
'
test_expect_success 'flux-uri pid resolver fails for nonexistent pid' '
	test_expect_code 1 flux uri pid:123456
'
test_expect_success 'flux-uri pid resolver fails with ?local&remote' '
	test_expect_code 1 flux uri "pid:$$?local&remote"
'
test_expect_success NO_CHAIN_LINT 'flux-uri pid scheme fails for non-flux pid' '
	pid=$(bash -c "unset FLUX_URI;sleep 30 >/dev/null 2>&1 & echo \$!") &&
	test_expect_code 1 flux uri pid:$pid &&
	kill $pid
'
test_expect_success 'flux uri fails for completed job' '
	complete_id=$(flux submit --wait flux start true) &&
	test_expect_code 1 flux uri ${complete_id} 2>jobid-notrunning.log &&
	test_debug "cat jobid-notrunning.log" &&
	grep "not running" jobid-notrunning.log
'
test_expect_success 'start a small hierarchy of Flux instances' '
	cat <<-EOF >batch.sh &&
	#!/bin/sh
	jobid=\$(flux submit -n1 flux start flux run sleep 300) &&
	flux --parent job memo \$(flux getattr jobid) jobid=\$jobid &&
	flux job attach \$jobid
	EOF
	chmod +x batch.sh &&
	jobid=$(flux batch -n1 batch.sh) &&
	flux job wait-event -T offset -vt 180 -c 2 $jobid memo
'
test_expect_success 'flux uri resolves jobid argument' '
	flux proxy $(flux uri --local $jobid) flux getattr jobid >jobid1.out &&
	test "$(cat jobid1.out)" = "$jobid"
'
test_expect_success 'flux uri resolves hierarchical jobid argument'  '
	jobid2=$(flux jobs -no {user.jobid} $jobid) &&
	test_debug "echo attempting to resolve jobid:${jobid}/${jobid2}" &&
	uri=$(FLUX_SSH=$testssh flux uri --local jobid:${jobid}/${jobid2}) &&
	test_debug "echo jobid:${jobid}/${jobid2} is ${uri}" &&
	uri=$(FLUX_SSH=$testssh flux uri --local ${jobid}/${jobid2}) &&
	test_debug "echo ${jobid}/${jobid2} is ${uri}"
'
test_expect_success 'flux uri resolves hierarchical jobids with ?local' '
	test_debug "echo attempting to resolve jobid:${jobid}/${jobid2}" &&
	uri=$(flux uri jobid:${jobid}/${jobid2}?local) &&
	test_debug "echo jobid:${jobid}/${jobid2}?local is ${uri}" &&
	uri=$(flux uri ${jobid}/${jobid2}?local) &&
	test_debug "echo ${jobid}/${jobid2}?local is ${uri}"

'
test_expect_success 'flux uri works with relative paths' '
	root_uri=$(FLUX_SSH=$testssh flux uri --local .) &&
	job1_uri=$(FLUX_SSH=$testssh flux uri --local ${jobid}) &&
	job2_uri=$(FLUX_SSH=$testssh flux uri --local ${jobid}/${jobid2}) &&
	uri=$(FLUX_SSH=$testssh flux proxy $job2_uri flux uri /) &&
	test_debug "echo flux uri / got ${uri} expected ${root_uri}" &&
	test "$uri" = "$root_uri" &&
	uri=$(FLUX_SSH=$testssh flux proxy $job2_uri flux uri ../..) &&
	test_debug "echo flux uri ../.. got ${uri} expected ${root_uri}" &&
	test "$uri" = "$root_uri" &&
	uri=$(FLUX_SSH=$testssh flux proxy $job2_uri flux uri ..) &&
	test_debug "echo flux uri .. got ${uri} expected ${job1_uri}" &&
	test "$uri" = "$job1_uri" &&
	uri=$(FLUX_SSH=$testssh flux proxy $job2_uri flux uri .) &&
	test_debug "echo flux uri . got ${uri} expected ${job2_uri}" &&
	test "$uri" = "$job2_uri"
'
test_expect_success 'flux uri --wait can resolve URI for pending job' '
	uri=$(flux uri --wait $(flux batch -n1 --wrap hostname)) &&
	flux job wait-event -vt 60 $(flux job last) clean  &&
	test "$uri" = "$(flux jobs -no {uri} $(flux job last))"
'
test_expect_success 'terminate batch job cleanly' '
	flux proxy $(flux uri --local ${jobid}) flux cancel --all &&
	flux job wait-event -vt 60 ${jobid} clean
'
test_expect_success 'flux uri jobid returns error for non-instance job' '
	id=$(flux submit sleep 600) &&
	test_expect_code 1 flux uri $id
'
test_expect_success 'flux uri jobid scheme returns error for invalid jobid' '
	test_expect_code 1 flux uri jobid:boop
'
test_expect_success 'flux uri jobid scheme returns error for unknown jobid' '
	test_expect_code 1 flux uri jobid:f1
'
test_expect_success 'setup fake srun and scontrol cmds for mock slurm testing' '
	#  slurm resolver runs `srun flux uri slurm:jobid`
	#  mock the execution of that command here by just returning a uri
	cat <<-EOF >srun &&
	#!/bin/sh
	test -n "\$SRUN_FAIL" && exit 1
	exec flux uri pid:$$
	EOF
	chmod +x srun &&
	cat <<-EOF >squeue &&
	#!/bin/sh
	# used by resolver to get nodelist, just echo a couple fake hosts
	test -n "\$SQUEUE_FAIL" && exit 1
	test -n "\$SQUEUE_EMPTY" && exit 0
	echo foo[100-101]
	EOF
	chmod +x squeue &&
	#  slurm resolver attempts to list pids from `scontrol listpids`
	#  return a single listpids line with our pid for mocking
	#  set
	cat <<-EOF >scontrol &&
	#!/bin/sh
	test -n "\$REMOTE" && exit 1
	echo "PID      JOBID    STEPID   LOCALID GLOBALID"
	echo "1         1234      1234         0        0"
	echo "$$        1234      1234         0        0"
	EOF
	chmod +x scontrol
'
test_expect_success 'flux-uri mock testing of slurm resolver works' '
	result=$(PATH=$(pwd):$PATH flux uri --local slurm:1234) &&
	test_debug "echo slurm:1234 got $result" &&
	test "$result" = "$FLUX_URI" &&
	result=$(PATH=$(pwd):$PATH REMOTE=t flux uri --local slurm:1234) &&
	test_debug "echo slurm:1234 with REMOTE=t got $result" &&
	test "$result" = "$FLUX_URI" &&
	( export PATH=$(pwd):$PATH REMOTE=t SRUN_FAIL=t &&
	  test_expect_code 1 flux uri slurm:1234 ) &&
	( export PATH=$(pwd):$PATH REMOTE=t SQUEUE_FAIL=t &&
	  test_expect_code 1 flux uri slurm:1234 2>squeue1.err) &&
	test_debug "cat squeue1.err" &&
	grep -i "unable to query nodelist" squeue1.err &&
	( export PATH=$(pwd):$PATH REMOTE=t SQUEUE_EMPTY=t &&
	  test_expect_code 1 flux uri slurm:1234 2>squeue2.err) &&
	test_debug "cat squeue2.err" &&
	grep -i "empty nodelist" squeue2.err
'
test_expect_success 'setup fake csm_allocation_query for mock lsf testing' '
	cat <<-EOF >csm_allocation_query &&
	#!/bin/sh
	test -n "\$LSF_FAIL" && exit 4
	echo "compute_nodes:"
	echo " - lassen9"
	echo " - lassen10"
	EOF
	chmod +x csm_allocation_query &&
	export CSM_ALLOCATION_QUERY=$(pwd)/csm_allocation_query &&
	export FLUX_SSH=${SHARNESS_TEST_SRCDIR}/scripts/tssh
'
test_expect_success 'flux-uri mock testing of lsf resolver works' '
	result=$(SHELL=/bin/sh flux uri --local lsf:12345) &&
	FLUX_URI_1=$(flux exec -r 1 flux getattr local-uri) &&
	test_debug "echo checking if $result is $FLUX_URI or $FLUX_URI_1" &&
	test "$result" = "$FLUX_URI" -o "$result" = "$FLUX_URI_1"
'
test_expect_success 'cleanup jobs' '
	flux cancel --all &&
	flux queue drain
'
test_done
