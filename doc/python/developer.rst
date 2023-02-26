Flux Python Development
=======================

These are notes for Python developers of Flux!

Development Environment
-----------------------

Note that we have a provided Dev Containers (VSCode) environment that you can
use to develop in. Instructions for using it are `documented in the repository README <https://github.com/flux-framework/flux-core/#vscode-dev-containers>`_. We recommend you use this environment (or the same container it 
is built from in ``.devcontainer`` for consistent results.

Linting
-------

We have added support for basic automation of linting tasks, which you are
free to use or not. If you choose to not use these tools locally, they
will be checked in the continuous integration (and you can make changes 
accordingly). Python 3.6 is required for the project, as a standard. You can
also continue to use the previous older custom linting and formatting 
scripts in the ``scripts`` folder of the repository.

.. note:: The ``flux`` package which is used to interact with Flux *cannot* yet be installed into a virtual environment with pip or conda.

Install the dependencies for linting (note these will already be installed in 
the dev containers environment and you can skip this step):

.. code-block:: console

    $ pip install --force-reinstall -r scripts/requirements-dev.txt

This includes linting tools black, isort, mypy, and flake8, and pre-commit
to automate running them. You can then (optionally) install the hook:

.. code-block:: console

    $ pre-commit install --hook-type pre-commit

You can optionally run pre-commit at any time like so:

.. code-block:: console

    $ pre-commit run --all-files
    
You'll see (on the first run) a bunch of errors, but likely the tools will
fix them for you, and a new run will be much cleaner, with only a few manual
fixes necessary:

.. code-block:: console

    Check for added large files..............................................Passed
    Check for case conflicts.................................................Passed
    Check docstring is first.................................................Passed
    Check that scripts with shebangs are executable..........................Passed
    Fix End of Files.........................................................Passed
    Trim Trailing Whitespace.................................................Passed
    Mixed line ending........................................................Passed
    black....................................................................Passed
    mypy.....................................................................Passed

And that's it! The check run in the CI is equivalent to here, so you should
always be able to reproduce failures. And if you install the pre-commit hook,
you can largely prevent them by always catching them before commit.

Writing Python Commands
-----------------------

The Flux command design is done in a way that some commands are generated
from the C code, and others are helpers that come from Python. 
If you need to add a new command (and want it to show up) there are some tricks you need to know.
This short guide will help you on this journey! 🏔️

Locations
~~~~~~~~~

Flux commands can be found under ``src/cmd``. You'll notice many files with
a prefix "flux-". By default, any file with this prefix will be discovered
and available to the user as a flux command. As an example, ``flux-run.py`` will
be available as ``flux run`` and ``flux-job.c`` will be available as ``flux job``. 
This shows that we have a mix of C derived and Python derived commands. if you are
interested in how this works, look at ``src/cmd/cmdhelp.c``.

Let's now say that we add a new command ``flux-foo.py``. We would expect to compile
Flux and see it available in ``flux help``, right? Wrong!
While the command will technically work as ``flux foo``, it won't show up in the help,
and there are a few tricks to getting that working, discussed next.

Adding a New Command
~~~~~~~~~~~~~~~~~~~~

The command generation works as follows:

 - A new command should be a Python file added to ``src/cmd`` named like ``flux-<command-name>.py``.
 - The command can use (and write new) shared base classes that are found under ``src/bindings/python/flux/cli`` 
 - The script ``etc/gen-cmdhelp.py`` is run during build, and it looks for entries in ``doc/manpages.py``
 - This means you need to add line entries for your new commands including the .rst file path relative to doc, e.g.:
 
  .. code-block:: python

    ('man1/flux-submit', 'flux-submit', 'submit a job to a Flux instance', [author], 1),
    ('man1/flux-run', 'flux-run', 'run a Flux job interactively', [author], 1),
    ('man1/flux-bulksubmit', 'flux-bulksubmit', 'submit jobs in bulk to a Flux instance', [author], 1),
    ('man1/flux-alloc', 'flux-alloc', 'allocate a new Flux instance for interactive use', [author], 1),
    ('man1/flux-batch', 'flux-batch', 'submit a batch script to Flux', [author], 1),

 - And of course, the dependency to that means actually writing the file! Python commands are in "man1."
 - Update ``doc/Makefile.am`` (MAN1_FILES_PRIMARY) and ``src/cmd/Makefile.am`` (dist_fluxcmd_SCRIPTS) with your new file.
 - Try to use shared logic whenever possible! E.g., ``doc/man1/common`` has common snippets.
 - The script generates ``etc/flux/help.d/core.json`` where you can sanity check the output.
 
Since these changes need to be compiled into the source code, be careful that if you re-generate that json
file, you do a ``make clean`` to ensure that the cmd and etc directories are rebuilt. It's best to
start from scratch with ``make clean`` as (in this writer's experience), partial cleans don't always work.

Updating a Command
~~~~~~~~~~~~~~~~~~

Updating a command is much easier, as the documentation .rst file will already
exist! This means that (depending on your updates) you should update this file,
do a build from scratch, and check that:

 - The command functions as you would expect.
 - It shows up in help, and the help documentation is comprehensive and correct.
 - Tests for the command are updated or added.

Happy Developing!
