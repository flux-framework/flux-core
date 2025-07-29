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

Example: temporarily patch a failing macos workflow:

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
