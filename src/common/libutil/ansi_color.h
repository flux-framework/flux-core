/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_ANSI_COLOR_H
#define _UTIL_ANSI_COLOR_H

#define ANSI_COLOR_RED        "\x1b[31m"
#define ANSI_COLOR_GREEN      "\x1b[32m"
#define ANSI_COLOR_YELLOW     "\x1b[33m"
#define ANSI_COLOR_BLUE       "\x1b[34m"
#define ANSI_COLOR_MAGENTA    "\x1b[35m"
#define ANSI_COLOR_CYAN       "\x1b[36m"
#define ANSI_COLOR_GRAY       "\x1b[37m"
#define ANSI_COLOR_DEFAULT    "\x1b[39m"
#define ANSI_COLOR_DARK_GRAY  "\x1b[90m"

#define ANSI_COLOR_BOLD_BLUE  "\x1b[01;34m"

#define ANSI_COLOR_RESET      "\x1b[0m"
#define ANSI_COLOR_BOLD       "\x1b[1m"
#define ANSI_COLOR_HALFBRIGHT "\x1b[2m"
#define ANSI_COLOR_REVERSE    "\x1b[7m"

#define ANSI_COLOR_RESET      "\x1b[0m"

#endif /* !_UTIL_ANSI_COLOR_H */

// vi:ts=4 sw=4 expandtab
