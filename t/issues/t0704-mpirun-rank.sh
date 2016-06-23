#!/bin/sh -e
# #704: MPIRUN_RANK != FLUX_TASK_RANK on nodeid > 0
flux wreckrun -N4 -n4 sh -c 'test $MPIRUN_RANK = $FLUX_TASK_RANK'
