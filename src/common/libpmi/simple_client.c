/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <stdbool.h>
#include "ccan/str/str.h"
#include <flux/taskmap.h>

#include "src/common/libutil/aux.h"
#include "src/common/libmissing/pipe2.h"

#include "simple_client.h"
#include "simple_server.h"
#include "keyval.h"
#include "pmi.h"

/* Thread safety: all wire I/O is under pmi->lock, which a request
 * holds for its round trip.  A group barrier must not hold it while
 * waiting, since another thread may need to send the request that
 * completes it.  So one waiter at a time is the reader: it polls the
 * fd without the lock, then reads one line with it.  Other waiters
 * sleep on pmi->cond.  A barrier_out read by a thread that is not its
 * owner is parked on pmi->pending, and the owner is woken to claim it.
 *
 * A barrier that fails after it was sent may leave its barrier_out
 * unread, or lose one read for another thread.  Either way replies can
 * no longer be matched to waiters, so the failure marks the client
 * failed and every barrier in flight or entered later fails too.
 */

/* The whole-job barrier has no tag on the wire.  Internally it is
 * matched with this one, which no group barrier can have.
 */
enum barrier_tag {
    BARRIER_TAG_NONE = -1,
};

/* A barrier in flight, owned by the thread waiting for it.
 */
struct barrier_wait {
    int tag;
    struct barrier_wait *next;
};

struct barrier_out {
    int tag;
    int rc;
    struct barrier_out *next;
};

/* Park a barrier_out for its owner and wake the waiters.  Call with
 * lock held.
 * On ENOMEM the owner would wait forever, so callers fail the client.
 * Every tag on the list was sent by a thread of this client that is
 * still waiting for it, since a waiter only gives up by failing the
 * client.  So the list is bounded by the number of barriers in flight
 * and is empty whenever no barrier is outstanding.
 */
static int pending_push (struct pmi_simple_client *pmi, int tag, int rc)
{
    struct barrier_out *b;

    if (!(b = calloc (1, sizeof (*b))))
        return -1;
    b->tag = tag;
    b->rc = rc;
    b->next = pmi->pending;
    pmi->pending = b;
    pthread_cond_broadcast (&pmi->cond);
    if (pmi->reader) {
        ssize_t n;
        do {
            n = write (pmi->wakefd[1], "", 1);
        } while (n < 0 && errno == EINTR);
    }
    return 0;
}

/* Mark the client failed and wake every barrier waiter so it can
 * notice.  Call with lock held.
 */
static void client_fail (struct pmi_simple_client *pmi)
{
    pmi->failed = true;
    pthread_cond_broadcast (&pmi->cond);
    if (pmi->reader) {
        ssize_t n;
        do {
            n = write (pmi->wakefd[1], "", 1);
        } while (n < 0 && errno == EINTR);
    }
}

/* Return true if a barrier with 'tag' is in flight.  Call with lock
 * held.
 */
static bool tag_waiting (struct pmi_simple_client *pmi, int tag)
{
    for (struct barrier_wait *w = pmi->waiting; w; w = w->next) {
        if (w->tag == tag)
            return true;
    }
    return false;
}

/* Return true if a barrier_out with 'rtag' answers a wait for 'tag'.
 * A group barrier is answered by its own tag.  The whole-job barrier is
 * answered by an untagged reply, or by a tagged one that answers no
 * group barrier in flight: some servers (Hydra) tag the reply to an
 * untagged request.  barrier_send() admits one barrier per tag at a
 * time, so that reply can only be the whole-job barrier's.
 */
static bool tag_matches (struct pmi_simple_client *pmi, int tag, int rtag)
{
    if (tag != BARRIER_TAG_NONE)
        return rtag == tag;
    return rtag == BARRIER_TAG_NONE || !tag_waiting (pmi, rtag);
}

/* Take a parked barrier_out answering 'tag', if any.  Call with lock
 * held.
 */
static bool pending_pop (struct pmi_simple_client *pmi, int tag, int *rcp)
{
    struct barrier_out **pp = &pmi->pending;

    while (*pp) {
        struct barrier_out *b = *pp;
        if (tag_matches (pmi, tag, b->tag)) {
            *pp = b->next;
            *rcp = b->rc;
            free (b);
            return true;
        }
        pp = &b->next;
    }
    return false;
}

/* Read one line from the fd into pmi->buf.  Buffering is done here
 * rather than in stdio so the fd can be polled.
 * Returns 0 on success, 1 if !block and no line is ready, -1 on error
 * or EOF.  Call with lock held.
 */
static int read_line (struct pmi_simple_client *pmi, bool block)
{
    for (;;) {
        char *nl = memchr (pmi->rbuf, '\n', pmi->rlen);
        if (nl) {
            size_t linelen = nl - pmi->rbuf + 1;
            if (linelen >= pmi->buflen) {
                errno = EPROTO;
                return -1;
            }
            memcpy (pmi->buf, pmi->rbuf, linelen);
            pmi->buf[linelen] = '\0';
            memmove (pmi->rbuf, nl + 1, pmi->rlen - linelen);
            pmi->rlen -= linelen;
            return 0;
        }
        if (pmi->rlen == pmi->rbufsize) {
            errno = EPROTO; // line longer than any valid response
            return -1;
        }
        if (!block) {
            struct pollfd pfd = { .fd = pmi->fd, .events = POLLIN };
            int n;
            do {
                n = poll (&pfd, 1, 0);
            } while (n < 0 && errno == EINTR);
            if (n < 0)
                return -1;
            if (n == 0)
                return 1;
        }
        ssize_t n = read (pmi->fd, pmi->rbuf + pmi->rlen, pmi->rbufsize - pmi->rlen);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1; // EOF
        pmi->rlen += n;
    }
}

/* Wait for the fd to become readable, or for a wake from a thread
 * that parked a barrier_out while this one was polling.  Only the
 * reader calls this, so only it drains the wake pipe.  Call with lock
 * released.
 */
static int wait_input (struct pmi_simple_client *pmi)
{
    struct pollfd pfd[2] = {
        { .fd = pmi->fd, .events = POLLIN },
        { .fd = pmi->wakefd[0], .events = POLLIN },
    };
    char buf[64];
    int n;

    do {
        n = poll (pfd, 2, -1);
    } while (n < 0 && errno == EINTR);
    if (n < 0 || (pfd[0].revents & (POLLERR | POLLNVAL)))
        return -1;
    if (pfd[1].revents & POLLIN) {
        while (read (pmi->wakefd[0], buf, sizeof (buf)) > 0)
            ;
    }
    return 0;
}

/* Read the response to a simple request, expecting 'cmd'.  A barrier_out
 * belongs to another thread's barrier: park it and read again.
 * Returns 0 on success, -1 on error.  Call with lock held.
 */
static int read_response (struct pmi_simple_client *pmi, const char *cmd)
{
    for (;;) {
        int tag;
        int rc;

        if (read_line (pmi, true) < 0)
            return -1;
        if (keyval_parse_isword (pmi->buf, "cmd", "barrier_out") == 0) {
            if (keyval_parse_int (pmi->buf, "tag", &tag) < 0)
                tag = BARRIER_TAG_NONE;
            if (keyval_parse_int (pmi->buf, "rc", &rc) < 0)
                rc = 0;
            if (pending_push (pmi, tag, rc) < 0) {
                client_fail (pmi); // the owner's reply is lost
                return -1;
            }
            continue;
        }
        return keyval_parse_isword (pmi->buf, "cmd", cmd) == 0 ? 0 : -1;
    }
}

/* Client sends exact required pmi_version and minimum required pmi_subversion.
 * Server sends client's pmi_version and maximum supported pmi_subversion.
 * See RFC 13 Version Handshake section for more detail.
 */
int pmi_simple_client_init (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    unsigned int vers, subvers;
    unsigned int kvsname_max, keylen_max, vallen_max;
    int rc;

    if (!pmi)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0)
        goto done;
    if (read_line (pmi, true) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "response_to_init") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (pmi->buf, "pmi_version", &vers) < 0
        || keyval_parse_uint (pmi->buf, "pmi_subversion", &subvers) < 0)
        goto done;
    if (vers != 1 || subvers < 1)
        goto done;

    if (fprintf (pmi->f, "cmd=get_maxes\n") < 0)
        goto done;
    if (read_line (pmi, true) < 0)
        goto done;
    if (keyval_parse_isword (pmi->buf, "cmd", "maxes") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_uint (pmi->buf, "kvsname_max", &kvsname_max) < 0
        || keyval_parse_uint (pmi->buf, "keylen_max", &keylen_max) < 0
        || keyval_parse_uint (pmi->buf, "vallen_max", &vallen_max) < 0)
        goto done;
    /* Size the line buffers for the largest possible response. */
    int buflen = keylen_max + vallen_max + kvsname_max + SIMPLE_MAX_PROTO_OVERHEAD;
    if (buflen > pmi->buflen) {
        char *buf;
        char *rbuf;
        if (!(buf = calloc (1, buflen)) || !(rbuf = calloc (1, buflen))) {
            free (buf);
            result = PMI_ERR_NOMEM;
            goto done;
        }
        memcpy (rbuf, pmi->rbuf, pmi->rlen);
        free (pmi->buf);
        free (pmi->rbuf);
        pmi->buf = buf;
        pmi->rbuf = rbuf;
        pmi->buflen = pmi->rbufsize = buflen;
    }
    pmi->version = vers;
    pmi->subversion = subvers;
    pmi->kvsname_max = kvsname_max;
    pmi->keylen_max = keylen_max;
    pmi->vallen_max = vallen_max;
    pmi->initialized = 1;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_finalize (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=finalize\n") < 0)
        goto done;
    if (read_response (pmi, "finalize_ack") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_get_appnum (struct pmi_simple_client *pmi, int *appnum)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!appnum)
        return PMI_ERR_INVALID_ARG;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_appnum\n") < 0)
        goto done;
    if (read_response (pmi, "appnum") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "appnum", appnum) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!universe_size)
        return PMI_ERR_INVALID_ARG;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_universe_size\n") < 0)
        goto done;
    if (read_response (pmi, "universe_size") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_int (pmi->buf, "size", universe_size) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

/* Send a barrier_in for 'group' (NULL for the untagged whole-job
 * barrier) and wait for the barrier_out answering 'tag' (see
 * tag_matches()), with the lock released while waiting so other
 * threads can send the requests that complete it.  Check the parked
 * list, then any line already buffered or ready on the fd.  If there is
 * none, become the reader and poll without the lock, or if another
 * thread is already the reader, sleep until it hands off or parks a
 * line.  A buffered line must be checked before polling, since one
 * read() can deliver several lines and leave the fd quiet.  A thread
 * that parks a line also wakes the reader, whose poll cannot otherwise
 * notice a line that another thread consumed.
 *
 * A barrier whose tag is already in flight on this client is refused
 * without sending, since the two replies could not be told apart.
 * A failure after sending fails the client (see client_fail()).
 * On success, set *rcp to the barrier rc and return 0.  Return -1 on
 * error.  Call with lock held.
 */
static int barrier_wait (struct pmi_simple_client *pmi,
                         const char *group,
                         int tag,
                         int *rcp)
{
    struct barrier_wait *wait;
    int result = -1;
    int n;

    if (pmi->failed || tag_waiting (pmi, tag))
        return -1;
    if (!(wait = calloc (1, sizeof (*wait))))
        return -1;
    if (group)
        n = fprintf (pmi->f, "cmd=barrier_in group=%s:%d\n", group, tag);
    else
        n = fprintf (pmi->f, "cmd=barrier_in\n");
    if (n < 0) {
        client_fail (pmi); // a partial line would corrupt the stream
        free (wait);
        return -1;
    }
    wait->tag = tag;
    wait->next = pmi->waiting;
    pmi->waiting = wait;
    for (;;) {
        int rtag;
        int rc;

        if (pending_pop (pmi, tag, rcp))
            break;
        if (pmi->failed)
            goto done;
        if ((n = read_line (pmi, false)) < 0)
            goto fail;
        if (n == 1) {
            if (pmi->reader) {
                pthread_cond_wait (&pmi->cond, &pmi->lock);
                continue;
            }
            pmi->reader = true;
            pthread_mutex_unlock (&pmi->lock);
            n = wait_input (pmi);
            pthread_mutex_lock (&pmi->lock);
            pmi->reader = false;
            pthread_cond_broadcast (&pmi->cond);
            if (n < 0)
                goto fail;
            continue;
        }
        if (keyval_parse_isword (pmi->buf, "cmd", "barrier_out") < 0)
            goto fail;
        if (keyval_parse_int (pmi->buf, "tag", &rtag) < 0)
            rtag = BARRIER_TAG_NONE;
        if (keyval_parse_int (pmi->buf, "rc", &rc) < 0)
            rc = 0;
        if (tag_matches (pmi, tag, rtag)) {
            *rcp = rc;
            break;
        }
        if (pending_push (pmi, rtag, rc) < 0)
            goto fail;
    }
    result = 0;
    goto done;
fail:
    client_fail (pmi);
done:
    for (struct barrier_wait **pp = &pmi->waiting; *pp; pp = &(*pp)->next) {
        if (*pp == wait) {
            *pp = wait->next;
            break;
        }
    }
    free (wait);
    return result;
}

/* The whole-job barrier is sent untagged against every server version,
 * as MPICH does.  A tagged WORLD group barrier is not equivalent: Hydra
 * files all WORLD barriers under one name regardless of tag.  Unlike
 * MPICH, the lock is not held while waiting, so another thread's group
 * barrier can be sent and complete meanwhile.
 */
int pmi_simple_client_barrier (struct pmi_simple_client *pmi)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    pthread_mutex_lock (&pmi->lock);
    if (barrier_wait (pmi, NULL, BARRIER_TAG_NONE, &rc) < 0)
        goto done;
    result = rc == 0 ? PMI_SUCCESS : rc;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_barrier_tag (const char *stringtag)
{
    uint32_t h = 2166136261U;
    int tag;

    if (!stringtag)
        return 0;
    for (const unsigned char *p = (const unsigned char *)stringtag; *p; p++) {
        h ^= *p;
        h *= 16777619U;
    }
    /* MPICH applies abs() to the int-cast hash.  Map h == 0x80000000
     * to 0 rather than negate INT_MIN, which is undefined.
     */
    if (h == 0x80000000u)
        return 0;
    tag = (int)h;
    return tag < 0 ? -tag : tag;
}

int pmi_simple_client_barrier_group (struct pmi_simple_client *pmi,
                                     const char *group,
                                     int tag)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!group || tag < 0)
        return PMI_ERR_INVALID_ARG;
    if (pmi->version != 1 || pmi->subversion < 2)
        return PMI_FAIL; // server does not support PMI-1.2 group barriers

    pthread_mutex_lock (&pmi->lock);
    if (barrier_wait (pmi, group, tag, &rc) < 0)
        goto done;
    result = rc == 0 ? PMI_SUCCESS : rc;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname,
                                       int length)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!kvsname || length <= 0)
        return PMI_ERR_INVALID_ARG;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get_my_kvsname\n") < 0)
        goto done;
    if (read_response (pmi, "my_kvsname") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_word (pmi->buf, "kvsname", kvsname, length) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_kvs_put (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               const char *value)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!kvsname || !key || !value)
        return PMI_ERR_INVALID_ARG;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=put kvsname=%s key=%s value=%s\n",
                 kvsname, key, value) < 0)
        goto done;
    if (read_response (pmi, "put_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

int pmi_simple_client_kvs_get (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               char *value,
                               int len)
{
    int result = PMI_FAIL;
    int rc;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!kvsname || !key || !value || len <= 0)
        return PMI_ERR_INVALID_ARG;
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f, "cmd=get kvsname=%s key=%s\n", kvsname, key) < 0)
        goto done;
    if (read_response (pmi, "get_result") < 0)
        goto done;
    if (keyval_parse_int (pmi->buf, "rc", &rc) == 0 && rc != 0) {
        result = rc;
        goto done;
    }
    if (keyval_parse_string (pmi->buf, "value", value, len) < 0)
        goto done;
    result = PMI_SUCCESS;
done:
    pthread_mutex_unlock (&pmi->lock);
    return result;
}

/* Helper for get_clique_size(), get_clique_ranks().
 * Fetch 'PMI_process_mapping' from the KVS and parse.
 * On success, a struct taskmap is stored in the pmi client aux hash
 *  and returned to the caller.
 */
static struct taskmap *fetch_taskmap (struct pmi_simple_client *pmi)
{
    struct taskmap *map = NULL;
    int result;
    char *nom;
    char *val;

    assert (pmi != NULL);
    assert (pmi->initialized);

    if ((map = pmi_simple_client_aux_get (pmi, "taskmap")))
        return map;

    nom = calloc (1, pmi->kvsname_max);
    val = calloc (1, pmi->vallen_max);
    if (!nom || !val) {
        result = PMI_ERR_NOMEM;
        goto done;
    }
    result = pmi_simple_client_kvs_get_my_name (pmi, nom, pmi->kvsname_max);
    if (result != PMI_SUCCESS)
        goto done;
    /*  First try flux.taskmap, falling back to PMI_process_mapping if it
     *  does not exist (e.g. if process manager is not Flux).
     */
    result = pmi_simple_client_kvs_get (pmi,
                                        nom,
                                        "flux.taskmap",
                                        val,
                                        pmi->vallen_max);
    if (result != PMI_SUCCESS) {
        result = pmi_simple_client_kvs_get (pmi,
                                            nom,
                                            "PMI_process_mapping",
                                            val,
                                            pmi->vallen_max);
        if (result != PMI_SUCCESS)
            goto done;
    }
    if (!(map = taskmap_decode (val, NULL))) {
        result = PMI_FAIL;
        goto done;
    }
    if (pmi_simple_client_aux_set (pmi,
                                   "taskmap",
                                   map,
                                   (flux_free_f) taskmap_destroy) < 0) {
        taskmap_destroy (map);
        map = NULL;
    }
done:
    free (nom);
    free (val);
    return map;
}

int pmi_simple_client_get_clique_size (struct pmi_simple_client *pmi,
                                       int *size)
{
    int nodeid = -1;
    struct taskmap *map;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!size)
        return PMI_ERR_INVALID_ARG;
    if (!(map = fetch_taskmap (pmi)) || taskmap_unknown (map)) {
        *size = 1;
        return PMI_SUCCESS;
    }
    if ((nodeid = taskmap_nodeid (map, pmi->rank)) < 0
        || (*size = taskmap_ntasks (map, nodeid)) < 0)
        return PMI_FAIL;
    return PMI_SUCCESS;
}

int pmi_simple_client_get_clique_ranks (struct pmi_simple_client *pmi,
                                        int ranks[],
                                        int length)
{
    struct taskmap *map;
    const struct idset *ids;
    int ntasks;
    int nodeid;
    unsigned int i;
    int index = 0;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (!ranks)
        return PMI_ERR_INVALID_ARG;
    map = fetch_taskmap (pmi);
    if (!map || taskmap_unknown (map)) {
        if (length != 1)
            return PMI_ERR_INVALID_SIZE;
        *ranks = pmi->rank;
        return PMI_SUCCESS;
    }
    if ((nodeid = taskmap_nodeid (map, pmi->rank)) < 0
        || (ntasks = taskmap_ntasks (map, nodeid)) < 0)
        return PMI_FAIL;
    if (ntasks > length)
        return PMI_ERR_INVALID_SIZE;
    if (!(ids = taskmap_taskids (map, nodeid)))
        return PMI_FAIL;
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        ranks[index++] = i;
        i = idset_next (ids, i);
    }
    return PMI_SUCCESS;
}

int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code,
                             const char *msg)
{
    int result = PMI_FAIL;
    char *cpy = NULL;

    if (!pmi || !pmi->initialized)
        return PMI_ERR_INIT;
    if (exit_code < 0)
        return PMI_ERR_INVALID_ARG;
    /* If message includes embedded \n's that would interfere with
     * the wire protocol, replace them with spaces.
     */
    if (strchr (msg, '\n')) {
        if (!(cpy = strdup (msg)))
            return PMI_ERR_NOMEM;
        for (char *cp = cpy; *cp != '\0'; cp++) {
            if (*cp == '\n')
                *cp = ' ';
        }
        msg = cpy;
    }
    pthread_mutex_lock (&pmi->lock);
    if (fprintf (pmi->f,
                 "cmd=abort exitcode=%d%s%s\n",
                 exit_code,
                 msg ? " error_msg=" : "",
                 msg ? msg : "") < 0) {
        pthread_mutex_unlock (&pmi->lock);
        goto done;
    }
    exit (exit_code);
    /* NOTREACHED */
done:
    free (cpy);
    return result;
}

void *pmi_simple_client_aux_get (struct pmi_simple_client *pmi,
                                 const char *name)
{
    if (!pmi) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (pmi->aux, name);
}

int pmi_simple_client_aux_set (struct pmi_simple_client *pmi,
                               const char *name,
                               void *aux,
                               flux_free_f destroy)
{
    if (!pmi) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&pmi->aux, name, aux, destroy);
}

void pmi_simple_client_destroy (struct pmi_simple_client *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        aux_destroy (&pmi->aux);
        if (pmi->f)
            (void)fclose (pmi->f);
        while (pmi->pending) {
            struct barrier_out *b = pmi->pending;
            pmi->pending = b->next;
            free (b);
        }
        (void)pthread_cond_destroy (&pmi->cond);
        (void)pthread_mutex_destroy (&pmi->lock);
        if (pmi->wakefd[0] >= 0)
            (void)close (pmi->wakefd[0]);
        if (pmi->wakefd[1] >= 0)
            (void)close (pmi->wakefd[1]);
        free (pmi->rbuf);
        free (pmi->buf);
        free (pmi);
        errno = saved_errno;
    }
}

struct pmi_simple_client *pmi_simple_client_create_fd (const char *pmi_fd,
                                                       const char *pmi_rank,
                                                       const char *pmi_size,
                                                       const char *pmi_spawned)
{
    struct pmi_simple_client *pmi;
    int fd = -1;

    if (!pmi_fd || !pmi_rank || !pmi_size) {
        errno = EINVAL;
        return NULL;
    }
    if (!(pmi = calloc (1, sizeof (*pmi))))
        return NULL;
    if (pthread_mutex_init (&pmi->lock, NULL) != 0) {
        free (pmi);
        return NULL;
    }
    if (pthread_cond_init (&pmi->cond, NULL) != 0) {
        (void)pthread_mutex_destroy (&pmi->lock);
        free (pmi);
        return NULL;
    }
    pmi->wakefd[0] = pmi->wakefd[1] = -1;
    errno = 0;
    fd = strtol (pmi_fd, NULL, 10);
    pmi->rank = strtol (pmi_rank, NULL, 10);
    pmi->size = strtol (pmi_size, NULL, 10);
    if (errno != 0 || fd < 0 || pmi->rank < 0 || pmi->size < 1)
        goto error;
    if (pmi_spawned) {
        errno = 0;
        pmi->spawned = strtol (pmi_spawned, NULL, 10);
        if (errno != 0)
            goto error;
    }
    if (!(pmi->f = fdopen (fd, "w"))) // pmi->f takes ownership of fd
        goto error;
    setvbuf (pmi->f, NULL, _IONBF, 0); // each fprintf is one write()
    pmi->fd = fd;
    fd = -1;
    pmi->buflen = pmi->rbufsize = SIMPLE_MAX_PROTO_LINE;
    if (!(pmi->buf = calloc (1, pmi->buflen))
        || !(pmi->rbuf = calloc (1, pmi->rbufsize)))
        goto error;
    if (pipe2 (pmi->wakefd, O_CLOEXEC | O_NONBLOCK) < 0)
        goto error;
    return pmi;
error:
    if (fd >= 0)
        (void)close (fd);
    pmi_simple_client_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
