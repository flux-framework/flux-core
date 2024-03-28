==========================
flux-config-job-manager(5)
==========================


DESCRIPTION
===========

The Flux **job-manager** service may be configured via the ``job-manager``
table, which may contain the following keys:


KEYS
====

inactive-age-limit
   (optional) String (in RFC 23 Flux Standard Duration format) that specifies
   the maximum age of inactive jobs retained in the KVS.  The age is computed
   since the job became inactive.  Once a job is removed from the KVS, its job
   data is only available via the job-archive, if configured.  Inactive jobs
   can also be manually purged with :man1:`flux-job` ``purge``.

inactive-num-limit
   (optional) Integer maximum number of inactive jobs retained in the KVS.

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

   inactive-age-limit = "7d"
   inactive-num-limit = 10000

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

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`, :man1:`flux-jobtap`, :man7:`flux-jobtap-plugins`
