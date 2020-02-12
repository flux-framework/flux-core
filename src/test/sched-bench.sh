#!/bin/bash
#
# Run a set of jobs (with or without exec subsystem loaded) and
#  print some timing stats gathered from job eventlogs.
#
declare prog=$(basename $0)

declare NNODES=8
declare CPN=32

declare -r long_opts="help,nnodes:,cores-per-node:,jobs:,noexec,sched-opts:verbose"
declare -r short_opts="hvN:c:j:o:"
declare -r usage="\
\n\
Usage: $prog [OPTIONS]\n\
Simple Flux sched/exec test benchmark.\n\
\n\
Options:\n\
 -h, --help              display this messages\n\
 -v, --verbose           run with extra verbose output\n\
 -N, --nnodes=NNODES     set simulated number of nodes (default=${NNODES})\n\
 -c, --cores-per-node=N  set simulated cores per node (default=${CPN})\n\
 -j, --jobs=NJOBS        set number of jobs to run (default nnodes*cpn)\n\
 -o, --sched-opts=OPTS   set scheduler module load options\n\
     --noexec            do not simulate execution, just scheduling\n"


log() { local fmt=$1; shift; printf >&2 "$prog: $fmt" "$@"; }
die() { log "$@" && exit 1; }

verbose() { test $VERBOSE = "t" && log "$@"; }

log_timing_msg() {
    local name=$1
    local start=$2
    local end=$3
    local elapsed=$(echo "$end - $start" | bc -l)
    local jps=$(echo "$NJOBS/$elapsed" | bc -l)
    log "$name $NJOBS jobs in %.3fs (%.2f job/s)\n" $elapsed $jps
}

GETOPTS=$(/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@)
if test $? != 0; then
    echo  "$usage"
    exit 1
fi

eval set -- "$GETOPTS"
while true; do
    case "$1" in
      -v|--verbose)         VERBOSE=t;  shift   ;;
      -N|--nnodes)          NNODES=$2;  shift 2 ;;
      -c|--cores-per-node)  CPN=$2;     shift 2 ;;
      -j|--jobs)            NJOBS=$2;   shift 2 ;;
      -o|--sched-opts)      OPTS="$2";  shift 2 ;;
      --noexec)             NOEXEC=t;   shift   ;;
      --)                   shift ; break ;     ;;
      -h|--help)            echo -e "$usage" ; exit 0           ;;
      *)                    die "Invalid option '$1'\n$usage"   ;;
    esac
done

# If not set, set number of jobs to nnodes * cores-per-node:
NJOBS=${NJOBS:-$((${NNODES}*${CPN}))}

log "On branch $(git rev-parse --abbrev-ref HEAD): $(git describe)\n"
log "starting with $NJOBS jobs across ${NNODES} nodes with ${CPN} cores/node.\n"
log "broker.pid=$(flux getattr broker.pid)\n"

#  Reload scheduler so we can insert a fake resource set:
flux module remove sched-simple
flux kvs put --json \
    resource.hwloc.by_rank="{\"[0-$(($NNODES-1))]\":{\"Core\":$CPN}}"
flux mini run --dry-run --setattr=system.exec.test.run_duration=.001s hostname \
    > job.json

log "Loading sched-simple: ${OPTS}\n"
flux module load sched-simple ${OPTS} || die "Failed to load sched-simple"

#  If not testing exec system, remove the job-exec module
test "$NOEXEC" = "t" && flux module remove job-exec

t_start=$(date +%s.%N)
t/ingest/submitbench -f 1024 -r $NJOBS job.json > job.list
t_ingest=$(date +%s.%N)
log_timing_msg ingested $t_start $t_ingest

test "$VERBOSE" = "t" && flux queue status -v

last=$(tail -1 job.list)
first=$(head -1 job.list)

starttime=$(flux job eventlog $first | awk '$2 == "submit" {print $1}')
alloctime=$(flux job wait-event $last alloc | awk '$2 == "alloc" {print $1}')
log_timing_msg allocated $starttime $alloctime

test "$VERBOSE" = "t" && flux queue status -v

if test -z "$NOEXEC"; then
    runtime=$(flux job wait-event $last clean | awk '{print $1}')
    log_timing_msg ran $starttime $runtime
fi

flux job cancelall -f
flux queue idle --quiet

test "$VERBOSE" = "t" && flux queue status -v

t_done=$(date +%s.%N)
log_timing_msg "total walltime for" $t_start $t_done

#  If not testing exec system, reinstall job-exec to avoid error from rc3
# test "$NOEXEC" = "t" && flux module load job-exec
# XXX: Not a good idea, starts running jobs just before script exit


# vi: ts=4 sw=4 expandtab
