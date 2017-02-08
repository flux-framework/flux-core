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
#include <unistd.h>
#include <sys/mman.h>
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
    coproc_f cb;
    size_t ssize;
    size_t pagesize;
    uint8_t *stack;
    coproc_state_t state;
    int rc;
    void *arg;
};

static const size_t default_stack_size = 2*1024*1024; /* 2mb stack */

void coproc_destroy (coproc_t *c)
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
#if SIZEOF_UINTPTR_T == SIZEOF_INT
    coproc_t *c = (coproc_t *)(uintptr_t)(high);
#elif SIZEOF_UINTPTR_T == 8 && SIZEOF_INT == 4
    coproc_t *c = (coproc_t *)((((uintptr_t)high) << 32) | low);
#else
#error FIXME: unexpected pointer/integer size
#endif
    assert (c->magic == COPROC_MAGIC);

    c->rc = c->cb (c, c->arg);
    c->state = CS_RETURNED;

    (void)swapcontext (&c->uc, &c->parent);
}

/* return 'l' rounded up to a multiple of the system page size.
 */
static size_t compute_size (size_t l, size_t pagesize)
{
    return ((l + pagesize - 1) & ~(pagesize - 1));
}

coproc_t *coproc_create (coproc_f cb)
{
    coproc_t *c = xzmalloc (sizeof (*c));
    int errnum;

    c->magic = COPROC_MAGIC;
    if (getcontext (&c->uc) < 0)
        goto error;
    c->state = CS_INIT;
    c->pagesize = sysconf (_SC_PAGE_SIZE);
    c->ssize = compute_size (default_stack_size + 2*c->pagesize, c->pagesize);
    if ((errnum = posix_memalign ((void **)&c->stack, c->pagesize, c->ssize))) {
        errno = errnum;
        goto error;
    }
    if (mprotect (c->stack, c->pagesize, PROT_NONE) < 0
                || mprotect (c->stack + c->ssize - c->pagesize,
                                                   c->pagesize, PROT_NONE < 0))
        goto error;
    c->uc.uc_stack.ss_sp = c->stack + c->pagesize;
    c->uc.uc_stack.ss_size = c->ssize - 2*c->pagesize;

    c->cb = cb;

    return c;
error:
    coproc_destroy (c);
    return NULL;
}

int coproc_yield (coproc_t *c)
{
    assert (c->magic == COPROC_MAGIC);
    if (c->state != CS_RUNNING) {
        errno = EINVAL;
        return -1;
    }
    c->state = CS_YIELDED;
    return swapcontext (&c->uc, &c->parent);
}

int coproc_resume (coproc_t *c)
{
    assert (c->magic == COPROC_MAGIC);
    if (c->state != CS_YIELDED) {
        errno = EINVAL;
        return -1;
    }
    c->state = CS_RUNNING;
    return swapcontext (&c->parent, &c->uc);
}

int coproc_start (coproc_t *c, void *arg)
{
    assert (c->magic == COPROC_MAGIC);

    if (c->state != CS_INIT && c->state != CS_RETURNED) {
        errno = EINVAL;
        return -1;
    }

#if SIZEOF_UINTPTR_T == SIZEOF_INT
    makecontext (&c->uc, (void (*)(void))trampoline, 2,
                 (uintptr_t)c, 0);
#elif SIZEOF_UINTPTR_T == 8 && SIZEOF_INT == 4
    makecontext (&c->uc, (void (*)(void))trampoline, 2,
                ((uintptr_t)c) >> 32, ((uintptr_t)c) & 0xffffffff);
#else
#error FIXME: unexpected pointer/integer size
#endif
    c->arg = arg;
    c->state = CS_RUNNING;
    return swapcontext (&c->parent, &c->uc);
}

bool coproc_started (coproc_t *c)
{
    assert (c->magic == COPROC_MAGIC);
    return (c->state == CS_RUNNING || c->state == CS_YIELDED);
}

bool coproc_returned (coproc_t *c, int *rc)
{
    assert (c->magic == COPROC_MAGIC);
    if (rc && c->state == CS_RETURNED)
        *rc = c->rc;
    return (c->state == CS_RETURNED);
}

size_t coproc_get_stacksize (coproc_t *c)
{
    return c->ssize - 2*c->pagesize;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
