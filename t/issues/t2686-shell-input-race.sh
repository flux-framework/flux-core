#!/bin/sh -e
#
#  Multi-rank jobs race with creation of guest.input eventlog creation
#

SUBMITBENCH=${SHARNESS_TEST_DIRECTORY}/ingest/submitbench

cat <<EOF >test.sh
#!/usr/bin/env bash

RC=0
RANKS="[0-\$((\$(flux getattr size)-1))]"

flux kvs put resource.R="\$(flux R encode -r\$RANKS -c0-15)"
flux kvs get resource.R

flux module remove sched-simple
flux module reload resource monitor-force-up noverify
flux module load sched-simple

flux dmesg | grep 'sched-simple.*ready'  | tail -1

flux submit --dry-run -o cpu-affinity=off -N2 -n2 sleep 0 \
    | ${SUBMITBENCH} -r 24 - >jobs.list

cat jobs.list

for id in \$(cat jobs.list); do
    if ! flux job attach \$id >/dev/null 2>&1; then
        printf "job %s\n" \$id
        flux job eventlog -p guest.output \$id
        RC=1
    fi
done

exit \$RC
EOF

flux start -s 4 sh ./test.sh
