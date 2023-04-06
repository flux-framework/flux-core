###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import concurrent.futures
import json
import threading
from abc import ABC, abstractmethod

import flux
from flux.importer import import_path, import_plugins


class ValidatorResult:
    """Container for result or results from the JobValidator validate method"""

    def __init__(self):
        self.errnum = 0
        self.errmsgs = []

    def __str__(self):
        result = dict(errnum=self.errnum)
        if self.errmsgs:
            result["errstr"] = self.errmsg
        return json.dumps(result)

    def push_result(self, errnum, errmsg=None):
        """Add a result from one validator to a ValidatorResult

        Args:
            errnum (:obj:`int`): error number (0 for success)
            errmsg (:obj:`str`, optional): An optional error message for a
                failed result.
        """
        if errnum > self.errnum:
            self.errnum = errnum
        if errmsg and errmsg not in self.errmsgs:
            self.errmsgs.append(errmsg)

    @property
    def errmsg(self):
        """str: comma-separated string list of all error messages"""
        return ", ".join(self.errmsgs)

    @property
    def success(self):
        """bool: True if job validated successfully, False otherwise"""
        return self.errnum == 0


class ValidatorJobInfo:
    """An instance of a Flux job specification used by job validators

    Attributes:
        jobspec (dict): Submitted jobspec in Python dict form
        userid (int): Submitting user id
        flags (int): Job flags supplied during submission
        urgency (int): Job urgency
        flux (:obj:`Flux`): On-demand, per-thread Flux handle

    """

    #  Thread-local storage, used to provide an on-demand, per-thread
    #    Flux handle for validators that require one.
    tls = threading.local()

    def __init__(self, jobinfo):
        self.jobinfo = jobinfo

    def __getattr__(self, attr):
        if attr == "flux":
            #  Allow one flux handle per thread, created on demand:
            try:
                return self.tls.flux
            except AttributeError:
                self.tls.flux = flux.Flux()
                return self.tls.flux
        else:
            #  Return components of the validate request as attrs
            return self.jobinfo[attr]


class ValidatorPlugin(ABC):  # pragma: no cover
    """Base class for Validator Plugins"""

    def __init__(self, parser):
        """Initialize a ValidatorPlugin"""

    def configure(self, args):
        """Configure a ValidatorPlugin. Run after argparse.parse_args()

        Args:
            args (:obj:`Namespace`): The resulting Namespace after calling
            argparse.parse_args()
        """

    @abstractmethod
    def validate(self, job):
        """Validate a job. A ValidatorPlugin must implement this method

        If a job fails validation, this method should either throw an
        exception, which will be caught by the calling script, or a
        ``(errnum, errmsg)`` tuple may optionally be returned, if that
        is more convenient.

        On success, this method should return nothing or explicitly::

            (0, None)

        Args:
            job (:obj:`ValidatorJobInfo`): the job to validate

        Returns:
            None or (errnum, errmsg) tuple.
        """
        raise NotImplementedError


# pylint: disable=too-many-instance-attributes
class JobValidator:
    """A plugin-based job validator class

    JobValidator loads plugins that implement the ValidatorPlugin interface
    from the 'flux.job.validator.plugins' namespace. Plugins may be configured
    at runtime by passing in a ``--plugins=LIST`` option

    """

    default_validators = ["jobspec"]
    plugin_namespace = "flux.job.validator.plugins"

    def __init__(self, argv, pluginpath=None, parser=None):

        self.validators = []
        self.executor = None

        if pluginpath is None:
            pluginpath = []

        #  Setup parser so we can parse --plugin and plugins can attach
        #   their own options
        if parser is None:
            parser = argparse.ArgumentParser(
                formatter_class=flux.util.help_formatter(), add_help=False
            )

        self.parser = parser
        self.parser_group = self.parser.add_argument_group("Validator options")
        self.plugins_group = self.parser.add_argument_group(
            "Options provided by plugins"
        )
        self.parser_group.add_argument("--plugins", action="append", default=[])

        #  Parse provided argv, but only parse known args, save
        #   remaining arguments for plugin configuration once plugins
        #   have been selected.
        #
        args, self.remaining_args = self.parser.parse_known_args(argv)
        if not args.plugins:
            args.plugins = self.default_validators
        else:
            args.plugins = [x for xs in args.plugins for x in xs.split(",")]

        #  Load all available validator plugins:
        self.plugins = import_plugins(self.plugin_namespace, pluginpath)
        self.args = args

    def start(self):
        """Select and configure plugins, start executor, etc."""

        #  Now configure selected plugins:
        for name in self.args.plugins:
            if name not in self.plugins:
                try:
                    self.plugins[name] = import_path(name)
                except:
                    raise ValueError(f"validator plugin '{name}' not found")
            plugin = self.plugins[name].Validator(parser=self.plugins_group)
            self.validators.append(plugin)

        #  Parse remaining args and pass result to plugins now that all
        args = self.parser.parse_args(self.remaining_args)
        for validator in self.validators:
            validator.configure(args)

        self.executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=len(self.validators),
        )
        return self

    def validate(self, jobinfo):
        """Validate jobinfo using all loaded validators

        Args:
            jobinfo (:obj:`ValidatorJobInfo`): A ValidatorJobInfo object which
                describes the job to be validated.

        Returns:
            :obj:`ValidatorResult`

            If any one validator plugin fails, then result will indicate
            failure.
        """

        # Empty jobinfo is considered success
        if jobinfo is None:
            return (0, None)

        if isinstance(jobinfo, str):
            jobinfo = json.loads(jobinfo)
        job = ValidatorJobInfo(jobinfo)

        futures = [
            self.executor.submit(validator.validate, job)
            for validator in self.validators
        ]

        result = ValidatorResult()
        for fut in concurrent.futures.as_completed(futures):
            try:
                res = fut.result()
                if res is not None:
                    result.push_result(*res)
            except (ValueError, TypeError, EnvironmentError) as exc:
                result.push_result(1, str(exc))
                for future in futures:
                    future.cancel()
            except concurrent.futures.CancelledError:
                pass
        return result
