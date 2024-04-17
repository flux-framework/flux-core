#!/bin/sh

# ensure epilog doesn't run on jobs canceled at shutdown

cat <<-EOF >t5892.toml
[job-manager]
plugins = [
  { load = "perilog.so" },
]

[job-manager.epilog]
command = [ "touch", "t5892-epilog-flag" ]
EOF

flux start -o,--config-path=t5892.toml \
	flux submit sleep inf

rc=0
if test -f t5892-epilog-flag; then
	echo The epilog did run, contrary to expectations.>&2
	rc=1
else
	echo The epilog did not run, as expected. >&2
fi

rm -f t5892-epilog-flag
rm -f t5892.toml

exit $rc
