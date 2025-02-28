.. flux-help-section: other

================
flux-hostlist(1)
================

SYNOPSIS
========

**flux** **hostlist** [*OPTIONS*] [*SOURCES*]

DESCRIPTION
===========

.. program:: flux hostlist

:program:`flux hostlist` takes zero or more *SOURCES* of host lists on the
command line and concatenates them by default into a single RFC 29 Hostlist.

*SOURCES* can optionally be combined by various set operations, for example
to find the intersection, difference, or to subtract hostlists.

SOURCES
=======

Valid *SOURCES* of hostlist information include:

instance
  hosts from the broker ``hostlist`` attribute

jobid
  hosts assigned to a job.

local
  *jobid* from ``FLUX_JOB_ID`` environment variable if set, otherwise
  *instance*

avail[able]
  *instance* hostlist minus those nodes down or drained

stdin or ``-``
  read a list of hosts on stdin

hosts
  a literal RFC 29 Hostlist

The default source is *stdin*.

OPTIONS
=======

.. option:: -e, --expand

  Expand hostlist result using the defined output delimiter. Default is
  space-delimited.

.. option:: -d, --delimiter=S

  Set the delimiter for :option:`--expand` to string *S*.

.. option:: -c, --count

  Emit the number of hosts in the result hostlist instead of the hostlist
  itself.

.. option:: -n, --nth=IDS

  Output only the hosts at indices *IDS* (*-IDS* to index from the end),
  where *IDS* is a valid RFC 22 idset (e.g. '0' will return the first host,
  '0-1' will return the first and second, '-1' returns the last host).  The
  command will fail if any id in *IDS* is not a valid index.

.. option:: -F, --find=HOSTS

  Output a list of space-separated indices of *HOSTS* in the result hostlist,
  where *HOSTS* should be one or more hosts in hostlist form. The command
  will fail if any host in *HOSTS* is not found.

.. option:: -L, --limit=N

  Output at most *N* hosts (*-N* to output the last *N* hosts).

.. option:: -S, --sort

  Display sorted result.

.. option:: -u, --union, --unique

  Return only unique hosts. This implies :option:`--sort`. Without any
  other manipulation options, this is equivalent to returning the set
  union of all provided hosts. (By default, all inputs are concatenated).

.. option:: -x, --exclude=HOSTS|IDS

  Exclude all hosts in  *HOSTS* or indices in idset *IDS* from the result.
  It is not an error if any hosts or indices do not exist in the target
  hostlist.

.. option:: -i, --intersect

  Return the set intersection of all hostlists.

.. option:: -m, --minus

  Subtract all hostlists from the first.

.. option:: -X, --xor

  Return the symmetric difference of all hostlists.

.. option:: -f, --fallback

  If an argument to :command:`flux-hostlist` is a single hostname, and the
  hostname can be interpreted as a valid Flux jobid (e.g. starts with ``f``
  and otherwise contains valid base58 characters like ``fuzzy`` or ``foo1``),
  then the command may fail with::

    flux-hostlist: ERROR: job foo1 not found

  With the :option:`--fallback` option arguments that appear to be jobids that
  are not found are treated as hostnames, e.g.::
 
    $ flux hostlist --fallback foo1 foo2
    foo[1-2]

.. option:: -l, --local

  Change the default input source to "local". This is a shorter way to
  specify ``flux hostlist local``.
  
.. option:: -q, --quiet

  Suppress output and exit with a nonzero exit code if the hostlist is empty.

EXAMPLES
========

Create host file for the current job or instance if running in an initial
program:

::

  $ flux hostlist -led'\n' >hostfile

Launch an MPI program using :program:`mpiexec.hydra` from within a batch
script:

::

  #!/bin/sh
  mpiexec.hydra -launcher ssh -hosts "$(flux hostlist -le)" mpi_hello

List the hosts for one job: (Note: this is the same as
:command:`flux jobs -no {nodelist} JOBID`)

::

  $ flux hostlist JOBID
  host[1-2]

List the hosts for one job, excluding the first node:

::

  $ flux hostlist -x 0 JOBID

List the unordered, unique hosts for multiple jobs:

::

  $ flux hostlist -u JOBID1 JOBID2 JOBID3
  host[1-2,4]

Determine if any failed jobs shared common nodes:

::

  $ flux hostlist --intersect $(flux jobs -f failed -no {id})
  host4

Determine if a given host appeared the last submitted job:

::

  if flux hostlist -q -i $(flux job last) host1; then
      echo host1 was part of your last job
  fi


Count the number of currently available hosts:

::

  $ flux hostlist --count avail
  4

List all the hosts on which a job named 'myapp' ran:

::

  $ flux hostlist --union $(flux pgrep myapp)
  host[2,4-5]

List all hosts in the current instance which haven't had a job assigned
in the last 100 jobs:

::

  $ flux hostlist --minus instance $(flux jobs -c 100 -ano {id})
  host0

EXIT STATUS
===========

0
  Successful operation

1
  One or more *SOURCES* were invalid, an invalid index was specified to
  :option:`--nth`, or :option:`--quiet` was used and the result hostlist
  was empty.

2
  Invalid option specified or other command line error

RESOURCES
=========

.. include:: common/resources.rst
  
FLUX RFC
========

:doc:`rfc:spec_29`
:doc:`rfc:spec_22`


SEE ALSO
========

:man1:`flux-getattr`, :man1:`flux-jobs`, :man7:`flux-broker-attributes`
