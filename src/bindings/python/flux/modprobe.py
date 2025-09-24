#############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import bisect
import concurrent
import glob
import os
import subprocess
import sys
import threading
import time
from collections import OrderedDict, defaultdict, namedtuple
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import flux
import flux.importer
from flux.conf_builtin import conf_builtin_get
from flux.idset import IDset
from flux.utils import tomli as tomllib
from flux.utils.graphlib import TopologicalSorter


def run_all_rc_scripts(runlevel):
    """
    Helper script for flux-modprobe(1) rc1 and rc3 scripts that replaces
    the following shell code from rc1/rc3:
    ```
    core_dir=$(cd ${0%/*} && pwd -P)
    all_dirs=$core_dir${FLUX_RC_EXTRA:+":$FLUX_RC_EXTRA"}
    IFS=:
    for rcdir in $all_dirs; do
        for rcfile in $rcdir/rc{runlevel}.d/*; do
        [ -e $rcfile ] || continue
            echo running $rcfile
            $rcfile || exit_rc=1
        done
    done
    ```

    Args:
        runlevel (int): runlevel (1 or 3) in which function is running
    Raises:
        OSError: one or more rc scripts failed
    """
    success = True
    core_dir = Path(conf_builtin_get("confdir")).resolve()
    all_dirs = [core_dir]
    rc_extra = os.environ.get("FLUX_RC_EXTRA")
    if rc_extra:
        all_dirs.extend(Path(d) for d in rc_extra.split(":") if d.strip())

    for entry in all_dirs:
        rcdir = entry / f"rc{runlevel}.d"
        if not rcdir.exists() or not rcdir.is_dir():
            continue
        try:
            # Get all files in rcX.d directory, sorted by name
            rc_files = sorted(
                [
                    file
                    for file in rcdir.iterdir()
                    if file.is_file() and os.access(file, os.X_OK)
                ]
            )

            # for rcfile in $rcdir/rc1.d/*; do
            for rcfile in rc_files:
                try:
                    print(f"running {rcfile}")
                    subprocess.run([str(rcfile)], check=True)
                except subprocess.CalledProcessError as e:
                    success = False
                    print(
                        f"{rcfile} failed with exit code {e.returncode}",
                        file=sys.stderr,
                    )
                except (FileNotFoundError, PermissionError, OSError) as e:
                    success = False
                    print(f"Cannot execute {rcfile}: {e}", file=sys.stderr)

        except (PermissionError, OSError) as e:
            success = False
            print(f"Cannot access directory {rcdir}: {e}", file=sys.stderr)

    if not success:
        raise OSError(f"one or more rc{runlevel}.d scripts failed")


def default_flux_confdir():
    """
    Return the builtin Flux confdir
    """
    return Path(conf_builtin_get("confdir"))


class RankConditional:
    """
    Conditional rank statement, e.g. ``>0``
    """

    def __init__(self, arg):
        if arg[0] == ">":
            self.gt = True
        elif arg[0] == "<":
            self.gt = False
        else:
            raise ValueError("rank condition must be either < or >")
        self.rank = int(arg[1:])

    def test(self, rank):
        if self.gt:
            return rank > self.rank
        return rank < self.rank

    def __str__(self):
        s = ">" if self.gt else ">"
        return f"{s}{self.rank}"


class RankIDset:
    """
    Rank by IDset, e.g. ``all`` or ``0-1``
    """

    def __init__(self, arg):
        self.ranks = None
        self.all = False
        if arg == "all":
            self.all = True
        else:
            try:
                self.ranks = IDset(arg)
            except ValueError:
                raise ValueError(f"ranks: invalid idset: {arg}")

    def test(self, rank):
        if self.all:
            return True
        return self.ranks[rank]

    def __str__(self):
        if self.all:
            return "all"
        return f"{self.ranks}"


def rank_conditional(arg):
    """
    Rank conditional factory function
    """
    cls = RankIDset
    if arg.startswith((">", "<")):
        cls = RankConditional
    return cls(arg)


class Task:
    """
    Class representing a modprobe task and associated configuration
    """

    VALID_ARGS = {
        "ranks": "all",
        "provides": [],
        "requires": [],
        "needs": [],
        "before": [],
        "after": [],
        "needs_attrs": [],
        "needs_config": [],
        "disabled": False,
        "priority": 100,
    }

    def __init__(self, name, *args, **kwargs):
        self.name = name
        self.starttime = None
        self.endtime = None
        self.force_enabled = False

        for attr in kwargs:
            if attr not in self.VALID_ARGS:
                raise ValueError(f"{self.name}: unknown task argument {attr}")

        for attr, default in self.VALID_ARGS.items():
            val = kwargs.get(attr, default)
            # Handle case where attr is set explicitly to None, in which case,
            # inherit the default
            if val is None:
                val = default
            setattr(self, attr, val)

        # convert self.ranks to rank conditional object:
        self.ranks = rank_conditional(self.ranks)

    def enabled(self, context=None):
        """
        Return True if task is currently not disabled. A task may be
        disabled by configuration, because it only runs on a given set of
        ranks, if the task has been configured to require a configuration
        or broker attribute which is not set, or if a module this task
        needs is not enabled.
        """
        if self.force_enabled:
            return True
        if context is None:
            return not self.disabled
        if self.disabled or not self.ranks.test(context.rank):
            return False
        for key in self.needs_config:
            val = context.handle.conf_get(key)
            if not val:
                return False
        for attr in self.needs_attrs:
            val = context.attr_get(attr)
            if not val:
                return False
        return True

    def runtask(self, context):
        """
        Run this task's run() method (or dry_run() if dry_run is True)
        """
        self.starttime = time.time()
        try:
            if context.dry_run:
                self.dry_run(context)
            else:
                self.run(context)
        finally:
            self.endtime = time.time()

    def run(self, context):
        """
        A task's run() method. This should be overridden in the specific
        Task subclass.
        """
        print(self.name)

    def dry_run(self, context):
        """
        Default task dry_run() method. This prints the task name. Override
        in a subclass with more specific information if necessary.
        """
        print(self.name)


class CodeTask(Task):
    """
    A modprobe task that runs as a Python function
    """

    def __init__(self, name, func, *args, **kwargs):
        self.func = func
        super().__init__(name, *args, **kwargs)

    def run(self, context):
        context.print(f"start {self.name}")
        self.func(context)
        context.print(f"completed {self.name}")

    def dry_run(self, context):
        print(f"run {self.name}")


class Module(Task):
    """
    A modprobe task to load/remove a broker module.
    The default action is to the load the module. Call the ``set_remove()``
    method to convert the task to remove the module.
    """

    VALID_KEYS = (
        "name",
        "module",
        "args",
        "ranks",
        "provides",
        "requires",
        "needs",
        "before",
        "after",
        "needs-attrs",
        "needs-config",
        "priority",
        "disabled",
        "exec",
    )

    def __init__(self, conf):
        """Initialize a module task from modprobe.toml entry
        The default run method loads the module.
        Call set_remove() to set the task to unload a module.
        """
        try:
            name = conf["name"]
        except KeyError:
            raise ValueError("Missing required config key 'name'") from None

        self.module = conf.get("module", name)
        self.args = conf.get("args", [])
        self.exec = conf.get("exec", False)
        self.run = self._load

        # Build kwargs to pass along to Task class
        kwargs = {}
        for key, val in conf.items():
            if key not in self.VALID_KEYS:
                prefix = f"{self.name}: " if hasattr(self, "name") else ""
                raise ValueError(f"{prefix}invalid config key {key}")
            key = key.replace("-", "_")
            if key in super().VALID_ARGS:
                kwargs[key] = val

        super().__init__(name, **kwargs)

    def set_remove(self):
        """
        Mark module to be removed instead of loaded (the default)
        """
        # swap before and after
        self.after, self.before = self.before, self.after
        # clear needs and requires since these do not apply to module removal:
        self.needs = []
        self.requires = []
        self.run = self._remove

    def _load(self, context):
        args = context.getopts(self.name, default=self.args, also=self.provides)
        payload = {"path": self.module, "args": args, "exec": self.exec}

        if self.name != self.module:
            payload["name"] = self.name

        context.print(f"module.load {payload}")
        context.handle.rpc("module.load", payload).get()
        context.print(f"loaded {self.name}")

    def _remove(self, context):
        try:
            context.print(f"module.remove {self.name}")
            context.handle.rpc("module.remove", {"name": self.name}).get()
            context.print(f"removed {self.name}")
        except FileNotFoundError:
            # Ignore already unloaded modules
            pass

    def dry_run(self, context):
        if self.run == self._load:
            module_args = context.getopts(
                self.name, default=self.args, also=self.provides
            )
            print(" ".join([f"load {self.name}", *module_args]))
        else:
            print(f"remove {self.name}")

    def to_dict(self):
        return {
            attribute: getattr(self, attribute)
            for attribute in self.VALID_KEYS
            if hasattr(self, attribute)
        }


class TaskDB:
    """
    Dict of service/module name to list of tasks sorted such the
    current default task is at the end of the list.
    """

    TaskEntry = namedtuple("TaskEntry", ("priority", "index", "task"))

    def __init__(self):
        self.tasks = defaultdict(list)

    def add(self, task, index=None):
        for name in (*task.provides, task.name):
            if index is None:
                index = len(self.tasks[name])
            bisect.insort_right(
                self.tasks[name],
                self.TaskEntry(task.priority, index, task),
            )

    def get(self, service):
        """
        Return the highest priority task providing ``service`` which is also
        enabled. If there are no enabled tasks providing ``service``, then
        return the highest priority task. If no tasks provide ``service``,
        raise ``ValueError``.
        """
        if len(self.tasks[service]) == 0:
            raise ValueError(f"no such task or module {service}")
        enabled = list(filter(lambda x: x.task.enabled(), self.tasks[service]))
        if len(enabled) == 0:
            return self.tasks[service][-1].task
        return enabled[-1].task

    def update(self, task):
        """
        Update a task object which already resides in the taskdb by removing
        the task from all service name lists and re-adding it, preserving the
        original insertion order. This handles any potential update of a task,
        such as a new ``priority``, or additional ``provides``
        """
        for name in (task.name, *task.provides):
            to_remove = -1
            for i, entry in enumerate(self.tasks[name]):
                if entry.task == task:
                    to_remove = i
                    break
            if to_remove < 0:
                raise ValueError(f"{task.name} not found in taskdb {name} list")

            # remove task from this list and re-insert, preserving the
            # original insertion index:
            self.tasks[name].pop(to_remove)
            self.add(task, index=entry.index)

    def set_alternative(self, service, name):
        """Select a specific alternative 'name' for service"""
        lst = self.tasks[service]
        try:
            index = next(i for i, x in enumerate(lst) if x.task.name == name)
        except StopIteration:
            raise ValueError(f"no module {name} provides {service}")

        # nothing to do if list has length of 1
        if len(lst) == 1:
            return

        # bump priority of new task and move to end of list:
        entry = lst.pop(index)
        priority = lst[-1].priority
        lst.append(self.TaskEntry(priority + 1, entry.index, entry.task))

    def disable(self, service):
        """disable task/module/service with name 'service'"""
        if not self.tasks[service]:
            raise ValueError(f"no such module or task '{service}'")
        for entry in self.tasks[service]:
            entry.task.disabled = True

    def enable(self, service):
        """
        Force a module/task/service to be enabled even if it would normally
        be disabled by rank, needs-config, or needs-attr.
        """
        if not self.tasks[service]:
            raise ValueError(f"no such module or task '{service}'")
        for entry in self.tasks[service]:
            entry.task.force_enabled = True

    def any_provides(self, tasks, name):
        """Return True if any task in tasks provides name"""
        for task in [self.get(x) for x in tasks]:
            if not task.disabled and name in (task.name, *task.provides):
                return True
        return False


def task(name, **kwargs):
    """
    Decorator for modprobe "rc" task functions.

    This decorator is applied to functions in an rc1 or rc3 python
    source file to turn them into valid flux-modprobe(1) tasks.

    Args:
        name (required, str): The name of this task.
        ranks (optional, str): A rank expression that indicates on which
            ranks this task should be invoked. ``ranks`` may be a valid
            RFC 22 Idset string, a single integer prefixed with ``<`` or
            ``<`` to indicate matching ranks less than or greater than a
            given rank, or the string ``all`` (the default if ``ranks``
            is not specified). Examples: ``0``, ``>0``, ``0-3``.
        requires (optional, list): An optional list of task or module names
            this task requires. This is used to ensure required tasks are
            active when activating another task. It does not indicate that
            this task will necessarily be run before or after the tasks it
            requires. (See ``before`` or ``after`` for those features)
        needs (options, list): Disable this task if any task in ``needs`` is
            not active.
        provides (optional, list): An optional list of string service name
            that this task provides. This can be used to set up alternatives
            for a given service. (Mostly useful with modules)
        before (optional, list): A list of tasks or modules for which this task
            must be run before.
        after (optional, list) A list of tasks or modules for which this task
            must be run after.
        needs_attrs (optional, list): A list of broker attributes on which
            this task depends. If any of the attributes are not set then the
            task will not be run.
        needs_config (optional, list): A list of config keys on which this
            task depends. If any of the specified config keys are not set,
            then this task will not be run.

    Example:
    ::
        # Declare a task that will be run after the kvs module is loaded
        # only on rank 0
        @task("test", ranks="0", needs=["kvs"], after=["kvs"])
        def test_kvs_task(context):
            # do something with kvs
    """
    if not isinstance(name, str):
        raise ValueError('task missing required name argument: @task("name")')

    def create_task(func):
        return CodeTask(name, func, **kwargs)

    return create_task


class Context:
    """
    Context object passed to all modprobe tasks.
    Allows the passage of data between tasks, simple access to broker
    configuration and attributes, addition of module arguments, etc.
    """

    tls = threading.local()

    def __init__(self, modprobe, verbose=False, dry_run=False):
        self.verbose = verbose
        self.dry_run = dry_run
        self.modprobe = modprobe
        self._data = {}
        self.module_args = defaultdict(list)
        self.module_args_overwrite = {}

    def print(self, *args):
        """Print message if modprobe is in verbose output mode"""
        if self.verbose:
            print(*args, file=sys.stderr)

    @property
    def handle(self):
        """Return a per-thread Flux handle created on demand"""
        if not hasattr(self.tls, "_handle"):
            self.tls._handle = flux.Flux()
        return self.tls._handle

    @property
    def rank(self):
        return self.handle.get_rank()

    def set(self, key, value):
        """Set arbitrary data at key for future use. (see get())"""
        self._data[key] = value

    def get(self, key, default=None):
        """Get arbitrary data set by other tasks with optional default value"""
        return self._data.get(key, default)

    def attr_get(self, attr, default=None):
        """Get broker attribute with optional default"""
        try:
            return self.handle.attr_get(attr)
        except FileNotFoundError:
            return default

    def conf_get(self, key, default=None):
        """Get config key with optional default"""
        return self.handle.conf_get(key, default=default)

    def rpc(self, topic, *args, **kwargs):
        """Convenience function to call context.handle.rpc()"""
        return self.handle.rpc(topic, *args, **kwargs)

    def setopt(self, module, options, overwrite=False):
        """
        Append option to module opts. ``option`` may contain multiple options
        separated by whitespace.
        """
        if overwrite:
            self.module_args_overwrite[module] = True

        self.module_args[module].extend(options.split())

    def getopts(self, name, default=None, also=None):
        """Get module opts for module 'name'
        If also is provided, append any module options for those names as well
        """
        lst = [name]
        if also is not None:
            lst.extend(also)
        result = []
        if default is not None and not self.module_args_overwrite.get(name):
            result = list(default)
        for name in lst:
            result.extend(self.module_args[name])
        return result

    def bash(self, command):
        """Execute command under ``bash -c``"""
        process = subprocess.run(["bash", "-c", command])
        if process.returncode != 0:
            if process.returncode > 0:
                raise RuntimeError(
                    f"bash: exited with exit status {process.returncode}"
                )
            else:
                raise RuntimeError(f"bash: died by signal {process.returncode}")

    def load_modules(self, modules):
        """Set a list of modules to load by name"""
        self.modprobe.activate_modules(modules)

    def remove_modules(self, modules=None):
        """
        Set a list of modules to remove by name.
        Remove all if ``modules`` is None.
        """
        self.modprobe.set_remove(modules)

    def set_alternative(self, name, alternative):
        """Force an alternative for module ``name`` to ``alternative``"""
        self.modprobe.taskdb.set_alternative(name, alternative)

    def enable(self, name):
        """
        Force enable a module/service/task, overriding ranks conditional,
        needs-config, and needs-attrs.

        Note: This will not also enable dependencies of ``name``.
        """
        self.modprobe.taskdb.enable(name)


class ModuleList:
    """Simple class for iteration and lookup of loaded modules"""

    def __init__(self, handle):
        resp = handle.rpc("module.list").get()
        self.loaded_modules = []
        self.servicemap = {}
        for entry in resp["mods"]:
            if entry["path"] != "builtin":
                self.loaded_modules.append(entry["name"])
                for name in entry["services"]:
                    self.servicemap[name] = entry["name"]

    def __iter__(self):
        for name in self.loaded_modules:
            yield name

    def lookup(self, name):
        return self.servicemap.get(name, None)


class Modprobe:
    """
    The modprobe main class. Intended for use by flux-modprobe(1).
    """

    def __init__(self, timing=False, verbose=False, dry_run=False):
        self.exitcode = 0
        self.timing = None
        self.t0 = None
        self._locals = None

        self.taskdb = TaskDB()
        self.context = Context(self, verbose=verbose, dry_run=dry_run)
        self.handle = self.context.handle
        self.rank = self.handle.get_rank()

        self.searchpath = {
            "toml": self._get_searchpath(),
            "py": self._get_searchpath(builtindir="libexecdir"),
        }

        # Active tasks are those added via the @task decorator, and
        # which will be active by default when running "all" tasks:
        self._active_tasks = []

        if timing:
            self.timing = []
            self.t0 = time.time()

    @property
    def timestamp(self):
        if not self.t0:
            return 0.0
        return time.time() - self.t0

    @property
    def active_tasks(self):
        """Return all active, enabled tasks"""
        return self._process_needs(
            list(
                filter(
                    lambda task: self.get_task(task).enabled(self.context),
                    self._active_tasks,
                )
            )
        )

    def print(self, *args):
        """Wrapper for context.print()"""
        self.context.print(*args)

    def add_timing(self, name, starttime, end=None):
        if self.timing is None:
            return
        if end is None:
            end = self.timestamp
        self.timing.append(
            {"name": name, "starttime": starttime, "duration": end - starttime}
        )

    def save_task_timing(self, tasks):
        if self.timing is None:
            return
        for task in sorted(tasks, key=lambda x: x.starttime):
            self.add_timing(
                task.name,
                starttime=task.starttime - self.t0,
                end=task.endtime - self.t0,
            )

    def add_task(self, task):
        """Add a task to internal task db"""
        self.taskdb.add(task)

    def add_active_task(self, task):
        """Add a task to the task db and active tasks list"""
        self.add_task(task)
        self._active_tasks.append(task.name)

    def get_task(self, name, default=None):
        """Return current task providing string 'name'"""
        return self.taskdb.get(name)

    def has_task(self, name):
        """Return True if task exists in taskdb"""
        try:
            self.taskdb.get(name)
            return True
        except ValueError:
            return False

    def update_module(self, name, entry, new_module=None):
        task = self.get_task(name)
        if new_module is None:
            if "name" not in entry:
                entry["name"] = name
            new_module = Module(entry)
        for key in entry.keys():
            setattr(task, key, getattr(new_module, key))
        self.taskdb.update(task)

    def add_modules(self, file):
        with open(file, "rb") as fp:
            config = tomllib.load(fp)

        for name, entry in config.items():
            if name == "modules":
                for table in entry:
                    try:
                        task = Module(table)
                    except ValueError as exc:
                        raise ValueError(
                            f"{file}: invalid modules entry: {exc}"
                        ) from None

                    # Update tasks that already exist:
                    if self.has_task(task.name):
                        self.update_module(task.name, table, task)
                    else:
                        self.add_task(task)
            else:
                # Allow <module>.key to update an existing configured module:
                self.update_module(name, entry)

    def _get_searchpath(self, builtindir="datadir"):
        """
        Return list of dirs in ``FLUX_MODPROBE_PATH`` if set, o/w returns the
        default modprobe search path.
        Args:
            builtindir (str): base path for builtin/package path. Should
                be either "datadir" or "libexecdir".
        """
        searchpath = []
        if "FLUX_MODPROBE_PATH" in os.environ:
            searchpath = filter(
                lambda s: s and not s.isspace(),
                os.environ["FLUX_MODPROBE_PATH"].split(":"),
            )
        else:
            pkgdir = conf_builtin_get(builtindir)
            confdir = conf_builtin_get("confdir")
            searchpath = [f"{pkgdir}/modprobe", f"{confdir}/modprobe"]

        if "FLUX_MODPROBE_PATH_APPEND" in os.environ:
            searchpath.extend(
                filter(
                    lambda s: s and not s.isspace(),
                    os.environ["FLUX_MODPROBE_PATH_APPEND"].split(":"),
                )
            )

        # return searchpath without duplicates
        return list(OrderedDict.fromkeys(searchpath))

    def _searchpath_expand(self, name="modprobe", ext="toml"):
        """
        Expand searchpath for extension ``ext`` based on configured paths.
        """
        files = []
        for directory in self.searchpath[ext]:
            self.print(f"checking {directory}/{name}.d/*.{ext}")
            if Path(directory).exists():
                files.extend(sorted(glob.glob(f"{directory}/{name}.d/*.{ext}")))
        return files

    def _get_toml_files(self):
        """
        Return all modprobe config toml files found in the following order
         - Always read ``{fluxdatadir}/modprobe/modprobe.toml``
         - for dir in self.searchpath: read ``{dir}/modprobe.d/*.toml``
        """
        files = []
        builtin_toml_config = (
            Path(conf_builtin_get("datadir")) / "modprobe" / "modprobe.toml"
        )
        self.print(f"checking {builtin_toml_config}")
        if builtin_toml_config.exists():
            files.append(str(builtin_toml_config))
        files.extend(self._searchpath_expand())
        return files

    def _get_rc_files(self, name="rc1"):
        """
        Return all modprobe rc *.py files found in the following order
         - Always read ``{fluxdatadir}/modprobe/{name}.py`` (e.g. ``rc1.py``)
         - for dir in self.searchpath: read ``{dir}/{name}.d/*.py``
        """
        files = []
        builtin_rc_file = (
            Path(conf_builtin_get("libexecdir")) / "modprobe" / f"{name}.py"
        )
        self.print(f"checking {builtin_rc_file}")
        if builtin_rc_file.exists():
            files.append(str(builtin_rc_file))
        files.extend(self._searchpath_expand(name=name, ext="py"))
        return files

    def _update_modules_from_config(self):
        """Update modules using broker config
        Process a [modules] table in config support the following keys:

        alternatives: A table of keys that may adjust the current module
            alternative. e.g. ``alternatives.sched = "sched-simple"``
        <name>: A table of updates for an individual module, e.g.
            ``feasibility.ranks = "0,1"``
        """
        modules_conf = self.handle.conf_get("modules", default={})
        for key, entry in modules_conf.items():
            if key == "alternatives":
                for service, name in entry.items():
                    self.taskdb.set_alternative(service, name)
            else:
                self.update_module(key, entry)

    def configure_modules(self):
        """
        Load module configuration from TOML config.
        """
        for file in self._get_toml_files():
            self.print(f"loading {file}")
            self.add_modules(file)

        self._update_modules_from_config()

        return self

    def set_alternative(self, name, alternative):
        """
        Force an alternative for module ``name`` to ``alternative``
        """
        self.taskdb.set_alternative(name, alternative)

    def disable(self, name):
        """
        Disable module/task ``name``
        """
        self.taskdb.disable(name)

    def _solve_tasks_recursive(self, tasks, visited=None, skipped=None):
        """Recursively find all requirements of 'tasks'"""

        if visited is None:
            visited = set()
        if skipped is None:
            skipped = set()
        result = set()
        to_visit = [x for x in tasks if x not in visited]

        for name in to_visit:
            task = self.get_task(name)
            if task.enabled(self.context):
                result.add(task.name)
            else:
                skipped.add(task.name)
            visited.add(task.name)
            if task.requires:
                rset = self._solve_tasks_recursive(
                    tasks=task.requires, visited=visited, skipped=skipped
                )
                result.update(rset)

        return result

    def _process_needs(self, tasks):
        """Remove all tasks in tasks where task.needs is not met"""

        def remove_task_recursive(tasks, task):
            # When a task is removed, recursively remove tasks that
            # needed it:
            try:
                tasks.remove(task.name)
            except ValueError:
                # If task.name is not in current set of tasks it may be a
                # provider, so disable the task instead:
                self.disable(task.name)
                return
            provides_set = set((task.name, *task.provides))
            for name in tasks:
                x = self.get_task(name)
                if not provides_set.isdisjoint(x.needs):
                    # Task x needs task, remove it
                    remove_task_recursive(tasks, x)

        # Iterate over a copy of tasks list, removing tasks for which one
        # or more "needs" is not satisfied. When removing a task, tasks
        # that "need" that task are removed (recursively applied)
        for name in tasks.copy():
            task = self.get_task(name)
            for need in task.needs:
                if not self.taskdb.any_provides(tasks, need):
                    if name in tasks:
                        remove_task_recursive(tasks, task)

        return tasks

    def solve(self, tasks, timing=True):
        t0 = self.timestamp
        result = self._solve_tasks_recursive(tasks)
        if timing:
            self.add_timing("solve", t0)
        return result

    def _process_before(self, tasks, deps):
        """Process any task.before by appending this task's name to all
        successor's predecessor list.
        """

        def deps_add_all(name):
            """Add name as a predecessor to all entries in deps"""
            for task in [self.get_task(x) for x in deps.keys()]:
                if "*" not in task.before:
                    deps[task.name].append(name)

        for name in tasks:
            task = self.get_task(name)
            for successor in task.before:
                if successor == "*":
                    deps_add_all(task.name)
                else:
                    # resolve real successor name:
                    successor = self.get_task(successor).name
                    if successor in deps:
                        deps[successor].append(task.name)

    def get_deps(self, tasks):
        """Return dependencies for tasks as dict of names to predecessor list"""
        t0 = self.timestamp
        if not isinstance(tasks, set):
            tasks = set(tasks)
        deps = {}

        # Ensure tasks set contains all provides and the actual task name
        # (since presence in the set determines if a task is included in
        #  the predecessor list below)
        provides = set()
        for task in tasks:
            task = self.get_task(task)
            provides.add(task.name)
            provides.update(task.provides)
        tasks.update(provides)

        for name in tasks:
            task = self.get_task(name)
            if "*" in task.after:
                deps[task.name] = [
                    self.get_task(x).name for x in tasks if x != task.name
                ]
            else:
                after_tasks = [self.get_task(x).name for x in task.after]
                deps[task.name] = [x for x in after_tasks if x in tasks]
        self._process_before(tasks, deps)
        self.add_timing("deps", t0)

        return deps

    def get_requires(self, tasks, reverse=False):
        """Return dependencies for tasks as dicts of names to dependencies"""
        deps = {}
        for name in tasks:
            task = self.get_task(name)
            deps[task.name] = list(task.requires)
        if reverse:
            rdeps = {}
            for dependent, reqs in deps.items():
                for req in reqs:
                    if req not in rdeps:
                        rdeps[req] = set()
                    rdeps[req].add(dependent)
            return rdeps
        return deps

    def run(self, deps):
        """Run all tasks in deps in precedence order"""
        t0 = self.timestamp
        sorter = TopologicalSorter(deps)
        sorter.prepare()
        self.add_timing("prepare", t0)

        max_workers = None
        if sys.version_info < (3, 8):
            # In Python < 3.8, idle threads are not reused up to
            # max_workers. For these versions, set a low max_workers
            # to force thread (and therefore Flux handle) reuse:
            max_workers = 5

        executor = ThreadPoolExecutor(max_workers=max_workers)
        futures = {}
        started = {}

        while sorter.is_active():
            for task in [self.get_task(x) for x in sorter.get_ready()]:
                if task.name not in started:
                    future = executor.submit(task.runtask, self.context)
                    started[task.name] = task
                    futures[future] = task

            done, not_done = concurrent.futures.wait(
                futures.keys(), return_when=concurrent.futures.FIRST_COMPLETED
            )

            for future in done:
                task = futures[future]
                try:
                    future.result()
                except Exception as exc:
                    print(f"{task.name}: {exc}", file=sys.stderr)
                    self.exitcode = 1
                sorter.done(task.name)
                del futures[future]

        self.save_task_timing(started.values())
        executor.shutdown(wait=True)

        return self.exitcode

    def _load_file(self, path):
        module = flux.importer.import_path(path)
        tasks = filter(lambda x: isinstance(x, CodeTask), vars(module).values())
        for task in tasks:
            self.add_active_task(task)

        # Check for function setup() which should run before all other tasks
        setup = getattr(module, "setup", None)
        if callable(setup):
            setup(self.context)

    def read_rcfile(self, name):
        # For absolute file path, just add tasks from single file:
        if name.endswith(".py"):
            self.print(f"loading {name}")
            self._load_file(name)
            return

        # O/w, load all rc files in configured search path:
        for file in self._get_rc_files(name):
            self.print(f"loading {file}")
            self._load_file(file)

    def activate_modules(self, modules):
        for module in modules:
            task = self.get_task(module)
            if not isinstance(task, Module):
                raise ValueError(f"{module} is not a module")
            self._active_tasks.append(module)
            # append any requires from this module
            for other in task.requires:
                self._active_tasks.append(other)

    def _set_all_alternatives(self, modules):
        # Set all modules as the current selected alternatives:
        for module in modules:
            task = self.get_task(module)
            for service in task.provides:
                self.set_alternative(service, task.name)

    def load(self, modules):
        """
        Load modules and their dependencies (if not already loaded)

        Args:
            modules (list): List of modules to load.

        Raises:
            FileExistsError: Target modules (and all their dependencies)
                are already loaded, so there is nothing to do.
        """
        mlist = ModuleList(self.handle)
        needed_modules = [x for x in self.solve(modules) if x not in mlist]

        # Ensure explicitly requested modules are the current alternatives
        self._set_all_alternatives(needed_modules)

        if needed_modules:
            self.run(self.get_deps(needed_modules))
        else:
            raise FileExistsError(
                "All modules and their dependencies are already loaded."
            )

    def _find_removable(self, dependencies, modules_to_remove):
        """
        Given a set of modules and dependency list of all modules, find modules
        that can be removed because they no longer have any dependents.

        Args:
            dependencies (dict): Dictionary of modules to dependency list
            modules_to_remove (list): List of modules to remove

        Returns:
            list: modules that can be safely removed (including original list)
        """
        dependents = {}
        for dependent, reqs in dependencies.items():
            for req in reqs:
                if req not in dependents:
                    dependents[req] = set()
                dependents[req].add(dependent)

        # Start with the items we're told to remove
        removed_items = set(modules_to_remove)
        newly_removable = []

        # Keep track of items to check in this iteration
        modules_to_check = set(modules_to_remove)

        while modules_to_check:
            next_modules_to_check = set()

            for removed_item in modules_to_check:
                # Find all dependencies of the removed item
                # (items it depended on)
                if removed_item in dependencies:
                    for dependency in dependencies[removed_item]:
                        # Skip if this dependency is already being removed
                        if dependency in removed_items:
                            continue

                        # Check if this dependency still has other items
                        # depending on it
                        remaining_dependents = (
                            dependents.get(dependency, set()) - removed_items
                        )

                        # If no remaining dependents, it can be removed
                        if not remaining_dependents:
                            removed_items.add(dependency)
                            newly_removable.append(dependency)
                            next_modules_to_check.add(dependency)

            modules_to_check = next_modules_to_check

        # Add newly removed items to list of items to remove
        modules_to_remove.extend(newly_removable)

        # Discard elements from dependents if they are being removed
        for name in modules_to_remove:
            for deps in dependents.values():
                # discard both the name to remove and the real name
                # of the task in case they are different
                # (e.g. sched vs sched-simple):
                deps.discard(name)
                deps.discard(self.get_task(name).name)

        # Raise an error if any removed modules still have dependents
        for name in modules_to_remove:
            if dependents.get(name):
                raise ValueError(
                    f"{name} still in use by " + ", ".join(dependents[name])
                )

        return modules_to_remove

    def _solve_modules_remove(self, modules=None):
        """Solve for a set of currently loaded modules to remove"""
        mlist = ModuleList(self.handle)
        all_modules = [x for x in mlist if self.has_task(x)]

        if not modules or "all" in modules:
            # remove all configured modules
            modules = all_modules
        else:
            # Check if all specified modules are loaded:
            for module in modules:
                if not mlist.lookup(module):
                    raise ValueError(f"module {module} is not loaded")

        modules = self._find_removable(self.get_requires(all_modules), modules)

        # Compute reverse precedence graph of modules to remove so that
        # they can be removed in reverse order of load:
        deps = self.get_deps(modules)
        rdeps = defaultdict(set)
        for name, deplist in deps.items():
            for mod in deplist:
                mod = mlist.lookup(mod)
                rdeps[mod].add(name)

        # Convert module names to ModuleRemove tasks:
        tasks = set()
        for service in modules:
            name = mlist.lookup(service)
            if name is not None:
                tasks.add(name)

        # filter out tasks from rdeps that are not slated for removal
        deps = {}
        for name in tasks:
            deps[name] = {x for x in rdeps[name] if x in tasks}

        return list(tasks), deps

    def set_remove(self, modules=None):
        """Register a set of modules to remove or remove all modules"""
        if modules is None:
            mlist = ModuleList(self.handle)
            modules = [x for x in mlist if self.has_task(x)]

        # When removing modules, always set available alternatives to
        # the specific modules being requested to remove. This prevents
        # non-loaded but default alternatives from appearing in get_deps()
        # later:
        self._set_all_alternatives(modules)

        for module in modules:
            task = self.get_task(module)
            task.set_remove()
            self.add_active_task(task)

    def remove(self, modules):
        """Remove loaded modules"""
        tasks, deps = self._solve_modules_remove(modules)
        [self.get_task(x).set_remove() for x in deps.keys()]
        self.run(deps)
