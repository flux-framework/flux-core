#!/bin/bash
##############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

NNODES=$(flux resource list -no {nnodes})
NCORES=$(flux resource list -no {ncores})
CPN=$((${NCORES}/${NNODES}))

printf "MPI scale testing on ${LCSCHEDCLUSTER:-$(hostname)}\n"
printf "TIME: $(date -Is)\n"
printf "INFO: $(flux resource info)\n"
printf "TOPO: $(flux getattr tbon.topo)\n"
printf "\n"
printf "%6s %8s %14s %14s %14s %14s\n" NODES NTASKS INIT BARRIER FINALIZE TOTAL

seq2()
{
    local start=$1
    local end=$2
    local printend=1

    while [[ $start -lt $end ]]; do
        printf "$start\n"
        [[ $start = $end ]] && printend=0
        ((start*=2))
    done
    [[ $printend = 1 ]] && printf "$end\n"
}

flux bulksubmit --watch --progress --quiet --nodes={0} --tasks-per-node={1} \
    --exclusive \
    --env=FLUX_MPI_TEST_TIMING=t \
    ./t/mpi/hello \
    ::: $(seq2 1 ${NNODES}) \
    ::: $(seq2 1 ${CPN})

# vi: ts=4 sw=4 expandtab
