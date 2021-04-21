#!/bin/sh

test_description='Test flux job list inactive cache size'

. $(dirname $0)/sharness.sh

export FLUX_CONF_DIR=$(pwd)
test_under_flux 4 job

fj_wait_event() {
  flux job wait-event --timeout=20 "$@"
}

test_expect_success 'job-list: generate jobspec for tests' '
        flux jobspec --format json srun -N1 hostname > hostname.json
'

test_expect_success 'job-list: unload module' '
        flux module unload job-list
'

test_expect_success 'job-list: setup config file, cache size = 4' '
        cat >job-list.toml <<EOF &&
[job-list]
inactive-cache-size = 4
EOF
        flux config reload
'

test_expect_success 'job-list: load module' '
        flux module load job-list
'

test_expect_success 'job-list: submit 6 jobs' '
        for i in $(seq 1 6); do \
                flux job submit hostname.json >> inactiveids; \
                fj_wait_event `tail -n 1 inactiveids` clean ; \
        done &&
        tac inactiveids | flux job id > inactive.ids
'

test_expect_success HAVE_JQ 'flux job list inactive jobs are truncated with cache size 4' '
        flux job list -s inactive | jq .id > list_inactive_truncated1.out &&
        head -n 4 inactive.ids > inactive_truncated1.ids &&
        test_cmp list_inactive_truncated1.out inactive_truncated1.ids
'

test_expect_success 'job-list: submit 2 more jobs' '
        for i in $(seq 1 2); do \
                flux job submit hostname.json >> inactiveids; \
                fj_wait_event `tail -n 1 inactiveids` clean ; \
        done &&
        tac inactiveids | flux job id > inactive.ids
'

test_expect_success HAVE_JQ 'flux job list inactive jobs updated and truncated with cache size 4' '
        flux job list -s inactive | jq .id > list_inactive_truncated2.out &&
        head -n 4 inactive.ids > inactive_truncated2.ids &&
        test_cmp list_inactive_truncated2.out inactive_truncated2.ids
'

test_expect_success 'job-list: unload module' '
        flux module unload job-list
'

test_expect_success 'job-list: setup config file, cache size = 5' '
        cat >job-list.toml <<EOF &&
[job-list]
inactive-cache-size = 5
EOF
        flux config reload
'

test_expect_success 'job-list: load module' '
        flux module load job-list
'

test_expect_success HAVE_JQ 'flux job list inactive jobs are truncated with cache size 5' '
        flux job list -s inactive | jq .id > list_inactive_truncated3.out &&
        head -n 5 inactive.ids > inactive_truncated3.ids &&
        test_cmp list_inactive_truncated3.out inactive_truncated3.ids
'

test_expect_success 'job-list: unload module' '
        flux module unload job-list
'

test_expect_success 'job-list: setup config file, cache size = 0' '
        cat >job-list.toml <<EOF &&
[job-list]
inactive-cache-size = 0
EOF
        flux config reload
'

test_expect_success 'job-list: load module' '
        flux module load job-list
'

test_expect_success HAVE_JQ 'flux job list no inactive jobs listed with cache size 0' '
        flux job list -s inactive | jq .id > list_inactive_truncated4.out &&
        touch inactive_truncated4.ids &&
        test_cmp list_inactive_truncated4.out inactive_truncated4.ids
'

test_expect_success 'job-list: unload module' '
        flux module unload job-list
'

test_expect_success 'job-list: setup config file, cache size = -1 (unlimited)' '
        cat >job-list.toml <<EOF &&
[job-list]
inactive-cache-size = -1
EOF
        flux config reload
'

test_expect_success 'job-list: load module' '
        flux module load job-list
'

test_expect_success HAVE_JQ 'flux job list all inactive jobs with cache size = -1' '
        flux job list -s inactive | jq .id > list_inactive.out &&
        test_cmp list_inactive.out inactive.ids
'

test_done
