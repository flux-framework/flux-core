==============
flux-jobtap(1)
==============


SYNOPSIS
========

| **flux** **jobtap** **load** [*--remove=NAME*] *plugin* [*key=val...*]
| **flux** **jobtap** **remove** *plugin*
| **flux** **jobtap** **list** [*--all*]
| **flux** **jobtap** **query** *plugin*

DESCRIPTION
===========

The :program:`flux jobtap` command is used to query, load, and remove *jobtap*
plugins from the Flux job-manager module at runtime.

COMMANDS
========

load
----

.. program:: flux jobtap load

Load a new plugin into the job-manager.  Optional *key=val* arguments
occurring after *plugin*  will set config *key* to *val* for *plugin*.

.. option:: --remove=NAME

  Remove plugin *NAME* before loading *plugin*.  *NAME* may be a
  :linux:man7:`glob` pattern match.

remove
------

.. program:: flux jobtap remove

Remove *plugin*. *plugin* may be a :linux:man7:`glob` pattern in
which case all matching, non-builtin plugins are removed. The
special value ``all`` may be used to remove all loaded jobtap
plugins. Builtin plugins (those starting with a leading ``.``) must
be removed explicitly or by preceding *NAME* with ``.``,
e.g. ``.*``.

list
----

.. program:: flux jobtap list

Print the currently loaded list of plugins.  Plugins built in to the job
manager have a leading ``.`` in the name, e.g. ``.priority-default``.
They are not displayed by default.

.. option:: -a, --all

  List builtin plugins too.

query
-----

.. program:: flux jobtap query

Print a JSON object with extended information about *plugin*. This
includes at least the plugin name and path (or "builtin" if the plugin
was loaded internally), but may contain plugin-specific data if the plugin
supports the ``plugin.query`` callback topic.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man7:`flux-jobtap-plugins`
