.. flux-help-description : Get current process scope

=============
flux-scope(1)
=============


SYNOPSIS
========

**flux** **scope**


DESCRIPTION
===========

flux-scopes(1) prints the current process scope.  This command may be
useful to quickly determine the context of how a process is running.
The potential outputs are:

none
    The process is not running under flux.

system instance
    The process is running under the system instance.  For example,
    if you submitted ``flux scope`` as a job to the system instance:

       $ flux mini submit flux scope

    the output would be "system instance".   

initial program
    The process is the initial program running a non-system instance
    of flux.  For example, if ``flux scope`` were executed as the
    initial program for a flux sub-instance:

       $ flux mini submit flux start flux scope

    the output would be "initial program".

    As another example, "initial program" would be output if ``flux
    scope`` were executed as the initial program under a test
    instance.
    
       $ flux start --test-size=4 flux scope

job
    The process is a job running under a non-system instance of flux.
    For example, if ``flux scope`` were submitted as a job to a flux
    sub-instance:

       $ flux mini submit flux start flux mini run flux scope

    the output would be "job".


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-start`, :man1:`flux-mini`
