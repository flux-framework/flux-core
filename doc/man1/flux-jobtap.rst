==============
flux-jobtap(1)
==============


SYNOPSIS
========

**flux** **jobtap** *COMMAND* [*OPTIONS*] ARGS...

DESCRIPTION
===========

The flux-jobtap(1) command is used to query, load, and remove *jobtap*
plugins from the Flux job-manager module at runtime.

COMMANDS
========

**list** [*-a, --all*]
  Print the currently loaded list of plugins. Builtin plugins will only
  be displayed when the *--all* option is used. Plugins built in to the
  job manager have a leading ``.`` in the name, e.g. ``.priority-default``.

**load** [*-r*, *--remove=NAME*] PLUGIN [KEY=VAL, KEY=VAL...]
  Load a new plugin into the job-manager, optionally removing plugin NAME
  first. With *--remove* NAME may be a glob(7) pattern match. Optional
  KEY=VAL occurring after PLUGIN will set config KEY to VAL for PLUGIN.

**remove** NAME
  Remove plugin NAME. NAME may be a glob(7) pattern in which case all
  matching, non-builtin plugins are removed. The special value `all` may
  be used to remove all loaded jobtap plugins. Builtin plugins (those
  starting with a leading ``.``) must be removed explicitly or by
  preceding *NAME* with ``.``, e.g. ``.*``.

RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man7:`flux-jobtap-plugins`
