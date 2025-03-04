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
#  FLUX_KVS_NAMESPACE   Guest KVS namespace for this job
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

die() { echo "dummy-shell[$BROKER_RANK]: $@" >&2; exit 1; }
log() { echo "dummy-shell[$BROKER_RANK]: $@" >&2; }

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
declare -r BROKER_RANK=$(flux getattr rank)
declare -r JOBSPEC=$(flux job info --original $JOBID jobspec)
declare -r R=$(flux job info $JOBID R)

for var in FLUX_KVS_NAMESPACE BROKER_RANK JOBSPEC R; do
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
            JOB_SHELL_RANK=$i
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

get_cwd() {
    CWD=$(json_get "$JOBSPEC" .attributes.system.cwd)
    if test "$CWD" = "null"; then CWD=.; fi
}

get_command() {
    COMMAND=($(json_get "$JOBSPEC" '.tasks[0].command[]' | tr '\n' ' '))
}

get_traps() {
    trap "log got SIGTERM" 15
    TRAP=$(json_get "$JOBSPEC" '.attributes.system.environment.TRAP')
    if test "x$TRAP" != "xnull" ; then
        trap "log got signal $TRAP" $TRAP
    fi
}

test_mock_failure() {
    FAIL_MODE=$(json_get "$JOBSPEC" '.attributes.system.environment.FAIL_MODE')
    case "$FAIL_MODE" in
        before_barrier_entry)
            #
            #  Attempt to exit early from shell rank 1 *before* rank 0
            #   enters the shell barrier. This is best effort since there
            #   is not a good way to ensure a separate shell process has
            #   reached the barrier.
            #
            log "Got FAIL_MODE=$FAIL_MODE"
            if test $JOB_SHELL_RANK -eq 0; then
                sleep 1
            elif test $JOB_SHELL_RANK -eq 1; then
                log "before_barrier: exiting early on job shell rank 1"
                exit 1
            fi
            ;;
        after_barrier_entry)
            #
            #  Similar to above, but try to ensure that rank 0 shell has
            #   entered the barrier before rank 1 unexpectedly exits.
            #   See caveats about best effort above.
            #
            log "after_barrier_entry: rank=$JOB_SHELL_RANK"
            if test $JOB_SHELL_RANK -eq 1; then
                log "after_barrier: exiting early on job shell rank 1"
                sleep 1
                log "exiting"
                exit 1
            fi
            ;;
        *)
            ;;
    esac
}

barrier() {
    if [[ ${NNODES} -gt 1 ]]; then
        echo enter >&1
        read -u 0 line
        if [[ ${line##*=} -ne 0 ]]; then
            exit ${line##*=}
        fi
    fi
}

#
#  Gather remaining job information:
#
get_job_shell_rank
get_nnodes
barrier
get_duration
get_command
get_traps
get_cwd

cd $CWD

test_mock_failure
barrier

#
#  Run specified COMMAND:
#
log Running  "${COMMAND[@]}"
flux kvs eventlog append exec.eventlog shell.start
eval "${COMMAND[@]}"

# vi: ts=4 sw=4 expandtab
