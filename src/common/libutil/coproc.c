/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* See issue #126 for portability and performance drawbacks of
 * {get,put,make,swap}context and two alternative approaches.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <ucontext.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "coproc.h"
#include "xzmalloc.h"
#include "log.h"

typedef enum {
    CS_INIT,
    CS_RUNNING,
    CS_YIELDED,
    CS_RETURNED,
} coproc_state_t;

#define COPROC_MAGIC 0x0103ea02
struct coproc_struct {
    int magic;
    ucontext_t parent;
    ucontext_t uc;
    coproc_cb_t cb;
    size_t ssize;
    uint8_t *stack;
    coproc_state_t state;
    int rc;
    void *arg;
};

static const size_t default_stack_size = SIGSTKSZ*2;
static const size_t redzone = 16;
static const uint8_t redzone_pattern = 0x66;

void coproc_destroy (coproc_t c)
{
    if (c) {
        assert (c->magic == COPROC_MAGIC);
        if (c->stack)
            free (c->stack);
        c->magic = ~COPROC_MAGIC;
        free (c);
    }
}

static void trampoline (const unsigned int high, const unsigned int low)
{
    coproc_t c = (coproc_t)((((uintptr_t)high) << 32) | low);
    assert (c->magic == COPROC_MAGIC);

    c->rc = c->cb (c, c->arg);
    c->state = CS_RETURNED;

    swapcontext (&c->uc, &c->parent);
}

coproc_t coproc_create (coproc_cb_t cb)
{
    coproc_t c = xzmalloc (sizeof (*c));

    c->magic = COPROC_MAGIC;
    if (getcontext (&c->uc) < 0) {
        coproc_destroy (c);
        return NULL;
    }
    c->state = CS_INIT;
    c->ssize = default_stack_size;
    assert (c->ssize > 2*redzone);
    if (!(c->stack = malloc (c->ssize)))
        oom ();
    memset (c->stack, redzone_pattern, c->ssize);
    c->uc.uc_stack.ss_sp = c->stack + redzone;
    c->uc.uc_stack.ss_size = c->ssize - 2*redzone;

    c->cb = cb;

    return c;
}

int coproc_yield (coproc_t c)
{
    assert (c->magic == COPROC_MAGIC);
    if (c->state != CS_RUNNING) {
        errno = EINVAL;
        return -1;
    }
    c->state = CS_YIELDED;
    swapcontext (&c->uc, &c->parent);
    return 0;
}

static int verify_redzone (coproc_t c)
{
    int i;
    for (i = 0; i < redzone; i++) {
        if (c->stack[i] != redzone_pattern
         || c->stack[c->ssize - 1 - i] != redzone_pattern) {
            errno = EINVAL; /* FIXME need appropriate errno value */
            return -1;
        }
    }
    return 0;
}

int coproc_resume (coproc_t c)
{
    assert (c->magic == COPROC_MAGIC);
    if (c->state != CS_YIELDED) {
        errno = EINVAL;
        return -1;
    }
    c->state = CS_RUNNING;
    if (swapcontext (&c->parent, &c->uc) < 0)
        return -1;
    if (verify_redzone (c) < 0)
        return -1;
    return 0;
}

int coproc_start (coproc_t c, void *arg)
{
    assert (c->magic == COPROC_MAGIC);

    if (c->state != CS_INIT && c->state != CS_RETURNED) {
        errno = EINVAL;
        return -1;
    }

    const unsigned int high = ((uintptr_t)c) >> 32;
    const unsigned int low = ((uintptr_t)c) & 0xffffffff;
    makecontext (&c->uc, (void (*)(void))trampoline, 2, high, low);

    c->arg = arg;
    c->state = CS_RUNNING;
    if (swapcontext (&c->parent, &c->uc) < 0)
        return -1;
    if (verify_redzone (c) < 0)
        return -1;
    return 0;
}

bool coproc_started (coproc_t c)
{
    assert (c->magic == COPROC_MAGIC);
    if (c->state == CS_RUNNING || c->state == CS_YIELDED)
        return true;
    return false;
}

bool coproc_returned (coproc_t c, int *rc)
{
    assert (c->magic == COPROC_MAGIC);
    if (rc && c->state == CS_RETURNED)
        *rc = c->rc;
    return (c->state == CS_RETURNED);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
