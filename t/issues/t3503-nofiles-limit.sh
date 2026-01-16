#!/bin/sh
#
#  Ensure job-exec doesn't segfault when broker runs over nofile ulimit
#
#  Note: the file descriptor limit and number of jobs in the test were
#   chosen somewhat arbitrarily. The fd limit must be low, but not so
#   low that `flux start` or other essential services fail to initialize.
#   The number of jobs should be chosen such that some of them will
#   definitely cause the broker to run over the artificially lowered
#   fd limit.
#
ulimit -n 167
ulimit -Hn 167
flux start \
    sh -c '
flux submit --cc=1-12 hostname &&
flux queue drain &&
flux jobs -a
'
