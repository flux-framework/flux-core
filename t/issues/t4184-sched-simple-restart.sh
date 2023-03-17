#!/usr/bin/env bash
set -e

cat <<-EOF >t4184.sh
#!/bin/sh -e

which flux

NCORES=\$(flux resource list -s up -no {ncores})
jobids=\$(flux submit --cc=0-1 -n \$NCORES sleep 600)
id=\$(echo \$jobids | cut -d ' ' -f1)
id2=\$(echo \$jobids | cut -d ' ' -f2)
flux job wait-event \$id start

flux resource drain 0

flux module reload sched-simple

flux resource list
flux resource undrain 0

echo "t4184: waiting for resources to be back up:"
while test \$(flux resource list -s up -no {ncores}) -ne \$NCORES; do
  sleep 0.25
done

echo "t4184: canceling \$id"
flux cancel \${id}

echo "t4184: waiting for \$id to end"
flux job wait-event \$id clean

echo "t4184: waiting for \$id2 to start..."

flux job wait-event -t 15 \$id2 start

echo "t4184: canceling \$id2..."
flux cancel \$id2

echo "t4184: waiting for \$id2 to end..."
flux job wait-event -t 15 \$id2 clean

EOF

chmod +x t4184.sh

flux start -s 1 ./t4184.sh
