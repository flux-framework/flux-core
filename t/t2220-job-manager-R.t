#!/bin/sh
test_description='Test job manager internal copy of R'

. $(dirname $0)/sharness.sh

test_under_flux 1 full -Slog-stderr-level=1

# Usage: job_manager_getattr ID ATTR
job_manager_getattr() {
    flux python -c "import flux; print(flux.Flux().rpc(\"job-manager.getattr\",{\"id\":$1,\"attrs\":[\"$2\"]}).get_str())"
}

# Diff two json files
# https://stackoverflow.com/questions/31930041/using-jq-or-alternative-command-line-tools-to-compare-json-files
json_diff() {
	jq --sort-keys . $1 >difftmp1 &&
	jq --sort-keys . $2 >difftmp2 &&
	diff difftmp1 difftmp2
}

test_expect_success 'submit job' '
	flux submit -t 10s --wait-event=clean true | flux job id >jobid
'
test_expect_success 'job-manager getattr of R works' '
	job_manager_getattr $(cat jobid) R  >R.out
'
test_expect_success 'R contains expiration' '
	jq -e .R.execution.expiration R.out
'
test_expect_success 'reload job manager' '
	flux module remove job-list &&
	flux module remove sched-simple &&
	flux module reload job-manager &&
	flux module load sched-simple &&
	flux module load job-list
'
test_expect_success 'job-manager getattr of R still works' '
	job_manager_getattr $(cat jobid) R  >R2.out
'
test_expect_success 'R is unchanged after restart' '
	json_diff R.out R2.out
'
test_done
