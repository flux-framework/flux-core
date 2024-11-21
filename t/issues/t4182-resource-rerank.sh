#!/bin/sh -e
#
#  Ensure resource module resets ranks of R to match hostlist attribute
#

#  Do what flux start --test-hosts does, but using FLUX_TASK_RANK
#   to find fake hostname in provided hostlist
cat <<EOF2 >t4182-broker-wrapper.sh
#!/bin/bash
# Usage: t4182-broker-wrapper.sh hostlist ARGS ...

hostlist=\$1; shift
FLUX_FAKE_HOSTNAME=\$(flux hostlist --nth=\$FLUX_TASK_RANK "\$hostlist")
flux broker -Shostlist="\$hostlist" "\$@"
EOF2
chmod +x t4182-broker-wrapper.sh

cat <<EOF >t4182-test.sh
#!/bin/bash -e

R="\$((flux R encode -r 0 -c 0 -H foo0 && \
     flux R encode -r 1 -c 0-1 -H foo1 && \
     flux R encode -r 2-3 -c 0-3 -H 'foo[2-3]' \
    ) | flux R append)"

flux kvs put resource.R="\$R"
flux kvs get resource.R

flux module remove sched-simple
flux module reload resource monitor-force-up noverify
flux module load sched-simple

flux dmesg | grep 'sched-simple.*ready'  | tail -1

flux resource list

#  Disable rlist/hwloc resource verification
#    --test-hosts set FLUX_FAKE_HOSTNAME but hwloc doesn't know about that
echo "resource.noverify = true" >t4182-resource.toml

#  ensure R rerank failure is ignored (i.e. job completes successfully)
flux run -o per-resource.type=node -o cpu-affinity=off -n 11 \
	flux start --config-path=t4182-resource.toml \
	flux getattr hostlist

#  ensure R is reranked based on hostlist attribute:
flux run -o per-resource.type=node -o cpu-affinity=off -n 11 \
	./t4182-broker-wrapper.sh "foo[3,2,1,0]" \
	--config-path=t4182-resource.toml \
	sh -c 'flux kvs get resource.R' >t4182-test.out

EOF

#  Use --test-hosts instead of setting hostlist attr so that overlay
#   hello check for expected hostname:rank won't fail and abort the test
flux start --test-hosts="foo[0-3]" --test-size=4 bash ./t4182-test.sh

jq -S . <t4182-test.out

NODELIST="$(jq -r .execution.nodelist[0] <t4182-test.out)"
EXPECTED="foo[3,2,1,0]"

if test "$NODELIST" = "$EXPECTED"; then
	exit 0
else
	echo >&2 "Got $NODELIST, expected $EXPECTED"
	exit 1
fi


