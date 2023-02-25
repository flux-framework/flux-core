#!/bin/sh -e
#
#  Ensure resource module resets ranks of R to match hostlist attribute
#
# test-prereqs: HAVE_JQ

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

#  ensure R rerank failure is ignored (i.e. job completes successfully)
flux run -o per-resource.type=node -o cpu-affinity=off -n 11 \
        flux start flux getattr hostlist

#  ensure R is reranked based on hostlist attribute:
flux run -o per-resource.type=node -o cpu-affinity=off -n 11 \
        flux broker --setattr hostlist="foo[3,2,1,0]" \
	sh -c 'flux kvs get resource.R' >t4182-test.out

EOF

flux start -o,-Shostlist=foo[0-3] --test-size=4 bash ./t4182-test.sh

jq -S . <t4182-test.out

NODELIST="$(jq -r .execution.nodelist[0] <t4182-test.out)"
EXPECTED="foo[3,2,1,0]"

if test "$NODELIST" = "$EXPECTED"; then
	exit 0
else
	echo >&2 "Got $NODELIST, expected $EXPECTED"
	exit 1
fi


