#!/bin/sh -e
# dropcache, do a put that misses a references, unlink a dir, the reference
# should not be missing.
#
# N.B. the reason this test is split across two flux starts is we need the
# internal KVS cache to be empty

cat <<-EOF >t1760setup.sh
#!/bin/sh -e

flux kvs mkdir foo

EOF

cat <<-EOF >t1760test.sh
#!/bin/sh -e

${FLUX_BUILD_DIR}/t/kvs/issue1760 foo

EOF

chmod +x t1760setup.sh
chmod +x t1760test.sh

STATEDIR=issue1760-statedir
mkdir issue1760-statedir

flux start -s 1 \
    --setattr=statedir=${STATEDIR} \
    ./t1760setup.sh

flux start -s 1 \
    --setattr=statedir=${STATEDIR} \
    ./t1760test.sh
