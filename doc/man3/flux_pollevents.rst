==================
flux_pollevents(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_pollevents (flux_t *h);

  int flux_pollfd (flux_t *h);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_pollevents` returns a bitmask of poll flags for handle :var:`h`.

:func:`flux_pollfd` obtains a file descriptor that becomes readable, in
an edge triggered fashion, when :func:`flux_pollevents` has poll flags
raised.

Valid poll flags are:

FLUX_POLLIN
   Handle is ready for reading.

FLUX_POLLOUT
   Handle is ready for writing.

FLUX_POLLERR
   Handle has experienced an error.

These functions can be used to integrate a :type:`flux_t` handle into an
external event loop. They are analogous to the ZMQ_FD and ZMQ_EVENTS
socket options provided by ZeroMQ.


RETURN VALUE
============

:func:`flux_pollevents` returns flags on success. On error, -1 is returned,
and :var:`errno` is set appropriately.

:func:`flux_pollfd` returns a file descriptor on success. On error, -1 is
returned, and :var:`errno` is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.


EXAMPLES
========

Here is an example of a libev "composite watcher" for a Flux handle
using the hooks provided above. This code, more or less, is used internally
to integrate flux handles into the Flux reactor, which is based on libev.
Refer to the libev documentation for background on how libev works.

There are a total of four different types of libev watcher in the
composite watcher. libev "prepare" and "check" callbacks are executed
just before and just after libev blocks internally in the
:linux:man2:`poll` system call.  Here they are used to test
:func:`flux_pollevents`, make user callbacks, and enable/disable no-op
"io" and "idle" watchers. The io watcher watches for EV_READ on
:func:`flux_pollfd` file descriptor. The idle watcher, if enabled, is
always ready and thus causes the event loop to spin.

When :func:`flux_pollevents` has poll flags asserted, the idle watcher is
enabled.  When :func:`flux_pollevents` has no poll flags asserted, the idle
watcher is disabled and the io watcher is enabled. While the idle and io
watchers have no callbacks, if either is enabled and ready, the event loop
must execute the prepare and check callbacks.

The net results are 1) the edge-triggered notification provided by
:func:`flux_pollfd` is integrated with libev's level-triggered watcher
processing; 2) the handle is able to give control back to the event
loop between handle event callbacks to preserve fairness, i.e.
it doesn't have to consume events until they they are gone in one
callback; and 3) the event loop is able to sleep when there are no
handle events pending.

::

   // ev_flux.h
   #include <ev.h>

   struct ev_flux;

   typedef void (*ev_flux_f)(struct ev_loop *loop,
                             struct ev_flux *w, int revents);

   struct ev_flux {
       ev_io       io_w;
       ev_prepare  prepare_w;
       ev_idle     idle_w;
       ev_check    check_w;
       flux_t      *h;
       int         pollfd;
       int         events;
       ev_flux_f   cb;
       void        *data;
   };

::

   // ev_flux.c
   static int get_pollevents (flux_t *h)
   {
       int e = flux_pollevents (h);
       int events = 0;
       if (e < 0 || (e & FLUX_POLLERR))
           events |= EV_ERROR;
       if ((e & FLUX_POLLIN))
           events |= EV_READ;
       if ((e & FLUX_POLLOUT))
           events |= EV_WRITE;
       return events;
   }

   static void prepare_cb (struct ev_loop *loop, ev_prepare *w,
                           int revents)
   {
       struct ev_flux *fw = (struct ev_flux *)((char *)w
                          - offsetof (struct ev_flux, prepare_w));
       int events = get_pollevents (fw->h);

       if ((events & fw->events) || (events & EV_ERROR))
           ev_idle_start (loop, &fw->idle_w);
       else
           ev_io_start (loop, &fw->io_w);
   }

   static void check_cb (struct ev_loop *loop, ev_check *w,
                         int revents)
   {
       struct ev_flux *fw = (struct ev_flux *)((char *)w
                          - offsetof (struct ev_flux, check_w));
       int events = get_pollevents (fw->h);

       ev_io_stop (loop, &fw->io_w);
       ev_idle_stop (loop, &fw->idle_w);

       if ((events & fw->events) || (events & EV_ERROR))
           fw->cb (loop, fw, events);
   }

   int ev_flux_init (struct ev_flux *w, ev_flux_f cb,
                     flux_t *h, int events)
   {
       w->cb = cb;
       w->h = h;
       w->events = events;
       if ((w->pollfd = flux_pollfd (h)) < 0)
           return -1;

       ev_prepare_init (&w->prepare_w, prepare_cb);
       ev_check_init (&w->check_w, check_cb);
       ev_idle_init (&w->idle_w, NULL);
       ev_io_init (&w->io_w, NULL, w->pollfd, EV_READ);

       return 0;
   }

   void ev_flux_start (struct ev_loop *loop, struct ev_flux *w)
   {
       ev_prepare_start (loop, &w->prepare_w);
       ev_check_start (loop, &w->check_w);
   }

   void ev_flux_stop (struct ev_loop *loop, struct ev_flux *w)
   {
       ev_prepare_stop (loop, &w->prepare_w);
       ev_check_stop (loop, &w->check_w);
       ev_io_stop (loop, &w->io_w);
       ev_idle_stop (loop, &w->idle_w);
   }


RESOURCES
=========

Flux: http://flux-framework.org

libev API: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod

zmq_getsockopt(3): http://api.zeromq.org/4-0:zmq-getsockopt

Embedding ZeroMQ in the libev event loop:
http://funcptr.net/2013/04/20/embedding-zeromq-in-the-libev-event-loop
