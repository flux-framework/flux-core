=======================
flux-bash-completion(7)
=======================

DESCRIPTION
===========

flux-core installs a bash completion for :man1:`flux` and its included
subcommands. Subcommands provided by other projects may add their own
bash completion by installing a drop-in file. This manual describes how
drop-in files are found and the interface they must provide.

DROP-IN FILES
=============

When :man1:`flux` completion encounters a subcommand that flux-core does
not complete itself, it searches for a file with the same name as the
subcommand. For example, the drop-in file for :program:`flux hello` is
named ``hello``. The directories in :envvar:`FLUX_BASH_COMPLETION_PATH`
are searched first, followed by the flux-core bash completion directory.
The first file found is sourced.

The file must define a shell function named ``_flux_ext_CMD``, where
``CMD`` is the subcommand name with each ``-`` and ``.`` replaced by
``_``. For example, the function for :program:`flux my-cmd` is
``_flux_ext_my_cmd``. A file is sourced only once per shell. After that,
the function is called directly.

Like a function registered with :command:`complete -F`, the function is
called with three arguments:

``$1``
  The :man1:`flux` subcommand name.

``$2``
  The word being completed.

``$3``
  The word before the word being completed.

The function sets ``COMPREPLY`` to the list of possible completions.  The
standard bash variables ``COMP_WORDS`` and ``COMP_CWORD`` are available
to find other words on the command line, such as sub-subcommands. Only
the function knows which of its options take a value, so it must parse
these words itself.

If the bash-completion package is installed, ``$2`` and ``$3`` are not
split on ``=``, so ``$2`` may be ``--option=value``. In contrast,
``COMP_WORDS`` is split on the characters in ``COMP_WORDBREAKS``, which
usually include ``=``. Use the bash-completion command
:command:`_get_comp_words_by_ref -n = words cword` to get words that are
split the same way as ``$2`` and ``$3``.

Drop-in files are subject to these rules:

- Completions provided by flux-core take precedence. A drop-in file cannot
  replace the completion of a command that flux-core completes.

- Subcommand names must start with a letter or digit and contain only
  letters, digits, ``.``, ``_``, and ``-``. Other names are not searched.

- The file is sourced from inside a shell function, so variables created
  with :command:`declare` or :command:`local` at the top level of the file
  are local to that function. Use plain assignment for global variables.

- Other shell functions defined by the flux completion are internal and
  may change without notice.

EXAMPLE
=======

This drop-in file completes :program:`flux hello`, which takes a
``--lang=LANG`` option followed by subcommand ``greet`` or ``wave``.
It requires the bash-completion package.

.. code-block:: bash

  _flux_ext_hello()
  {
      local cmd=$1 cur=$2 prev=$3
      local words cword i subcmd=""

      _get_comp_words_by_ref -n = words cword

      # Find the first word after the subcommand name that is not
      # an option or the value of --lang
      for ((i = 1; i < cword; i++)); do
          [[ ${words[i]} == "$cmd" ]] && break
      done
      for ((i++; i < cword; i++)); do
          case ${words[i]} in
          --lang)
              ((i++))
              ;;
          -*)
              ;;
          *)
              subcmd=${words[i]}
              break
              ;;
          esac
      done

      # Complete the value of --lang VALUE or --lang=VALUE
      if [[ $cur == --lang=* ]]; then
          prev=--lang
          cur=${cur#*=}
      fi
      if [[ $prev == --lang ]]; then
          COMPREPLY=( $(compgen -W "en fr" -- "$cur") )
          return
      fi

      case $subcmd in
      greet)
          COMPREPLY=( $(compgen -W "--name --loud" -- "$cur") )
          ;;
      wave)
          COMPREPLY=( $(compgen -W "--hand" -- "$cur") )
          ;;
      *)
          COMPREPLY=( $(compgen -W "greet wave --lang --help" -- "$cur") )
          ;;
      esac
  }

INSTALLATION
============

Install drop-in files to the flux-core bash completion directory, which
can be queried using :linux:man1:`pkg-config`:

.. code-block:: console

  $ pkg-config --variable=fluxbashcompletiondir flux-core
  /usr/share/flux/bash-completion

To test a drop-in file before it is installed, add its directory to
:envvar:`FLUX_BASH_COMPLETION_PATH`.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux`, :man7:`flux-environment`
