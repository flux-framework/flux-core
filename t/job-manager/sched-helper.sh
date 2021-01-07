#!/bin/sh
#

# job-manager sched helper functions

JMGR_JOB_LIST=${FLUX_BUILD_DIR}/t/job-manager/list-jobs
JOB_CONV="flux python ${FLUX_SOURCE_DIR}/t/job-manager/job-conv.py"

# internal function to get job state via job manager
#
# if job is not found by list-jobs, but the clean event exists
# in the job's eventlog, return state as inactive
#
# use job-conf tool to convert state numeric value to string
# value.
#
# arg1 - jobid
_jmgr_get_state() {
        local id=$(flux job id $1)
        local state=$(${JMGR_JOB_LIST} \
                      | grep ${id} \
                      | jq .state \
                      | ${JOB_CONV} statetostr -s)
        test -z "$state" \
                && flux job wait-event --timeout=5 ${id} clean >/dev/null \
                && state=I
        echo $state
}

# verify if job is in specific state through job manager
#
# function will loop for up to 5 seconds in case state change is slow
#
# arg1 - jobid
# arg2 - single character expected state (e.g. R = running)
jmgr_check_state() {
        local id=$1
        local wantstate=$2
        for try in $(seq 1 10); do
                test $(_jmgr_get_state $id) = $wantstate && return 0
                sleep 0.5
        done
        return 1
}

# internal function to get job annotation key value via job manager
#
# arg1 - jobid
# arg2 - key
_jmgr_get_annotation() {
        local id=$(flux job id $1)
        local key=$2
        local note="$(${JMGR_JOB_LIST} | grep ${id} | jq .annotations | jq ."${key}")"
        echo $note
}

# verify if job contains specific annotation key & value through job manager
#
# function will loop for up to 5 seconds in case annotation update
# arrives slowly
#
# arg1 - jobid
# arg2 - key in annotation
# arg3 - value of key in annotation
#
# callers should set HAVE_JQ requirement
jmgr_check_annotation() {
        local id=$1
        local key=$2
        local value="$3"
        for try in $(seq 1 10); do
                test "$(_jmgr_get_annotation $id $key)" = "${value}" && return 0
                sleep 0.5
        done
        return 1
}

# verify if job contains specific annotation key through job manager
#
# arg1 - jobid
# arg2 - key in annotation
#
# callers should set HAVE_JQ requirement
jmgr_check_annotation_exists() {
        local id=$(flux job id $1)
        local key=$2
        ${JMGR_JOB_LIST} | grep ${id} | jq .annotations | jq -e ."${key}" > /dev/null
}

# verify that job contains no annotations through job manager
#
# arg1 - jobid
jmgr_check_no_annotations() {
        local id=$(flux job id $1)
        if ${JMGR_JOB_LIST} | grep ${id} > /dev/null
        then
            ${JMGR_JOB_LIST} | grep ${id} | jq -e .annotations > /dev/null && return 1
        fi
        return 0
}

# internal function to get job annotation key value via flux job list
#
# arg1 - jobid
# arg2 - key
_jinfo_get_annotation() {
        local id=$(flux job id $1)
        local key=$2
        local note="$(flux job list -A | grep ${id} | jq .annotations | jq ."${key}")"
        echo $note
}

# verify if annotation published to job-info
#
# function will loop for up to 5 seconds in case annotation update
# arrives slowly
#
# arg1 - jobid
# arg2 - key in annotation
# arg3 - value of key in annotation
#
# callers should set HAVE_JQ requirement
jinfo_check_annotation() {
        local id=$1
        local key=$2
        local value="$3"
        for try in $(seq 1 10); do
                test "$(_jinfo_get_annotation $id $key)" = "${value}" && return 0
                sleep 0.5
        done
        return 1
}

# verify that job contains no annotations via job-info
#
# arg1 - jobid
#
# callers should set HAVE_JQ requirement
jinfo_check_no_annotations() {
        local id=$(flux job id $1)
        flux job list -A | grep ${id} | jq -e .annotations > /dev/null && return 1
        return 0
}

# verify if job contains specific annotation key through job-info
#
# arg1 - jobid
# arg2 - key in annotation
#
# callers should set HAVE_JQ requirement
jinfo_check_annotation_exists() {
        local id=$(flux job id $1)
        local key=$2
        flux job list -A | grep ${id} | jq .annotations | jq -e ."${key}" > /dev/null
}

# verify if annotation seen via flux-jobs
#
# function will loop for up to 5 seconds in case annotation update
# arrives slowly
#
# arg1 - jobid
# arg2 - key in annotation
# arg3 - value of key in annotation
fjobs_check_annotation() {
        local id=$(flux job id $1)
        local key=$2
        local value="$3"
        for try in $(seq 1 10); do
                test "$(flux jobs -n --format={${key}} ${id})" = "${value}" && return 0
                sleep 0.5
        done
        return 1
}

# verify that flux-jobs see no annotations
#
# arg1 - jobid
fjobs_check_no_annotations() {
        local id=$1
        test -z $(flux jobs -n --format="{annotations}" ${id}) && return 0
        return 1
}

# verify flux-jobs sees annotation
#
# arg1 - jobid
# arg2 - key in annotation
fjobs_check_annotation_exists() {
        local id=$1
        local key=$2
        test -z $(flux jobs -n --format="{${key}}" ${id}) && return 1
        return 0
}
