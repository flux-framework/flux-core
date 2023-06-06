#!/bin/sh
#

# Return the expected jobids list in a given state:
#   "all", "pending", "running", "inactive", "active",
#   "completed", "canceled", "failed", "timeout"
#
job_list_state_ids() {
    for f in "$@"; do
        cat ${f}.ids
    done
}

# Return the expected count of jobs in a given state (See above for list)
#
job_list_state_count() {
    job_list_state_ids "$@" | wc -l
}
