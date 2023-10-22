======================
flux_shell_get_flux(3)
======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   flux_t *flux_shell_get_flux (flux_shell_t *shell);


DESCRIPTION
===========

Returns the Flux handle.


RETURN VALUE
============

Returns the Flux handle.


ERRORS
======

No error conditions are possible.


EXAMPLE
=======

::

   // Set a timer in flux_plugin_init().

::

   void flux_plugin_init (flux_plugin_t *p){

::

   // Get the shell handle,
   flux_shell_t *shell = flux_plugin_get_shell( p );

::

   // use that to get the flux handle,
   flux_t *flux = flux_shell_get_flux( shell );

::

   // and use that to get the reactor handle.
   flux_reactor_t *reactor = flux_get_reactor( flux );

::

   flux_watcher_t* timer   = flux_timer_watcher_create( reactor, 0.1, 0.1, timer_cb, NULL );
   flux_watcher_start(timer);

::

   ....


RESOURCES
=========

Flux: http://flux-framework.org
