/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "debugged.h"

int MPIR_being_debugged = 0;

void MPIR_Breakpoint ()
{

}

int get_mpir_being_debugged ()
{
    return MPIR_being_debugged;
}

void set_mpir_being_debugged (int v)
{
    MPIR_being_debugged = v;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
