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
]

# Facts about Flux
facts = [
    "Did you know that you can control a Flux instance on a different cluster? Check out flux proxy!",
    "Uh oh, what is going on with my nodes? 'flux resource list' will tell you the story!",
    "What's going on in my flux environment? Check out 'flux env' to see!",
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
