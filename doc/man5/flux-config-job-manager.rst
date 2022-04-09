==========================
flux-config-job-manager(5)
==========================


DESCRIPTION
===========

The Flux **job-manager** service may be configured via the ``job-manager``
table, which may contain the following keys:


KEYS
====

journal-size-limit
   (optional) Integer value that determines the maximum number of job events to
   be retained in the in-memory journal used to answer queries.  The default
   is 1000.

plugins
   (optional) An array of objects defining a list of jobtap plugin directives.
   Each directive follows the format defined in the :ref:`plugin_directive`
   section.


.. _plugin_directive:

PLUGIN DIRECTIVE
================

load
   (optional) A string instructing the job manager to load a plugin matching
   the given filename into the job-manager.  If the path is not absolute,
   then the first plugin matching the job-manager searchpath will be loaded.

remove
   (optional) A string instructing the job manager to remove all plugins
   matching  the  value.  The  value may be a :linux:man7:`glob`. If ``remove``
   appears with ``load``, plugin removal is always handled first.  The special
   value ``all`` is a synonym for ``*``, but will not fail when no plugins
   match.

conf
   (optional) An object, valid with ``load`` only, that defines a configuration
   table to pass to the loaded plugin.


EXAMPLE
=======

::

   [job-manager]

   journal-size-limit = 10000

   plugins = [
      {
        load = "priority-custom.so",
        conf = {
           job-limit = 100,
           size-limit = 128
        }
      }
   ]


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man5:`flux-config`, :man1:`flux-jobtap`, :man7:`flux-jobtap-plugins`
