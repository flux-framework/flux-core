###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import signal


# strsignal() is only available on Python 3.8 and up
def strsignal(signum):
    if signum == signal.SIGHUP:
        return "Hangup"
    elif signum == signal.SIGINT:
        return "Interrupt"
    elif signum == signal.SIGQUIT:
        return "Quit"
    elif signum == signal.SIGILL:
        return "Illegal instruction"
    elif signum == signal.SIGTRAP:
        return "Trace/breakpoint trap"
    elif signum == signal.SIGABRT or signum == signal.SIGIOT:
        return "Aborted"
    elif signum == signal.SIGBUS:
        return "Bus error"
    elif signum == signal.SIGFPE:
        return "Floating point exception"
    elif signum == signal.SIGKILL:
        return "Killed"
    elif signum == signal.SIGUSR1:
        return "User defined signal 1"
    elif signum == signal.SIGSEGV:
        return "Segmentation Fault"
    elif signum == signal.SIGUSR2:
        return "User defined signal 2"
    elif signum == signal.SIGPIPE:
        return "Broken pipe"
    elif signum == signal.SIGALRM:
        return "Alarm clock"
    elif signum == signal.SIGTERM:
        return "Terminated"
    # N.B. signal.SIGSTKFLT not defined until Python 3.11
    elif "SIGSTKFLT" in dir(signal) and signum == signal.SIGSTKFLT:  # novermin
        return "Stack fault"
    elif signum == signal.SIGCHLD:
        return "Child exited"
    elif signum == signal.SIGCONT:
        return "Continued"
    elif signum == signal.SIGSTOP:
        return "Stopped (signal)"
    elif signum == signal.SIGTSTP:
        return "Stopped"
    elif signum == signal.SIGTTIN:
        return "Stopped (tty input)"
    elif signum == signal.SIGTTOU:
        return "Stopped (tty output)"
    elif signum == signal.SIGURG:
        return "Urgent I/O condition"
    elif signum == signal.SIGXCPU:
        return "CPU time limit exceeded"
    elif signum == signal.SIGXFSZ:
        return "File size limit exceeded"
    elif signum == signal.SIGVTALRM:
        return "Virtual timer expired"
    elif signum == signal.SIGPROF:
        return "Profiling timer expired"
    elif signum == signal.SIGWINCH:
        return "Window changed"
    elif signum == signal.SIGIO or signum == signal.SIGPOLL:
        return "I/O possible"
    elif signum == signal.SIGPWR:
        return "Power failure"
    elif signum == signal.SIGSYS:
        return "Bad system call"
    raise ValueError
