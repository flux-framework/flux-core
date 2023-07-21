#!/bin/sh

test_description='Tests for job-archive'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job

ARCHIVEDIR=`pwd`
ARCHIVEDB="${ARCHIVEDIR}/jobarchive.db"

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

# wait for job to be in specific state in job-list module
# arg1 - jobid
# arg2 - job state
wait_jobid_state() {
        local jobid=$(flux job id $1)
        local state=$2
        local i=0
        while ! flux job list --states=${state} | grep $jobid > /dev/null \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# wait for job to be stored in job archive
# arg1 - jobid
# arg2 - database path
wait_db() {
        local jobid=$(flux job id $1)
        local dbpath=$2
        local i=0
        query="select id from jobs;"
        while ! ${QUERYCMD} -t 100 ${dbpath} "${query}" | grep $jobid > /dev/null \
               && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        if [ "$i" -eq "50" ]
        then
            return 1
        fi
        return 0
}

# count number of entries in database
# arg1 - database path
db_count_entries() {
        local dbpath=$1
        query="select id from jobs;"
        count=`${QUERYCMD} -t 10000 ${dbpath} "${query}" | grep "^id =" | wc -l`
        echo $count
}

# verify entries stored into job archive
# arg1 - jobid
# arg2 - database path
db_check_entries() {
        local id=$(flux job id $1)
        local dbpath=$2
        query="select * from jobs where id=$id;"
        ${QUERYCMD} -t 10000 ${dbpath} "${query}" > query.out
        if grep -q "^id = " query.out \
            && grep -q "userid = " query.out \
            && grep -q "ranks = " query.out \
            && grep -q "t_submit = " query.out \
            && grep -q "t_run = " query.out \
            && grep -q "t_cleanup = " query.out \
            && grep -q "t_inactive = " query.out \
            && grep -q "eventlog = " query.out \
            && grep -q "jobspec = " query.out \
            && grep -q "R = " query.out
        then
            return 0
        fi
        return 1
}

# get job values from database
# arg1 - jobid
# arg2 - database path
get_db_values() {
        local id=$(flux job id $1)
        local dbpath=$2
        query="select * from jobs where id=$id;"
        ${QUERYCMD} -t 10000 ${dbpath} "${query}" > query.out
        userid=`grep "userid = " query.out | awk '{print \$3}'`
        ranks=`grep "ranks = " query.out | awk '{print \$3}'`
        t_submit=`grep "t_submit = " query.out | awk '{print \$3}'`
        t_run=`grep "t_run = " query.out | awk '{print \$3}'`
        t_cleanup=`grep "t_cleanup = " query.out | awk '{print \$3}'`
        t_inactive=`grep "t_inactive = " query.out | awk '{print \$3}'`
        eventlog=`grep "eventlog = " query.out | awk '{print \$3}'`
        jobspec=`grep "jobspec = " query.out | awk '{print \$3}'`
        R=`grep "R = " query.out | awk '{print \$3}'`
}

# check database values (job ran)
# arg1 - jobid
# arg2 - database path
db_check_values_run() {
        local id=$(flux job id $1)
        local dbpath=$2
        get_db_values $id $dbpath
        if [ -z "$userid" ] \
            || [ -z "$ranks" ] \
            || [ "$t_submit" == "0.0" ] \
            || [ "$t_run" == "0.0" ] \
            || [ "$t_cleanup" == "0.0" ] \
            || [ "$t_inactive" == "0.0" ] \
            || [ -z "$eventlog" ] \
            || [ -z "$jobspec" ] \
            || [ -z "$R" ]
        then
            return 1
        fi
        return 0
}

# check database values (job did not run)
# arg1 - jobid
# arg2 - database path
db_check_values_no_run() {
        local id=$(flux job id $1)
        local dbpath=$2
        get_db_values $id $dbpath
        if [ -z "$userid" ] \
            || [ -n "$ranks" ] \
            || [ "$t_submit" == "0.0" ] \
            || [ "$t_run" != "0.0" ] \
            || [ "$t_cleanup" == "0.0" ] \
            || [ "$t_inactive" == "0.0" ] \
            || [ -z "$eventlog" ] \
            || [ -z "$jobspec" ] \
            || [ -n "$R" ]
        then
            return 1
        fi
        return 0
}

test_expect_success 'job-archive: load module without specifying period, should fail' '
        test_must_fail flux module load job-archive
'

test_expect_success 'job-archive: setup config file' '
        cat >archive.toml <<EOF &&
[archive]
period = "0.5s"
dbpath = "${ARCHIVEDB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive: load module' '
        flux module load job-archive
'

test_expect_success 'job-archive: launch a running job' '
        jobid=`flux submit sleep inf` &&
        echo $jobid > long_running_job.id
'

test_expect_success 'job-archive: stores inactive job info (job good)' '
        jobid=`flux submit hostname` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVEDB} &&
        db_check_entries $jobid ${ARCHIVEDB} &&
        db_check_values_run $jobid ${ARCHIVEDB}
'

test_expect_success 'job-archive: stores inactive job info (job fail)' '
        jobid=`flux submit nosuchcommand` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVEDB} &&
        db_check_entries $jobid ${ARCHIVEDB} &&
        db_check_values_run $jobid ${ARCHIVEDB}
'

# ensure long running job wasn't stored
test_expect_success 'job-archive: check 2 jobs stored' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 2
'

test_expect_success 'job-archive: cancel long running job' '
        jobid=$(cat long_running_job.id) &&
        flux cancel $jobid &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVEDB} &&
        db_check_entries $jobid ${ARCHIVEDB} &&
        db_check_values_run $jobid ${ARCHIVEDB}
'

test_expect_success 'job-archive: check 3 jobs stored' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 3
'

# to ensure job canceled before we run, we submit a job to eat up all
# resources first.
test_expect_success 'job-archive: stores inactive job info (job cancel)' '
        jobid1=`flux submit -N4 -n8 sleep 500` &&
        fj_wait_event $jobid1 start &&
        jobid2=`flux submit hostname` &&
        fj_wait_event $jobid2 submit &&
        flux cancel $jobid2 &&
        fj_wait_event $jobid2 clean &&
        flux cancel $jobid1 &&
        fj_wait_event $jobid1 clean &&
        wait_jobid_state $jobid2 inactive &&
        wait_db $jobid2 ${ARCHIVEDB} &&
        db_check_entries $jobid2 ${ARCHIVEDB} &&
        db_check_values_no_run $jobid2 ${ARCHIVEDB}
'

test_expect_success 'job-archive: stores inactive job info (resources)' '
        jobid=`flux submit -N1000 -n1000 hostname` &&
        fj_wait_event $jobid clean &&
        wait_jobid_state $jobid inactive &&
        wait_db $jobid ${ARCHIVEDB} &&
        db_check_entries $jobid ${ARCHIVEDB} &&
        db_check_values_no_run $jobid ${ARCHIVEDB}
'

test_expect_success 'job-archive: all jobs stored' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 6
'

test_expect_success 'job-archive: reload module' '
        flux module reload job-archive
'

test_expect_success 'job-archive: doesnt restore old data' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 6
'

test_expect_success 'job-archive: stores more inactive job info' '
        jobid1=`flux submit hostname` &&
        jobid2=`flux submit hostname` &&
        fj_wait_event $jobid1 clean &&
        fj_wait_event $jobid2 clean &&
        wait_jobid_state $jobid1 inactive &&
        wait_jobid_state $jobid2 inactive &&
        wait_db $jobid1 ${ARCHIVEDB} &&
        wait_db $jobid2 ${ARCHIVEDB} &&
        db_check_entries $jobid1 ${ARCHIVEDB} &&
        db_check_entries $jobid2 ${ARCHIVEDB} &&
        db_check_values_run $jobid1 ${ARCHIVEDB} &&
        db_check_values_run $jobid2 ${ARCHIVEDB}
'

test_expect_success 'job-archive: all jobs stored' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 8
'

# we don't check values in module stats b/c it can be racy w/ polling
test_expect_success 'job-archive: get module stats' '
        flux module stats job-archive
'

test_expect_success 'flux module stats job-archive is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats job-archive >/dev/null
'

test_expect_success 'job-archive: unload module' '
        flux module unload job-archive
'

test_expect_success 'job-archive: db exists after module unloaded' '
        count=`db_count_entries ${ARCHIVEDB}` &&
        test $count -eq 8
'

test_expect_success 'job-archive: setup config file without dbpath' '
        cat >archive.toml <<EOF &&
[archive]
period = "0.5s"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive: load module failure, statedir not set' '
        test_must_fail flux module load job-archive
'

test_expect_success 'job-archive: setup config file without period' '
        cat >archive.toml <<EOF &&
[archive]
dbpath = "${ARCHIVEDB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive: load module failure, period not set' '
        test_must_fail flux module load job-archive
'

test_expect_success 'job-archive: setup config file with illegal period' '
        cat >archive.toml <<EOF &&
[archive]
period = "-10.5x"
dbpath = "${ARCHIVEDB}"
busytimeout = "0.1s"
EOF
	flux config reload
'

test_expect_success 'job-archive: load module failure, period illegal' '
        test_must_fail flux module load job-archive
'

test_expect_success 'job-archive: setup config file with only period' '
        cat >archive.toml <<EOF &&
[archive]
period = "0.5s"
EOF
	flux config reload
'

test_expect_success 'job-archive: launch flux with statedir set' '
        flux start -o,--setattr=statedir=$(pwd) true
'

test_expect_success 'job-archive: job-archive setup in statedir' '
        ls $(pwd)/job-archive.sqlite
'

test_done
