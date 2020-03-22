#!/bin/bash
cmd=$1
case "$cmd" in
    exec)
        shift; 
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
