/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "top.h"

struct keys {
    struct top *top;
    flux_watcher_t *w;
};

static void keys_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct keys *keys = arg;
    int c;

    switch ((c = getch())) {
        case 'q':
            flux_reactor_stop (r);
            break;
        case 'j':
        case KEY_DOWN:
            joblist_pane_set_current (keys->top->joblist_pane, true);
            break;
        case 'k':
        case KEY_UP:
            joblist_pane_set_current (keys->top->joblist_pane, false);
            break;
        case 'h':
        case KEY_LEFT:
            queues_prev (keys->top->queues);
            summary_pane_query (keys->top->summary_pane);
            summary_pane_draw (keys->top->summary_pane);
            joblist_filter_jobs (keys->top->joblist_pane);
            joblist_pane_draw (keys->top->joblist_pane);
            break;
        case 'l':
        case KEY_RIGHT:
            queues_next (keys->top->queues);
            summary_pane_query (keys->top->summary_pane);
            summary_pane_draw (keys->top->summary_pane);
            joblist_filter_jobs (keys->top->joblist_pane);
            joblist_pane_draw (keys->top->joblist_pane);
            break;
        case '\n':
        case KEY_ENTER:
            joblist_pane_enter (keys->top->joblist_pane);
            break;
        case 'd':
            summary_pane_toggle_details (keys->top->summary_pane);
            break;
        case '':
            clear ();
            summary_pane_draw (keys->top->summary_pane);
            joblist_pane_draw (keys->top->joblist_pane);
            break;
    }
}

struct keys *keys_create (struct top *top)
{
    struct keys *keys;

    if (!(keys = calloc (1, sizeof (*keys))))
        fatal (errno, "error creating context for key handling");
    if (!(keys->w = flux_fd_watcher_create (flux_get_reactor (top->h),
                                            STDIN_FILENO,
                                            FLUX_POLLIN,
                                            keys_cb,
                                            keys)))
        fatal (errno, "error creating fd watcher for stdin");
    keys->top = top;

    cbreak ();
    noecho ();
    intrflush (stdscr, FALSE);
    keypad (stdscr, TRUE);

    flux_watcher_start (keys->w);
    return keys;
}

void keys_destroy (struct keys *keys)
{
    if (keys) {
        int saved_errno = errno;
        flux_watcher_destroy (keys->w);
        free (keys);
        errno = saved_errno;
    }
}

// vi:ts=4 sw=4 expandtab
