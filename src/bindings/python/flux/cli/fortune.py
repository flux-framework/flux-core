##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import random
import sys
from datetime import datetime

import flux.util
from flux.cli import base

# Choice of decorating symbols
symbols = ["@", "*", "**", "!", "$", "%", "^", "O", "o", "|", "x", "8", "*", "{*}", "-"]

# Choices of colors to print
colors = [
    "\033[91m %s %s %s\033[00m",  # red
    "\033[92m %s %s %s\033[00m",  # green
    "\033[93m %s %s %s\033[00m",  # yellow
    "\033[95m %s %s %s\033[00m",  # magenta
    "\033[94m %s %s %s\033[00m",  # blue
    "\033[96m %s %s %s\033[00m",  # cyan
    "\033[97m %s %s %s\033[00m",
]  # gray


class FortuneCmd(base.MiniCmd):
    """
    Surprise the user with some beautiful, hidden Flux fortunes and art!

    Usage: flux fortune

    flux fortune -c all            # this is the default
    flux fortune -c valentines     # show valentine fortune
    flux fortune -c art            # show art
    flux fortune -c facts          # show learning facts / tidbits
    flux fortune -c fun            # show fun fortune
    """

    @staticmethod
    def create_parser(
        prog, usage=None, description=None, exclude_io=False, add_help=True
    ):
        """
        Create a largely empty parser for flux fortune (no arguments or exposed)
        """
        if usage is None:
            usage = f"{prog} [OPTIONS...] COMMAND [ARGS...]"

        parser = argparse.ArgumentParser(
            prog=prog,
            usage=usage,
            description=description,
            formatter_class=flux.util.help_formatter(),
        )
        parser.add_argument(
            "-c",
            "--category",
            choices=["all", "valentines", "fun", "facts", "art"],
            default="all",
            help="Choose the category of fortunes to display.",
        )
        return parser

    def generate_fortune(self, args):
        """
        Generate the fortune, meaning:

        1. Choose to print a fortune (a) or the (rare) ascii art.
        2. If a, choose a color and print.
        3. If b, print the ascii and exit.
        """
        # Derive fortune based on category
        if args.category == "all":
            return self.random_fortune()

        # Request for facts
        if args.category == "facts":
            return self.show_fortune(facts)

        # Request for ascii art
        if args.category == "art":
            return self.show_art()

        # Request for a valentine
        if args.category == "valentines":
            return self.show_fortune(valentines)

        # Otherwise show a fun fortune
        self.show_fortune(fortunes)

    def show_art(self):
        """
        Show ascii art.
        """
        print(random.choice(art))

    def random_fortune(self):
        """
        A random fortune can be art, fun, valenties, or factoid.
        """
        # 1% chance to print ascii art, no matter what
        if random.uniform(0, 1) <= 0.01:
            return self.show_art()

        # Beyond that, 80% of the time is fact
        if random.uniform(0, 1) <= 0.80:
            return self.show_fortune(facts)

        # Otherwise we show fun or valentines
        # If it's within 3 weeks of Valentines...
        self.check_valentines()
        self.show_fortune(fortunes)

    def show_fortune(self, listing):
        """
        Randomly select a fortune from a list and colorize.
        """
        # Otherwise, choose a color and a fortune...
        color = random.choice(colors)
        s = random.choice(symbols)
        fortune = random.choice(listing)
        print(color % (s, fortune, s))

    def check_valentines(self):
        """
        Check if we are within a few weeks of Valentine's Day
        """
        global fortunes
        global valentines
        now = datetime.now()

        # End of January or start of February
        is_soon = (now.month == 1 and now.day > 29) or (
            now.month == 2 and now.day <= 14
        )
        if not is_soon:
            return

        fortunes += valentines

    def main(self, args):
        self.generate_fortune(args)
        sys.exit(self.exitcode)


# Valentines fortunes (2 weeks up to Valentine's day)
valentines = [
    "Roses are red, violets are blue, if you want graph-based scheduling, Flux is for you! <3",
    "Roses are red, violets are blue, all of my jobs, submit to you! <3",
    "Roses are red, violets are blue, you are my favorite job manager queue! <3",
    """
Sung to the tune of Monty Python's "Bruce's Philosopher Song":

Slurm, Slurm, on this I'm firm:
your interface resembles the excretions of a worm
Loadleveler took forever-er
but waiting for P-O-E gave you time to pee
jsrun is no j-s-fun
but we paid a lot for a system and so they gave us one
And of Condor we couldn't be fonder -
when run right, it kept the professor's office warm all night
""",
    """
Oh Flux, my darling, my heart doth flutter
With every submit you help me utter
Your unidirectional flow is so sublime
It simplifies my code and saves me time

My love for you is like an unchanging store
Always at the ready, forevermore
Your Python bindings make me swoon
Together, we make such a lovely tune

Oh Flux, you make my programming heart sing
Your architecture is a beautiful thing
With you by my side, I can conquer all
Happy Valentine's Day, my sweet, Flux, my all.

Citation:
OpenAI, 2023, Feb. 13, ChatGPT response to the prompt
"Write a valentine's day poem about flux framework that rhymes".
https://chat.openai.com
""",
]

# Facts about Flux
facts = [
    """
Flux can be started as a parallel job.  This is how flux-batch(1) and
flux-alloc(1) work!  Within one of these "subinstances" of Flux, you
(or your batch script) have access all the features of Flux without bothering
the system or parent Flux instance.  Do your worst - it is your personal
sandbox!
""",
    """
Other resource managers and MPI launchers can start Flux instances the
same way they launch MPI.  This is why workflows "coded to Flux" are
portable to many environments.
""",
    """
flux-submit(1) and flux-run(1) both start one parallel program.  The difference
is flux-submit(1) prints the job ID and exits immediately, while flux-run(1)
doesn't exit until the job has completed.
""",
    """
All jobs run within a Flux subinstance started by flux-batch(1) or
flux-alloc(1) look like *one* job to the Flux accounting system.
""",
    """
The system prolog/epilog do not run between jobs in a Flux subinstance
started by flux-batch(1) or flux-alloc(1).
""",
    """
A Flux subinstance started by flux-batch(1) or flux-alloc(1) has the same
capabilities as the system level Flux instance.  Unlike legacy systems that
offer a simpler "step scheduler" in batch jobs, the Flux subinstance is an
identical copy of Flux running on a resource subset.  This has been called
"fractal scheduling".
""",
    """
So Flux can start Flux, and the second Flux is identical to the first Flux,
so can *that* Flux start Flux?

"It's turtles all the way down." --Dong Ahn
""",
    """
flux-top(1) lets you navigate with arrow keys to your batch jobs or allocations
and display the jobs running within them.  Since you can nest Flux as deep
as you like, you can think of flux-top(1) as a browser for the job hieararchy.
""",
    """
Flux commands look for an environment variable FLUX_URI to determine which
Flux instance to connect to.  If it's not set, they try to connect to the
system instance of Flux.
""",
    """
flux-proxy(1) establishes a connection to a remote Flux instance, then
starts a shell in which Flux commands refer to that instance.  The connection
is dropped when the shell exits.
""",
    """
flux-uri(1) can resolve a jobid to its remote URI.  It can even resolve
Slurm and LSF job IDs when Flux is running in a non-native environment.
""",
    """
flux-overlay(1) can pretty-print a view of the Flux overlay network in your
batch job or allocation. Try this:

$ flux alloc -N16 --broker-opts=-Stbon.topo=kary:4
$ flux overlay status
$ flux alloc -N16 --broker-opts=-Stbon.topo=binomial
$ flux overlay status
$ flux alloc -N16 --broker-opts=-Stbon.topo=kary:1
$ flux overlay status
$ exit
$ exit
$ exit

You just ran three Flux instances nested like Matryoshka dolls, each with a
unique overlay network topology!
""",
    """
To get a quick summary of the resources available within a Flux instance:
   $ flux resource info
   16 Nodes, 96 Cores, 16 GPUs
""",
    """
To peruse Flux documentation, visit https://flux-framework.readthedocs.io
""",
    """
flux-version(1) reports the version of flux-core that you're running.
Visit https://flux-framework.org/releases/ for release notes.
For each release notes item, you'll find a link to the corresponding pull
request (change proposal) on github.
""",
]

# Fun fortunes
fortunes = [
    "I refuse to have modem speeds without the sweet modem sounds",
    "A yawn is a silent scream for coffee.",
    "Due to a shortage of robots, our staff is composed of humans and may react unpredictably when abused.",
    "Dear <software>, stop doing <that>. Thank you. Best, <developer>",
    "Dear Flux, I don't need your bad jokes. Thanks. Best, <flux-user>",
    "The best kind of HPC system I can imagine would smell like cookies. Forever.",
    "From __future__ import fluxisthebest",
    "Help me, I'm trapped in a container!",
    "You don’t need to worry about getting older when you’re a robot… it’s just a one digit progression in your time-stamp.",
    "I reached for my mouse... grabbed an avocado instead.",
    "One could predict supercomputer age based on bug accumulation.",
    "Yo dawg I heard you liked flux instances, so here is a flux instance to run in your flux instance!",
    "One does not simply run an HPC job on the cloud... without Flux!",
    "Flux submit, flux run... get the job done!",
    "Job in pending? Could be... a ghost in the machine! ...or you forgot to update your accounting database.",
    "A completed job is worth two in the queue.",
    "An analysis of 1000 tasks begins with one batch.",
    "A flux alloc a day keeps the sysadmins away.",
    "The cluster is always less busy on the other side.",
    "Don't count your jobs completed before they're done.",
    "The early flux user catches the queue!",
    "The early bird gets the worm, but the early user gets the supercomputer!",
    "No use crying over failed jobs... ask for help!",
    "The cluster is shining, the weather is sweet. Submit your job, to complete!",
    "If you have a Flux nightmare, you might wake up sweating in parallel.",
    "A cycle saved is a cycle earned",
    "You can't judge a program by it's source code, but you can judge the developer!",
    "Don't panic! That's the kernel's job.",
    "Keep calm and carry on - it's the kernel's job to panic.",
    "All work and no computer games makes your GPU idle.",
    "If you want to go fast, go alone. If you want to go far, go distributed.",
    "A stitch in time saves 9TB of backups.",
    "If at first you don't succeed, reboot and try again.",
    "To err is to human. To really foul things up requires a computer - Paul Ehrlich",
    "Give a scientist a program, frustrate him for a day. Teach a scientist to program, frustrate him for a lifetime!",
    "Why did the Flux instance go to the gym? To beef up its processing power!",
    "Did you know 'fuzzybunny' is a valid Flux jobid?",
]

# This can be appended with new art as desired
art = [
    """
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m.[0m[37m'[0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[36mo[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mk[0m[37m.[0m[37m.[0m[34m.[0m[34m'[0m[34m,[0m[34m'[0m[34m.[0m[34m.[0m[37m.[0m[34m.[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[36ml[0m[34ml[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m   [37m [0m[37m.[0m[37m,[0m[36m:[0m[37m:[0m[37m:[0m[37m;[0m[37m,[0m[37m'[0m[37m'[0m[37m'[0m[37m,[0m[37m,[0m[37m:[0m[36m:[0m[36m:[0m[37m;[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m'[0m[34m;[0m[34m'[0m[34m'[0m[34m,[0m[34m'[0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m.[0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m  [37m [0m[37m.[0m[37m:[0m[36mo[0m[36m:[0m[37m.[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m [0m[37m [0m[37m.[0m[37m:[0m[36mo[0m[37m;[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m,[0m[36mo[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m,[0m[36m:[0m[36mc[0m[36mc[0m[36m:[0m[37m:[0m[37m;[0m[37m;[0m[36m:[0m[36m:[0m[36mc[0m[36m:[0m[37m'[0m[37m.[0m[37m,[0m[36mo[0m[37m,[0m[37m [0m [37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m  [37m [0m[37m.[0m[36ml[0m[36ml[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36ml[0m[36ml[0m[37m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m;[0m[36mo[0m[36m:[0m[37m.[0m[36mo[0m[36mc[0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[34m.[0m[37m [0m[37m [0m[37m.[0m[37m,[0m[37m:[0m[36m:[0m[36m:[0m[37m:[0m[37m:[0m[36m:[0m[36m:[0m[36mc[0m[36m:[0m[37m,[0m[37m.[0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36md[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36ml[0m[36m:[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m'[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[36m:[0m[36m:[0m[36m:[0m[36mc[0m[37m;[0m[37m.[0m[37m,[0m[36mo[0m[36mc[0m[36mo[0m[34m:[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[36m;[0m[36mc[0m[36mc[0m[37m;[0m[37m,[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m.[0m[37m;[0m[36mc[0m[36mc[0m[37m'[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m'[0m[36mx[0m[37m'[0m[37m [0m[37m [0m[37m [0m[37m;[0m[36md[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mc[0m[36ml[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m,[0m[34ml[0m[34mc[0m[36m;[0m[34ml[0m[34mc[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[34m;[0m[34m'[0m[34m'[0m[34m,[0m[34m;[0m[34m,[0m[34m,[0m[34m;[0m[37m;[0m[36m:[0m[36mc[0m[36mc[0m[36m:[0m[37m'[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36m:[0m[36mo[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m [37m.[0m[36md[0m[37m'[0m[37m [0m  [37m,[0m[36md[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m,[0m[36md[0m[37m'[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[34m,[0m[34m,[0m[34m:[0m[34mc[0m[34mc[0m[34mc[0m[34mc[0m[34mc[0m[34m;[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m;[0m[36ml[0m[37m:[0m[37m [0m [37m [0m[37m.[0m[36mc[0m[36mo[0m[37m.[0m[37m [0m[37m [0m
[37m [0m[37m [0m[36mo[0m[36m:[0m[37m [0m[37m [0m [37m [0m[36mo[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m;[0m[36md[0m[37m.[0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[34m;[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m:[0m[36ml[0m[36mc[0m[37m;[0m[37m'[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m;[0m[36mc[0m[36ml[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m,[0m[36mo[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m,[0m[36md[0m[37m.[0m[37m [0m
[37m [0m[36mc[0m[36mO[0m[37m.[0m[37m [0m[37m [0m[37m [0m[36m:[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m'[0m[36mk[0m[37m.[0m[37m [0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m   [37m [0m[37m [0m[37m [0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[34mc[0m[37m.[0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m;[0m[36mo[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36md[0m[37m'[0m[37m [0m[37m [0m[37m [0m[37m;[0m[36md[0m[37m [0m
[37m;[0m[36mk[0m[36mk[0m[36m:[0m[37m [0m[37m [0m[36mc[0m[36mO[0m[37m;[0m[37m [0m[37m [0m[37m.[0m[36mx[0m[36ml[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m;[0m[37m.[0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m.[0m[36mo[0m[37m:[0m[37m [0m[37m [0m [37m.[0m[36md[0m[37m.[0m[37m [0m [37m [0m[36md[0m[37m;[0m
[37m [0m[36mo[0m[36m:[0m[37m [0m[37m [0m[37m.[0m[36ml[0m[36mk[0m[36m:[0m[37m [0m[37m.[0m[36mx[0m[36mO[0m[36mx[0m[37m.[0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m.[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mk[0m[37m.[0m[37m [0m[37m [0m[37m [0m[36mc[0m[36mc[0m[37m [0m[37m [0m[37m [0m[37m;[0m[36md[0m
[37m [0m[36md[0m[36m:[0m[37m [0m  [37m;[0m[36md[0m[37m [0m[37m [0m[37m [0m[37m:[0m[36mk[0m[37m,[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[36mo[0m[37m:[0m[37m [0m[37m [0m[37m [0m[36m:[0m[36ml[0m[37m [0m[37m [0m[37m [0m[37m'[0m[36mx[0m
 [36mo[0m[36mc[0m[37m [0m[37m [0m [37m,[0m[36mx[0m[37m [0m [37m [0m[37m'[0m[36mk[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m.[0m   [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [36ml[0m[37m:[0m[37m [0m[37m [0m[37m [0m[36m:[0m[36mc[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mx[0m
[37m [0m[36m:[0m[36md[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mk[0m[37m.[0m[37m [0m[37m [0m[37m [0m[36mo[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m  [37m [0m[37m.[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mx[0m[37m'[0m[37m [0m[37m [0m[37m [0m[36mo[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m,[0m[36mo[0m
[37m [0m[37m.[0m[36mk[0m[37m;[0m[37m [0m[37m [0m[37m [0m[37m;[0m[36ml[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36md[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mo[0m[36mk[0m[37m.[0m[37m [0m[37m [0m[37m,[0m[36mO[0m[37m,[0m[37m [0m [37m [0m[36mo[0m[37m;[0m
[37m [0m[37m [0m[37m;[0m[36mk[0m[37m.[0m[37m [0m[37m [0m[37m [0m[36mc[0m[36m:[0m[37m [0m [37m [0m[37m [0m[36mc[0m[36mo[0m[37m'[0m[37m [0m[37m [0m[37m [0m[37m [0m  [37m [0m[37m [0m[37m.[0m[37m;[0m[34ml[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m:[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m.[0m[36mx[0m[36mO[0m[36mk[0m[37m.[0m[37m [0m[37m;[0m[36mk[0m[36mO[0m[37m'[0m[37m [0m[37m [0m[37m'[0m[36mk[0m[37m'[0m
[37m [0m[37m [0m[37m [0m[36m:[0m[36mx[0m[37m'[0m[37m [0m[37m [0m[37m [0m[36m:[0m[36mo[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m;[0m[36mc[0m[36mc[0m[36m:[0m[37m;[0m[37m;[0m[37m;[0m[36m:[0m[36mc[0m[36ml[0m[36md[0m[34m;[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34mo[0m[37m:[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m,[0m[36mk[0m[37m:[0m[37m.[0m[37m [0m[37m [0m[36mc[0m[36mx[0m[36mo[0m[37m,[0m[37m [0m[36mc[0m[36mk[0m[36mO[0m[37m;[0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m,[0m[36mx[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mc[0m[36ml[0m[37m;[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m.[0m[37m.[0m[37m,[0m[36m:[0m[36mo[0m[34ml[0m[34m,[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m:[0m[36mc[0m[36md[0m[36m:[0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36mc[0m[36mx[0m[37m,[0m[37m [0m [37m [0m[37m.[0m[36mo[0m[37m.[0m[37m [0m [37m [0m[37m;[0m[36mk[0m[37m;[0m[37m.[0m
[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m;[0m[36ml[0m[37m;[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m,[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[37m:[0m[37m;[0m[36m:[0m[36mc[0m[34m;[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m:[0m[36mo[0m[36md[0m[37m:[0m[37m,[0m[36mc[0m[36m:[0m[37m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m.[0m[37m,[0m[36mc[0m[36ml[0m[37m,[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m'[0m[36mo[0m[37m.[0m[37m [0m [37m [0m[37m,[0m[36md[0m[37m.[0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m.[0m[36m:[0m[36mc[0m[36m:[0m[37m,[0m[37m'[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m.[0m[37m'[0m[37m,[0m[37m:[0m[36m:[0m[37m,[0m[37m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m,[0m[37m,[0m[36mx[0m[37m:[0m[36mc[0m[36ml[0m[37m.[0m[37m [0m[37m.[0m[37m;[0m[36m:[0m[36mc[0m[36mc[0m[36m:[0m[37m,[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m'[0m[36ml[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m [0m[36mc[0m[36mo[0m[37m.[0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m'[0m[37m,[0m[37m;[0m[37m;[0m[37m;[0m[37m,[0m[37m,[0m[37m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m,[0m[34m,[0m[34m,[0m[37m.[0m[37m [0m[37m.[0m[36md[0m[37m:[0m[37m.[0m[36m:[0m[36ml[0m[37m;[0m[37m.[0m[37m [0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m.[0m[37m'[0m[36mc[0m[36ml[0m[37m;[0m[37m.[0m[37m [0m [37m [0m[37m'[0m[36md[0m[36m:[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m   [37m [0m    [37m [0m[37m [0m[37m [0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m;[0m[36mo[0m[37m'[0m[37m [0m[37m.[0m[37m,[0m[36m:[0m[36m:[0m[36mc[0m[36mc[0m[36mc[0m[36mc[0m[36mc[0m[36m:[0m[37m'[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[36ml[0m[36ml[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m.[0m[37m.[0m[37m [0m[37m [0m [37m [0m[37m [0m [37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m,[0m[34m,[0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m,[0m[36m:[0m[36m:[0m[37m,[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m.[0m[37m'[0m[37m:[0m[36mc[0m[36m:[0m[37m.[0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m.[0m [37m [0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m;[0m[34ml[0m[37m.[0m [37m [0m [37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m.[0m[37m;[0m[36mc[0m[36m:[0m[36m:[0m[36mc[0m[36mc[0m[36mc[0m[36mc[0m[36mc[0m[36mc[0m[37m;[0m[37m'[0m[37m.[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m'[0m[34m.[0m[37m [0m[37m [0m[37m.[0m[34m.[0m[34m'[0m[34m'[0m[34m.[0m[37m.[0m[36ml[0m[36ml[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[34m'[0m[34m.[0m[34m.[0m[34m.[0m[34m.[0m[37m [0m[37m [0m[37m.[0m[36ml[0m[36mk[0m[36ml[0m[37m.[0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m [37m [0m[37m [0m[37m [0m [37m [0m [37m [0m[37m [0m[37m [0m[37m.[0m[36mk[0m[36mk[0m[36m:[0m[37m [0m  [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m
[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[36mc[0m[37m'[0m[37m [0m [37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m[37m [0m

Surprise! Thank you for using Flux Framework.
"""
]
