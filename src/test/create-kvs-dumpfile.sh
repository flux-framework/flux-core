#!/usr/bin/env bash
#
#  Run a workload in the docker container from a given flux-core tag
#   (previous tag by default), and preserve a dumpfile.
#
OUTPUTDIR="$(pwd)/output"
TAG=$(git describe --abbrev=0)
WORKLOAD="\
#!/bin/sh
#
#  Basic workload used to create a set of varied eventlogs in KVS
#
flux bulksubmit --quiet --cc=1-4 {} ::: true false nocommand &&
id=\$(flux submit --urgency=hold hostname) &&
flux cancel \$id &&
id=\$(flux submit --wait-event=start sleep 30) &&
flux submit --quiet --dependency=afternotok:\$id true &&
flux cancel \$id &&
flux queue drain &&
flux jobs -a
"
declare -r prog=${0##*/}
die() { echo -e "$prog: $@"; exit 1; }

declare -r long_opts="help,tag:,workload:,statedir:"
declare -r short_opts="ht:w:d:"
declare -r usage="
Usage: $prog [OPTIONS]\n\
Run a workload under the latest flux-core tag docker image and save\n\
the content file to a state directory.\n\
\n\
Options:\n\
 -h, --help             Display this message\n\
 -t, --tag=NAME         Run against flux-core tag NAME instead of latest tag\n\
 -w, --workload=SCRIPT  Run workload SCRIPT instead of a default\n\
 -d, --output=DIR       Save dumpfile to DIR (default=$OUTPUTDIR)
"

# check if running in OSX
if [[ "$(uname)" == "Darwin" ]]; then
    # BSD getopt
    GETOPTS=`/usr/bin/getopt $short_opts -- $*`
else
    # GNU getopt
    GETOPTS=`/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@`
    if [[ $? != 0 ]]; then
        die "$usage"
    fi
    eval set -- "$GETOPTS"
fi

while true; do
    case "$1" in
      -h|--help)     echo -ne "$usage";          exit 0  ;;
      -t|--tag)      TAG="$2";                   shift 2 ;;
      -w|--workload) WORKLOAD_PATH="$2";         shift 2 ;;
      -d|--output)   OUTPUTDIR="$2";             shift 2 ;;
      --)            shift; break;                       ;;
      *)             die "Invalid option '$1'\n$usage"   ;;
    esac
done

if test "${TAG}" = "$(git describe)"; then
    printf "Doing nothing by default since this commit is a tag\n"
    printf "To force: use --tag=TAG option\n"
    exit 0
fi

mkdir "${OUTPUTDIR}" || die "Failed to create $OUTPUTDIR"
if test -n "$WORKLOAD_PATH"; then
    cp $WORKLOAD_PATH ${OUTPUTDIR}/workload.sh
else
    printf "$WORKLOAD" > ${OUTPUTDIR}/workload.sh
fi

chmod +x ${OUTPUTDIR}/workload.sh

DUMPFILE=flux-${TAG}.tar.bz2
printf "Creating dumpfile in ${OUTPUTDIR}/${DUMPFILE}\n"
docker run -i --rm -u $(id -u) \
     --mount type=bind,source=${OUTPUTDIR},target=/data \
     fluxrm/flux-core:bionic-${TAG} \
     flux start sh -c "/data/workload.sh; flux dump /data/${DUMPFILE}"

rm ${OUTPUTDIR}/workload.sh

