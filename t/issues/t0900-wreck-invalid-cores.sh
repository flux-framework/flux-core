#!/bin/sh
run_timeout() {
    perl -e 'alarm shift @ARGV; exec @ARGV' "$@"
}
# set one of the rank.X.cores to 0, invalid specification:
run_timeout 1 flux wreckrun -N2 -P 'lwj["rank.1.cores"] = 0' hostname
test $? = 1 && exit 0

exit 1
