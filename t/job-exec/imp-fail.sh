#!/bin/bash
#
#  Mock flux-imp that fails on broker rank 1
#
cmd=$1

# Test requirement, make sure we change back to sharness trash directory,
#  multiuser jobs do not cause IMP to chdir to cwd of job:
cd $SHARNESS_TRASH_DIRECTORY

case "$cmd" in
    exec)
        shift;
        printf "test-imp: Going to fail on rank 1\n" >&2
        if test $(flux getattr rank) = 1; then exit 0; fi
        exec "$@" ;;
    *)
        printf "test-imp: Fatal: Unknown cmd=$cmd\n" >&2; exit 1 ;;
esac

# vi: ts=4 sw=4 expandtab
