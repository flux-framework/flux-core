
.. _submission_directives:

SUBMISSION DIRECTIVES
=====================

The *flux mini batch* command supports submission directives
mixed within the submission script. The submission directive specification
is fully detailed in RFC 36, but is summarized here for convenience:

 * A submission directive is indicated by a line that starts with
   a prefix of non-alphanumeric characters followed by a tag ``FLUX:`` or
   ``flux:``. The prefix plus tag is called the *directive sentinel*. E.g.,
   in the example below the sentinel is ``# flux:``: ::

     #!/bin/sh
     # flux: -N4 -n16
     flux mini run -n16 hostname

 * All directives in a file must use the same sentinel pattern, otherwise
   an error will be raised.
 * Directives must be grouped together - it is an error to include a
   directive after any non-blank line that doesn't start with the common
   prefix.
 * The directive starts after the sentinel to the end of the line.
 * The ``#`` character is supported as a comment character in directives.
 * UNIX shell quoting is supported in directives.
 * Triple quoted strings can be used to include newlines and quotes without
   further escaping. If a triple quoted string is used across multiple lines,
   then the opening and closing triple quotes must appear at the end of the
   line. For example ::

     # flux: --setattr=user.conf="""
     # flux: [config]
     # flux:   item = "foo"
     # flux: """

Submission directives may be used to set default command line options for
*flux mini batch* for a given script. Options given on the *flux mini batch*
command line override those in the submission script, e.g.: ::

   $ flux mini batch --job-name=test-name --wrap <<-EOF
   > #flux: -N4
   > #flux: --job-name=name
   > flux mini run -N4 hostname
   > EOF
   ƒ112345
   $ flux jobs -no {name} ƒ112345
   test-name


