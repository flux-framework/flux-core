#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <limits.h>
#include <assert.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "hostlist.h"

static json_object *_host_to_json (char *host)
{
    struct addrinfo hints, *res = NULL, *r;
    int errnum;
    char ipaddr[64];
    json_object *ao, *no, *o = util_json_object_new_object (); 

    util_json_object_add_string (o, "name", host);

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((errnum = getaddrinfo (host, NULL, &hints, &res))) {
        msg ("getaddrinfo: %s: %s", host, gai_strerror (errnum));
        goto error;
    }
    if (res == NULL) {
        msg_exit ("unknown host: %s\n", host);
        goto error;
    }
    if (!(ao = json_object_new_array ()))
        oom ();
    for (r = res; r != NULL; r = r->ai_next) {
        if ((errnum = getnameinfo (r->ai_addr, r->ai_addrlen,
                                   ipaddr, sizeof (ipaddr),
                                   NULL, 0, NI_NUMERICHOST)))  {
            msg ("getnameinfo: %s: %s", host, gai_strerror (errnum));
            goto error;
        }
        if (!(no = json_object_new_string (ipaddr)))
            oom ();
        json_object_array_add (ao, no);
    }
    freeaddrinfo (res);
    json_object_object_add (o, "addrs", ao);
    return o;
error:
    if (res)
        freeaddrinfo (res);
    return NULL;
}

json_object *hostlist_to_json (char *s)
{
    hostlist_t hl = hostlist_create (s);
    hostlist_iterator_t itr;
    json_object *ao, *o;
    char *host;

    if (!(ao = json_object_new_array ()))
        oom ();
    if (!hl)
        msg_exit ("failed to parse hostlist");
    itr = hostlist_iterator_create (hl);
    while ((host = hostlist_next (itr))) {
        if (!(o = _host_to_json (host)))
            msg_exit ("could not look up %s", host);
        json_object_array_add (ao, o);
    }
    hostlist_iterator_destroy (itr);
    hostlist_destroy (hl);

    return ao;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
