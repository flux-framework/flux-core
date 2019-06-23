#!/bin/bash
#
#  Dummy "job shell" for job-exec module testing
#
#  Usage: dummy.sh JOBID
#
#  OPERATION
#
#  After reading JOBID on command line, the dummy job shell script
#  gathers the following variables for the job:
#
#  KVSDIR          Main KVS directory for this job
#  NAMESPACE       Guest KVS directory for this job
#  BROKER_RANK     Flux broker rank on which shell is executing
#  JOBSPEC         Jobspec for this job
#  R               Resource set for this job
#  NNODES          Total number of broker ranks (nodes) in this job
#  DURATION        Duration in seconds from jobspec or 0.01s by default
#  RANKLIST        Indexed array of broker ranks assigned this job
#  JOB_SHELL_RANK  Index of the current job shell within RANKLIST
#  COMMAND         Indexed array of the first specified command in 'tasks'
#                  section of jobspec
#
#  If any of the above information cannot be obtained, then the job
#  shell will exit with failure. Otherwise, the job shell will execute
#  a single copy of the commandline in COMMAND, which will have access
#  to all of the above global variables. Thus, COMMAND can be used to
#  drive tests of expected execution behavior.
#

die() { echo "dummy-shell: $@" >&2; exit 1; }

#
#  Declare globals:
#
declare -a RANKLIST
declare -a COMMAND
declare -i JOB_SHELL_RANK
declare -i NNODES
declare    DURATION

declare -r JOBID=$1
[[ -z "$JOBID" ]] && die "JOBID argument required"

#
#  Fetch read-only job information and verify all values found:
#
declare -r KVSDIR=$(flux job id --to=kvs $JOBID).guest
declare -r NAMESPACE=${KVSDIR}.guest
declare -r BROKER_RANK=$(flux getattr rank)
declare -r JOBSPEC=$(flux job info $JOBID jobspec)
declare -r R=$(flux job info $JOBID R)

for var in KVSDIR BROKER_RANK JOBSPEC R; do
    [[ -z "${!var}" ]] && die "Unable to determine ${var}!"
done

#
#  Functions:
#
get_ranklist() {
    while read line; do
        IFS=',' read -ra split <<<$line
        for entry in "${split[@]}"; do
            if [[ $entry =~ - ]]; then
                RANKLIST+=($(eval echo {${entry/-/..}}))
            else
                RANKLIST+=($entry)
            fi
        done
    done < <(echo "$R" | jq -r '.execution.R_lite[] | .rank')
}

get_job_shell_rank() {
    let i=0
    get_ranklist
    for r in "${RANKLIST[@]}"; do
        if [[ $r == $BROKER_RANK ]]; then
            JOB_SHELL_RANK=$r
            return 0
	fi
        ((i++))
    done
    die "My rank: $BROKER_RANK, not found in ranklist!"
}

get_nnodes() {
    NNODES=${#RANKLIST[@]}
}

json_get() { echo "$1" | jq -r $2; }

get_duration() {
    DURATION=$(json_get "$JOBSPEC" .attributes.system.duration)
    if test "$DURATION" = "null"; then DURATION=0.01; fi
}

get_command() {
    COMMAND=($(json_get "$JOBSPEC" '.tasks[0].command[]' | tr '\n' ' '))
}


#
#  Gather remaining job information:
#
get_job_shell_rank
get_nnodes
get_duration
get_command

#
#  Run specified COMMAND:
#
echo Running  "${COMMAND[@]}"
eval "${COMMAND[@]}"

