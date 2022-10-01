Flux Python Development
=======================

These are notes for Python developers of Flux!


Linting
-------

We have added support for basic automation of linting tasks, which you are
free to use or not. If you choose to not use these tools locally, they
will be checked in the continuous integration (and you can make changes 
accordingly).

.. note:: The ``flux`` package which is used to interact with Flux *cannot* yet be installed into a virtual environment with pip or conda.

To install the requirements, we recommend creating a virtual environment,
and then installing:

.. code-block:: console

    $ python -m venv env
    $ source env/bin/activate

Then install the dependencies for linting:

.. code-block:: console

    $ pip install -r scripts/requirements-dev.txt

This includes linting tools black, isort, mypy, and flake8, and pre-commit
to automate running them. You can then install the hook:

.. code-block:: console

    $ pre-commit install --hook-type pre-commit

You can optionally run pre-commit at any time like so:

.. code-block:: console

    $ pre-commit run --all-files
    
You'll see (on the first run) a bunch of errors, but likely the tools will
fix them for you, and a new run will be much cleaner, with only a few manual
fixes necessary:

.. code-block:: console

    check json...........................................(no files to check)Skipped
    check yaml...........................................(no files to check)Skipped
    fix end of files.........................................................Passed
    trim trailing whitespace.................................................Passed
    mixed line ending........................................................Passed
    black....................................................................Passed
    isort....................................................................Passed
    flake8...................................................................Passed
    mypy.....................................................................Passed


And that's it! The check run in the CI is equivalent to here, so you should
always be able to reproduce failures. And if you install the pre-commit hook,
you can largely prevent them by always catching them before commit.

Development Environment
-----------------------

To easily develop with Python, you can use a prebuilt container. Build as follows
from the root of the Flux repository:

.. code-block:: console

    $ docker build -t ghcr.io/flux-framework/flux-ubuntu -f etc/docker/ubuntu/Dockerfile .

And then shell inside, either without binding your code:

.. code-block:: console

    $ docker run -it ghcr.io/flux-framework/flux-ubuntu

or with binding your code (for interactive development):

.. code-block:: console

    $ docker run -v $PWD:/code -it ghcr.io/flux-framework/flux-ubuntu

In which case you should build things once more:

.. code-block:: console

    $ ./autogen.sh && make && make install
        
and then you can add the site-packages to your PYTHONPATH, make changes on your
local machine, and run ``make install`` to install and then test (this will move
the Python files from your local directory into the final install destination).

.. code-block:: console

    $ export PYTHONPATH=/usr/local/lib/flux/python3.10:/usr/local/lib/python3.10/site-packages


Happy Developing!
