#!/bin/sh

test_description='sched-simple pool-class: rack-local allocation using RackPool'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

# 4-node instance: ranks 0-1 in rack 0, ranks 2-3 in rack 1
test_under_flux 4 job

# Embed the pool file path in the writer URI so the scheduler can auto-discover
# RackPool at both the top level and in sub-instances without a pool-class= arg.
RACK_POOL_URI="file://${SHARNESS_TEST_SRCDIR}/scheduler/RackPool.py"

# Build R with rack topology injected into scheduling key
build_rack_R() {
	flux R encode -r0-3 -c0-1 | \
	    jq --arg writer "${RACK_POOL_URI}" '.scheduling = {
	        "writer": $writer,
	        "racks": [
	            {"id": 0, "ranks": "0-1"},
	            {"id": 1, "ranks": "2-3"}
	        ]
	    }'
}

test_expect_success 'load sched-simple with rack topology R' '
	flux module unload sched-simple &&
	build_rack_R >R.rack &&
	flux resource reload R.rack &&
	flux module load sched-simple
'
test_expect_success 'RackPool raises ValueError when scheduling key is absent' '
	cat >test_no_sched.py <<-EOF &&
	import sys
	sys.path.insert(0, "${SHARNESS_TEST_SRCDIR}/scheduler")
	from RackPool import RackPool
	import json, subprocess
	R = json.loads(subprocess.check_output(["flux", "R", "encode", "-r0-3", "-c0-1"]))
	RackPool(R, log=lambda lvl, msg: None)
	EOF
	test_must_fail flux python test_no_sched.py
'

test_expect_success 'RackPool raises ValueError when scheduling key has no rack topology' '
	cat >test_no_racks.py <<-EOF &&
	import sys
	sys.path.insert(0, "${SHARNESS_TEST_SRCDIR}/scheduler")
	from RackPool import RackPool
	import json, subprocess
	R = json.loads(subprocess.check_output(["flux", "R", "encode", "-r0-3", "-c0-1"]))
	R["scheduling"] = {"writer": "${RACK_POOL_URI}"}
	RackPool(R, log=lambda lvl, msg: None)
	EOF
	test_must_fail flux python test_no_racks.py
'

test_expect_success 'sub-instance on rack-local allocation inherits topology' '
	cat >batch-subinst.sh <<-EOF &&
	#!/bin/sh
	flux module unload sched-simple &&
	flux module load sched-simple &&
	jobid=\$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 \${jobid} alloc &&
	flux job info \${jobid} R | jq -e ".scheduling.racks | length == 1" &&
	flux job info \${jobid} R | jq -e ".scheduling.racks[0].ranks | tonumber < 2" &&
	flux job wait-event --timeout=10 \${jobid} clean &&
	flux run --nodes=2 --exclusive hostname
	EOF
	chmod +x batch-subinst.sh &&
	batchjob=$(flux batch --nodes=2 --exclusive batch-subinst.sh) &&
	flux job wait-event --timeout=30 ${batchjob} alloc &&
	flux job info ${batchjob} R | jq -e ".scheduling.writer | startswith(\"file://\")" &&
	flux job attach ${batchjob}
'
test_expect_success 'explicit pool-class=URI overrides writer-based discovery' '
	flux module unload sched-simple &&
	flux module load sched-simple pool-class=${RACK_POOL_URI} &&
	jobid=$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc &&
	flux job info ${jobid} R | jq -e ".scheduling.racks | length == 1" &&
	flux job wait-event --timeout=10 ${jobid} clean &&
	flux module unload sched-simple &&
	flux module load sched-simple
'

test_expect_success 'disable job-exec and validator' '
	flux module remove job-exec &&
	flux module reload -f job-ingest disable-validator
'
test_expect_success 'single-node job allocates successfully' '
	jobid=$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc
'
test_expect_success 'allocated R carries scheduling key with writer' '
	flux job info ${jobid} R | jq -e ".scheduling.racks" &&
	flux job info ${jobid} R | jq -e ".scheduling.writer | startswith(\"file://\")"
'
test_expect_success 'cleanup single-node job' '
	flux cancel ${jobid} &&
	flux job wait-event --timeout=10 ${jobid} clean
'
test_expect_success 'two-node job allocates within one rack' '
	jobid=$(flux submit --nodes=2 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc &&
	R=$(flux job info ${jobid} R) &&
	ranks=$(echo ${R} | jq -r ".execution.R_lite[0].rank") &&
	test_debug "echo allocated ranks: ${ranks}" &&
	{ test "${ranks}" = "0-1" || test "${ranks}" = "2-3"; }
'
test_expect_success 'cleanup two-node job' '
	flux cancel ${jobid} &&
	flux job wait-event --timeout=10 ${jobid} clean
'
test_expect_success 'three-node job is denied: cannot fit in one rack' '
	jobid=$(flux submit --nodes=3 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} exception &&
	flux job info ${jobid} eventlog | grep "rack-local"
'
test_expect_success 'jobs can backfill into different racks independently' '
	job0=$(flux submit --nodes=2 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${job0} alloc &&
	job1=$(flux submit --nodes=2 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${job1} alloc &&
	flux cancel ${job0} ${job1} &&
	flux job wait-event --timeout=10 ${job1} clean
'
test_expect_success 'two single-node jobs share a rack when the other is full' '
	job0=$(flux submit --nodes=2 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${job0} alloc &&
	job0_rack_id=$(flux job info ${job0} R | jq -r ".scheduling.racks[0].id") &&
	job1=$(flux submit --nodes=1 --exclusive hostname) &&
	job2=$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${job1} alloc &&
	flux job wait-event --timeout=10 ${job2} alloc &&
	job1_rack_id=$(flux job info ${job1} R | jq -r ".scheduling.racks[0].id") &&
	job2_rack_id=$(flux job info ${job2} R | jq -r ".scheduling.racks[0].id") &&
	test_debug "echo job0_rack_id: ${job0_rack_id}, job1_rack_id: ${job1_rack_id}, job2_rack_id: ${job2_rack_id}" &&
	test "${job1_rack_id}" = "${job2_rack_id}" &&
	test "${job1_rack_id}" != "${job0_rack_id}" &&
	flux cancel ${job0} ${job1} ${job2} &&
	flux job wait-event --timeout=10 ${job2} clean
'
test_expect_success 'rack_exclusive job allocates entire rack' '
	jobid=$(flux submit --nodes=1 --exclusive \
	    --setattr=system.rack_exclusive=true hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc &&
	flux job info ${jobid} R >rack_excl.R &&
	jq -e ".scheduling.racks | length == 1" rack_excl.R &&
	rack_ranks=$(jq -r ".scheduling.racks[0].ranks" rack_excl.R) &&
	test_debug "echo rack_exclusive allocated ranks: ${rack_ranks}" &&
	{ test "${rack_ranks}" = "0-1" || test "${rack_ranks}" = "2-3"; }
'
test_expect_success 'regular job runs on remaining rack while rack_exclusive job is running' '
	job1=$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${job1} alloc &&
	job1_ranks=$(flux job info ${job1} R | jq -r ".execution.R_lite[0].rank") &&
	test_debug "echo regular job allocated ranks: ${job1_ranks}" &&
	test "${job1_ranks}" != "${rack_ranks}"
'
test_expect_success 'cleanup rack_exclusive and regular jobs' '
	flux cancel ${jobid} ${job1} &&
	flux job wait-event --timeout=10 ${jobid} clean &&
	flux job wait-event --timeout=10 ${job1} clean
'
test_expect_success 'unload sched-simple' '
	flux module remove sched-simple
'

# Test the pool_class class-attribute path in Scheduler._make_pool.
# sched-rack-subclass.py is a Scheduler subclass with pool_class = RackPool
# set as a class attribute, so no pool-class= argument or scheduling.writer
# is required.
RACK_SUBCLASS="${SHARNESS_TEST_SRCDIR}/scheduler/sched-rack-subclass.py"

test_expect_success 'load sched-rack-subclass (pool_class class attribute)' '
	flux module load ${RACK_SUBCLASS}
'
test_expect_success 'pool_class attr: single-node job allocates successfully' '
	jobid=$(flux submit --nodes=1 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc &&
	flux job info ${jobid} R | jq -e ".scheduling.racks | length == 1" &&
	flux cancel ${jobid} &&
	flux job wait-event --timeout=10 ${jobid} clean
'
test_expect_success 'pool_class attr: two-node job allocates within one rack' '
	jobid=$(flux submit --nodes=2 --exclusive hostname) &&
	flux job wait-event --timeout=10 ${jobid} alloc &&
	ranks=$(flux job info ${jobid} R | jq -r ".execution.R_lite[0].rank") &&
	{ test "${ranks}" = "0-1" || test "${ranks}" = "2-3"; } &&
	flux cancel ${jobid} &&
	flux job wait-event --timeout=10 ${jobid} clean
'
test_expect_success 'unload sched-rack-subclass' '
	flux module remove sched-rack-subclass
'

test_done
