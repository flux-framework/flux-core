.. _debug:

###############
Debugging Notes
###############

***********************
source tree executables
***********************

`libtool <https://www.gnu.org/software/libtool/manual/libtool.html>`_ is
in use, so :option:`libtool e` trickery is needed to launch a tool against
an actual compiled executable.  Command front ends further complicate this.

.. note::
  :option:`libtool e` is shorthand for :option:`libtool --mode=execute`.

Example: run a built-in sub-command under GDB

.. code-block::

  $ libtool e gdb --ex run --args src/cmd/flux version

Example: run an external sub-command under GDB

.. code-block::

  $ src/cmd/flux /usr/bin/libtool e gdb --ex run --args src/cmd/flux-keygen

Example: run a broker module separately from the broker under GDB

.. code-block::

  $ src/cmd/flux /usr/bin/libtool e gdb --ex run --args src/broker/flux-module-exec heartbeat

Example: run the broker under GDB

.. code-block::

  $ src/cmd/flux start --wrap=libtool,e,gdb,--ex,run

Example: run the broker under valgrind

.. code-block::

  $ src/cmd/flux start --wrap=libtool,e,valgrind

***************
message tracing
***************

Example: trace messages sent/received by a command

.. code-block::

  $ FLUX_HANDLE_TRACE=t flux kvs get foo

Example: trace messages sent/received by two broker modules

.. code-block::

  $ flux module trace --full content kvs

Example: trace messages sent/received by this broker on the overlay network

.. code-block::

  $ flux overlay trace --full

***********
CI failures
***********

Failures that occur only in a github CI workflow can be directly examined
with `tmate access <https://mxschmitt.github.io/action-tmate/>`_.

In flux-core, the tmate action can be enabled by restarting the workflow
and selecting the "Enable debug logging" checkbox.

For other framework projects, the tmate action can be temporarily patched
into the workflows config as in the diff below for the macos workflow:

.. code-block::

   diff --git a/.github/workflows/main.yml b/.github/workflows/main.yml
   index c65ea7f08..fba80d919 100644
   --- a/.github/workflows/main.yml
   +++ b/.github/workflows/main.yml
   @@ -130,6 +130,9 @@ jobs:
          run: make check -j4 TESTS=
        - name: check what works so far
          run: scripts/check-macos.sh
   +    - name: tmate
   +      if: failure()
   +      uses: mxschmitt/action-tmate@v3


Push that temporary change to a personal fork, then find the ssh address by
examining the action output.

*****************************
running tests as Flux jobs
*****************************

Tests in the testsuite can be run as Flux jobs to help with debugging and
testing under different conditions. This approach is particularly useful
for ensuring tests are isolated from the enclosing Flux environment, running
tests in parallel across multiple nodes, or detecting race conditions.

Example: run a single test as a Flux job

.. code-block::

  $ flux run -o pty -n1 ./t1234-test.t -d -v

This ensures the test is properly isolated from the enclosing environment,
which can be helpful for debugging environment-related issues or verifying
that a test doesn't depend on external state. The :option:`-o pty` option
allocates a pty for the job to enable colorized output from the sharness
tests.

Example: run all sharness tests in parallel as Flux jobs

.. code-block::

  $ flux bulksubmit -n1 -o pty --watch --progress --job-name={./%} ./{} -d -v ::: t*.t

This is useful for running the entire test suite (or a subset depending on
what the glob after ``:::`` matches) quickly when you have access to multiple
nodes or cores. Each test runs as a separate job using one core (cores per
test can be adjusted with :option:`-c, --cores-per-task=N`), allowing
you to leverage available resources efficiently. The :option:`--watch`
option displays live output from jobs as they execute, :option:`--progress`
shows progress and pass/fail counts, and :option:`--job-name={./%}` sets
each job name to the test file basename with the ``.t`` extension removed.

.. note::
  After running tests with ``bulksubmit``, you can list any failures with
  ``flux jobs -f failed`` and then examine the output of a specific failed
  test using ``flux job attach JOBID``.

Example: run a single test multiple times to detect race conditions

.. code-block::

  $ flux submit --cc=1-16 -o pty --watch --progress -n1 ./t1234-test.t -d -v --root={{tmpdir}}

This runs the same test 16 times simultaneously, each with one core and a
unique temporary directory. This is particularly effective for finding
race conditions or intermittent failures that only appear under concurrent
execution. Adjust the ``--cc`` range as needed for your testing requirements.
Test concurrency can be further increased by running this example under
:command:`flux start -N`, where N is greater than 1.

.. note::
  Similar to ``bulksubmit``, failures can be identified with
  ``flux jobs -f failed`` and examined with ``flux job attach JOBID``.

.. note::
  The ``-d -v`` flags enable debug mode and verbose output respectively,
  which provide more detailed information when tests fail.
