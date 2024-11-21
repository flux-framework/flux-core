#!/bin/sh -e

# How this test works
#
# add some unique data to the content cache, we do several stores to
# build up a decent length internal list of flushable cache entries.
#
# write some of the same data again, if error present internal flush
# list will be messed up and length of flush list < number of dirty
# entries (acct_dirty).
#
# before fix, flux content flush will hang b/c number of dirty entries
# (acct_dirty) never reaches zero.

cat <<-EOF >t4482.sh
#!/bin/sh -e

flux module load content

echo "abcde" | flux content store
echo "fghij" | flux content store
echo "klmno" | flux content store
echo "pqrst" | flux content store
echo "tuvwx" | flux content store

echo "fghij" | flux content store
echo "klmno" | flux content store

flux module load content-sqlite

flux content flush

flux module remove content-sqlite
flux module remove content

EOF

chmod +x t4482.sh

flux start -s 1 \
    --setattr=broker.rc1_path= \
    --setattr=broker.rc3_path= \
    ./t4482.sh
