#!/bin/bash
#
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
#

get_builtins() {
    local FLUX_BUILTINS

    builtin_path=$1

    for builtin in $builtin_path/*; do
        if [[ $builtin == *".c" && \
              $builtin != *"attr"* ]]; then
            builtin="${builtin%.*}"
            FLUX_BUILTINS+="${builtin##*/}:"
        fi
    done

    while read line; do
        if [[ $line == *"optparse_reg_subcommand"* \
              && $line == *"attr"* ]]; then
             line="${line##*_}"
            FLUX_BUILTINS+="${line%,*}:"
        fi
    done < $builtin_path/attr.c

    echo FLUX_BUILTINS="$FLUX_BUILTINS"
}

get_builtins $1
