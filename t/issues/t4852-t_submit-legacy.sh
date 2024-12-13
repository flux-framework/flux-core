#!/bin/bash -e

# ensure t_submit/t_depend is available from job-list in older
# versions of flux that did not have the validate event.  To test, we
# remove the validate in an existing job's eventlog.

cat <<-EOF >t4852setup.sh
#!/bin/sh -e

jobid=\$(flux submit --wait true)

kvspath=\$(flux job id --to=kvs \$jobid)

flux kvs get \$kvspath.eventlog | grep -v validate > job4852.log

# need to remove version field in submit event for legacy eventlog format
# lazily "remove" it by just renaming it
head -n 1 job4852.log | sed -e "s/version/foobar/" > job4852.log2
tail -n +2 job4852.log >> job4852.log2

# head -n -1 to remove trailing newline
cat job4852.log2 | head -n -1 | flux kvs put -r \${kvspath}.eventlog=-

EOF

cat <<-EOF >t4852test.sh
#!/bin/sh -e

#assuming only one job in this test
flux jobs -a -no {id} > job4852.id
flux job list-ids \$(cat job4852.id) > job4852.out
grep t_submit job4852.out
grep t_depend job4852.out
cat job4852.out | jq -e ".t_submit == .t_depend"
EOF

chmod +x t4852setup.sh
chmod +x t4852test.sh

STATEDIR=issue4852-statedir
mkdir issue4852-statedir

flux start -s 1 \
    --setattr=statedir=${STATEDIR} \
    ./t4852setup.sh

flux start -s 1 \
    --setattr=statedir=${STATEDIR} \
    ./t4852test.sh
