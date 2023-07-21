#!/usr/bin/env bash
set -e

flux content flush

flux module remove content-sqlite

# create a dirty cache entry in the content-cache
flux kvs put issue4378=issue4378bug

flux module load content-sqlite

# Issue 4378 - without fix, this flux content flush would hang
flux content flush
