=====================
flux-shell-plugins(7)
=====================


DESCRIPTION
===========

The job shell supports external and builtin plugins which implement most
of the advanced job shell features. Job shell plugins are loaded into
a plugin stack by name, where the last loaded name wins. Therefore, to
override a builtin plugin, an alternate plugin which registers the same
name may be loaded at runtime.

.. note::
   Job shell plugins should be written with the assumption their access
   to Flux services may be restricted as a guest.

C plugins are defined using the Flux standard plugin format. A shell C
plugin should therefore export a single symbol ``flux_plugin_init()``, in
which calls to ``flux_plugin_add_handler(3)`` should be used to register
functions which will be invoked at defined points during shell execution.
These callbacks are defined by "topic strings" to which plugins can
"subscribe" by calling ``flux_plugin_add_handler(3)`` and/or
``flux_plugin_register(3)`` with topic :linux:man7:`glob` strings.

.. note::
   ``flux_plugin_init(3)`` is not called for builtin shell plugins. If
   a dynamically loaded plugin wishes to set shell options to influence
   a shell builtin plugin (e.g. to disable its operation), it should
   therefore do so in ``flux_plugin_init()`` in order to guarantee that
   the shell option is set before the builtin attempts to read them.

Simple plugins may also be developed directly in the shell ``initrc.lua``
file itself (see :man5:`flux-shell-initrc`, ``plugin.register()``)

By default, :program:`flux shell` supports the following plugin callback
topics:

**taskmap.SCHEME**
  Called when a taskmap scheme *SCHEME* is requested via the ``taskmap``
  shell option or the corresponding :option:`flux submit --taskmap` option.
  Plugins can register a ``taskmap.*`` callback to provide custom task
  mapping schemes beyond the built-in ``block``, ``cyclic``, ``hostfile``,
  and ``manual`` schemes. The callback receives the default block taskmap
  in the "taskmap" input argument and should return a new taskmap string
  in the output arguments. This callback is invoked before ``shell.init``.

**mustache.render.TEMPLATE**
  Called when a mustache template *TEMPLATE* needs to be rendered by the
  shell. This allows plugins to implement custom mustache templates. For
  example, the tmpdir plugin renders ``{{tmpdir}}`` by subscribing to
  ``mustache.render.tmpdir``. This callback is invoked for each task
  before it starts, for any environment variable containing a mustache
  template, or when a plugin calls ``flux_shell_mustache_render()``.

**shell.connect**
  Called immediately after the shell connects to the local Flux broker,
  before jobspec and resource information are fetched. This callback is
  only available to builtin shell plugins since it runs before the
  system initrc is loaded and external plugins can be initialized.

**shell.init**
  Called after the shell has finished fetching and parsing the
  **jobspec** and **R** from the KVS, but before any tasks
  are started.

**shell.post-init**
  Called after the shell initialization barrier has completed, but
  before starting any tasks.

**task.init**
  Called for each task after the task info has been constructed
  but before the task is executed.

**task.exec**
  Called for each task after the task has been forked just before
  :linux:man2:`execve` is called. This callback is made from within the
  task process.

**task.fork**
  Called for each task after the task if forked from the parent
  process (:program:`flux shell` process)

**task.exit**
  Called for each task after it exits and wait_status is available.

**shell.start**
  Called after all local tasks have been started. The shell "start"
  barrier is called just after this callback returns.

**shell.finish**
  Called after all local tasks have exited but before the shell exits
  the reactor. This callback runs while the reactor is still active,
  making it suitable for cleanup operations or asynchronous work that
  requires an active event loop.

**shell.exit**
  Called after all local tasks have exited and the shell has exited
  the reactor. This is the final callback before the shell process
  terminates.

**shell.log**
  Called by the shell logging facility when a shell component
  posts a log message.

**shell.log-setlevel**
  Called by the shell logging facility when a request to set the
  shell loglevel is made.

**shell.lost**
  Called by the shell when the job receives a lost-shell exception.
  The callback context contains the affected shell rank (``shell_rank``)
  and exception ``severity``.

**shell.resource-update**
  Called by the shell when the job's resource set **R** is updated.

**shell.reconnect**
  Called by the shell after reconnecting its handle to the local Flux
  broker (i.e., due to a broker restart).

Note however, that plugins may also call into the plugin stack to create
new callbacks at runtime, so more topics than those listed above may be
available in a given shell instance.

RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-shell`, :man5:`flux-shell-initrc`, :man7:`flux-shell-options`
