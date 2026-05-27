#!/bin/sh

#
# some commands such as 'ps' or 'dd' have shown to have problems when
# run under asan.  This simple wrapper unsets LD_PRELOAD and
# ASAN_OPTIONS so that asan is disabled when running the command.
#

unset LD_PRELOAD
unset ASAN_OPTIONS

"$@"
