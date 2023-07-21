#!/usr/bin/env bash
set -e

flux content flush

flux module remove content-sqlite

# we need to have a dirty entry in the content-cache for content-flush
# to mean anything.
flux kvs put issue4375=issue4375

! flux content flush

flux module load content-sqlite
