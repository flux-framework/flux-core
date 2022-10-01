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

Happy Developing!
