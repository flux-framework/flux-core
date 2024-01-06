#!/bin/sh -e

# internal KVS getroot request does not cache namespace with correct name

TEST=issue5657

cat <<-EOF >t5657test.sh
#!/bin/sh -e

flux kvs namespace create issue5657ns

flux kvs put --namespace=issue5657ns a=1
flux kvs link -T issue5657ns a link2a

# double check getting link2a treeobj on rank 1 works
flux exec -r 1 flux kvs get --treeobj link2a

# before fix, next line would hang because symlink would not cache correct
# namespace name, leading to internal infinite loop

flux exec -r 1 flux kvs get link2a

EOF

chmod +x t5657test.sh

flux start -s 2 ./t5657test.sh
