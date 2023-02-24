The ``bulksubmit`` utility allows rapid bulk submission of jobs using
an interface similar to GNU parallel or ``xargs``. The command takes
inputs on stdin or the command line (separated by ``:::``), and submits
the supplied command template and options as one job per input combination.

The replacement is done using Python's ``string.format()``, which is
supplied a list of inputs on each iteration. Therefore, in the common case
of a single input list, ``{}`` will work as the substitution string, e.g.::

    $ seq 1 4 | flux mini bulksubmit echo {}
    flux-mini: submit echo 1
    flux-mini: submit echo 2
    flux-mini: submit echo 3
    flux-mini: submit echo 4

With ``--dry-run`` ``bulksubmit`` will print the args and command which
would have been submitted, but will not perform any job submission.

The ``bulksubmit`` command can also take input lists on the command line.
The inputs are separated from each other and the command  with the special
delimiter ``:::``::

    $ flux mini bulksubmit echo {} ::: 1 2 3 4
    flux-mini: submit echo 1
    flux-mini: submit echo 2
    flux-mini: submit echo 3
    flux-mini: submit echo 4

Multiple inputs are combined, in which case each input is passed as a
positional parameter to the underlying ``format()``, so should be accessed
by index::

    $ flux mini bulksubmit --dry-run echo {1} {0} ::: 1 2 ::: 3 4
    flux-mini: submit echo 3 1
    flux-mini: submit echo 4 1
    flux-mini: submit echo 3 2
    flux-mini: submit echo 4 2

If the generation of all combinations of an  input list with other inputs is not
desired, the special input delimited ``:::+`` may be used to "link" the input,
so that only one argument from this source will be used per other input,
e.g.::

    $ flux mini bulksubmit --dry-run echo {0} {1} ::: 1 2 :::+ 3 4
    flux-mini: submit 1 3
    flux-mini: submit 2 4

The linked input will be cycled through if it is shorter than other inputs.

An input list can be read from a file with ``::::``::

    $ seq 0 3 >inputs
    $ flux mini bulksubmit --dry-run :::: inputs
    flux-mini: submit 0
    flux-mini: submit 1
    flux-mini: submit 2
    flux-mini: submit 3

If the filename is ``-`` then ``stdin`` will be used. This is useful
for including ``stdin`` when reading other inputs.

The delimiter ``::::+`` indicates that the next file is to be linked to
the inputs instead of combined with them, as with ``:::+``.

There are several predefined attributes for input substitution.
These include:

 - ``{.%}`` returns the input string with any extension removed.
 - ``{./}`` returns the basename of the input string.
 - ``{./%}`` returns the basename of the input string with any
   extension removed.
 - ``{.//}`` returns the dirname of the input string
 - ``{seq}`` returns the input sequence number (0 origin)
 - ``{seq1}`` returns the input sequence number (1 origin)
 - ``{cc}`` returns the current ``id`` from use of ``--cc`` or ``--bcc``.
   Note that replacement of ``{cc}`` is done in a second pass, since the
   ``--cc`` option argument may itself be replaced in the first substitution
   pass. If ``--cc/bcc`` were not used, then ``{cc}`` is replaced with an
   empty string. This is the only substitution supported with
   ``flux-mini submit``.

Note that besides ``{seq}``, ``{seq1}``, and ``{cc}`` these attributes
can also take the input index, e.g. ``{0.%}`` or ``{1.//}``, when multiple
inputs are used.

Additional attributes may be defined with the ``--define`` option, e.g.::

    $ flux mini bulksubmit --dry-run --define=p2='2**int(x)' -n {.p2} hostname \
       ::: $(seq 0 4)
    flux-mini: submit -n1 hostname
    flux-mini: submit -n2 hostname
    flux-mini: submit -n4 hostname
    flux-mini: submit -n8 hostname
    flux-mini: submit -n16 hostname

The input string being indexed is passed to defined attributes via the
local ``x`` as seen above.
