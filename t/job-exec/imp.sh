#!/bin/bash
cmd=$1
case "$cmd" in
    exec)
        shift; 
        #  Copy IMP's input to a file so tests can check result:
        cat - >imp-$(flux job id $2).input
        printf "test-imp: Running $* \n" >&2
        exec "$@" ;;
    kill)
        signal=$2;
        pid=$3;
        shift 3;
        printf "test-imp: Kill pid $pid signal $signal\n" >&2
        ps -fp $pid >&2
        kill -$signal $pid ;;
    *)
        printf "test-imp: Fatal: Unknown cmd=$cmd\n" >&2; exit 1 ;;
esac

# vi: ts=4 sw=4 expandtab
