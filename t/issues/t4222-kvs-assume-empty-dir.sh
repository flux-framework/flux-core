#!/bin/bash -e

flux module remove kvs-watch
flux module remove kvs

flux dump --checkpoint issue4222.tar

flux content flush
flux content dropcache
flux module remove content-sqlite

sqlitepath=$(flux getattr statedir)/content.sqlite
mv $sqlitepath $sqlitepath.bak

flux module load content-sqlite

flux restore --checkpoint issue4222.tar

flux module load kvs
flux module load kvs-watch

flux kvs namespace create issue4222ns
flux kvs put --namespace=issue4222ns a=1
