###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import base64
import errno
from typing import NamedTuple

from flux.future import FutureExt
from flux.idset import IDset
from flux.job import (
    EventLogEvent,
    JobException,
    JobID,
    event_wait,
    event_watch,
    event_watch_async,
    job_kvs_lookup,
)

# Log levels from shell.h for use with log_stderr_level:
LOG_QUIET = -1
LOG_FATAL = 0
# Note: LOG_ALERT=1, LOG_CRIT=2 are currently reserved in shell.h
LOG_ERROR = 3
LOG_WARN = 4
LOG_NOTICE = 5
LOG_DEBUG = 6
LOG_TRACE = 7


class Taskset:
    """
    A Taskset represents a specific set of task ranks or "all" tasks.
    Implements a minimal interface of the IDset class, mainly for
    testing if one Taskset intersects with another.

    The underlying IDset is only initialized for Taskset objects that
    do represent all tasks.

    Attributes:
        all (bool): a Taskset representing all tasks
        ids (IDset): If self.all is False, the underlying idset
    """

    def __init__(self, tasks):
        self.all = False
        if isinstance(tasks, Taskset):
            # Copy an existing Taskset
            self.all = tasks.all
            self.ids = tasks.ids.copy()
            return
        if isinstance(tasks, IDset):
            self.ids = tasks.copy()
            return
        self.ids = None
        if tasks in ("all", "*"):
            self.all = True
            return
        self.ids = IDset(tasks)

    def intersect(self, tasks):
        if not isinstance(tasks, Taskset):
            tasks = Taskset(tasks)
        if self.all:
            return Taskset(tasks)
        elif tasks.all:
            return Taskset(self)
        return Taskset(self.ids.intersect(tasks.ids))

    def __bool__(self):
        return self.all or len(self.ids) > 0

    def __str__(self):
        if self.all:
            return "all"
        return str(self.ids)


class OutputEvent(EventLogEvent):
    """
    Object representing RFC 24 Job Standard I/O data events
    Attributes:
        timestamp (float): timestamp for this event
        name (str): Name of this event: 'data'
        rank (Taskset): Set of ranks to which this event applies
        stream (str): name of output stream ("stdout", "stderr")
        eof (bool): True if this event marks EOF for stream
        data (str): output data
        dict (dict): original event as dict
    """

    def __init__(self, entry, labelio=False):
        super().__init__(entry)
        if self.name != "data":
            raise ValueError(f"event {self.name} is not a data event")

        self.rank = Taskset(self.context["rank"])
        self.stream = self.context["stream"]
        self.data = None
        self.eof = False

        if "eof" in self.context:
            self.eof = self.context["eof"]

        if "data" in self.context:
            data = self.context["data"]
            if "encoding" in self.context:
                if self.context["encoding"] == "base64":
                    data = base64.b64decode(data).decode(
                        "utf-8", errors="surrogateescape"
                    )
            if "repeat" in self.context:
                data *= self.context["repeat"]
            if labelio:
                data = [f"{self.rank}: {x}" for x in data.splitlines()]
                data = "\n".join(data) + "\n"
            self.data = data

    def render(self):
        return self.data


class LogEvent(EventLogEvent):
    """
    Object representing a RFC 24 Job "log" event
    Attributes:
        timestamp (float): timestamp for this log event
        name (str): name of this event: 'log'
        rank (Taskset): shell rank that produced the log message
        level (int): log level
        levelstr (str): log level string
        message (str): log message
        component (str): log component if available
        file (str): source file if available
        line (int); source line if available
        dict (dict): original event as dict
    """

    log_level_string = [
        "FATAL",
        "FATAL",
        "FATAL",
        "ERROR",
        " WARN",
        "",
        "DEBUG",
        "TRACE",
    ]

    @property
    def levelstr(self):
        return self.log_level_string[self.level]

    def __init__(self, entry):
        super().__init__(entry)
        # LogEvent class is only for 'log' events
        if self.name != "log":
            raise ValueError(f"event {self.name} is not a log event")
        self.level = self.context["level"]
        self.rank = Taskset(self.context["rank"])
        self.message = self.context["message"]
        self.component = self.context.get("component", None)
        self.file = self.context.get("file", None)
        self.line = self.context.get("line", -1)

    def render(self, include_file_and_line=False):
        prefix = f"flux-shell[{self.rank}]: "
        if self.levelstr:
            prefix += f"{self.levelstr}: "
        if self.component:
            prefix += f"{self.component}: "
        if include_file_and_line and self.file:
            prefix += f"{self.file}:{self.line}: "
        return prefix + self.message


class OutputHeaderEvent(EventLogEvent):
    """
    Object representing an RFC 24 header event
    Attributes:
        timestamp (float): timestamp for this event
        name (str): Name of this event: 'header'
        version (int): RFC 24 version
        stdout_count (int): expected number of stdout streams
        stderr_count (int): expected number of stderr streams
        stdout_encoding (str): default encoding for stdout
        stderr_encoding (str): default encoding for stderr
        event (dict): original event as dict
    """

    def __init__(self, entry):
        super().__init__(entry)
        if self.name != "header":
            raise ValueError(f"event {self.name} is not a header event")
        self.version = self.context["version"]
        self.stderr_count = self.context["count"]["stderr"]
        self.stdout_count = self.context["count"]["stdout"]
        self.stdout_encoding = self.context["encoding"]["stdout"]
        self.stderr_encoding = self.context["encoding"]["stderr"]


class RedirectEvent(EventLogEvent):
    """
    Object representing an RFC 24 redirect event
    Attributes:
        timestamp (float): timestamp for this event
        name (str): Name of this event: 'redirect'
        stream (str): the name of the stream being redirected
        rank (Taskset): the set of ranks being redirected
        path (str): redirect path
        dict (dict): original event as dict
    """

    def __init__(self, entry):
        super().__init__(entry)
        if self.name != "redirect":
            raise ValueError("event {self.name} is not a redirect event")
        self.stream = self.context["stream"]
        self.rank = Taskset(self.context["rank"])
        self.path = self.context["path"]

    def render(self):
        return f"{self.rank}: {self.stream} redirected to {self.path}"


class JobExceptionEvent(EventLogEvent):
    """
    Object representing a JobException event
    Attributes:
        timestamp (float): timestamp for this event
        name (str): name of this event: 'exception'
        severity (int): exception severity
        exc_type (str): exception type
        note (str): exception note
    """

    def __init__(self, entry):
        super().__init__(entry)
        if self.name != "exception":
            raise ValueError("event {self.name} is not an exception event")
        self.severity = self.context["severity"]
        self.exc_type = self.context["type"]
        self.note = self.context["note"]

    def render(self):
        result = f"job.exception: type={self.exc_type} severity={self.severity}"
        if self.note:
            result += f" {self.note}"
        return result


class JobOutput(NamedTuple):
    """
    Tuple containing job output result
    Attributes:
        stdout (str): stdout from all tasks
        stderr (str): stderr from all tasks
        log (str): log messages
    """

    stdout: str
    stderr: str
    log: str


def _parse_output_eventlog_entry(entry, labelio=False):
    """
    Parse a single output eventlog entry, returning an object of the
    appropriate type: OutputEvent, LogEvent, OutputHeaderEvent,
    RedirectEvent or JobExceptionEvent.
    """
    if entry is None:
        return None
    if isinstance(entry, EventLogEvent):
        event = entry
    else:
        event = EventLogEvent(entry)
    name = event.name
    if name == "data":
        return OutputEvent(event, labelio)
    elif name == "log":
        return LogEvent(event)
    elif name == "header":
        return OutputHeaderEvent(event)
    elif name == "redirect":
        return RedirectEvent(event)
    elif name == "exception":
        # Not technically an output event, but handle anyway
        return JobExceptionEvent(event)
    raise ValueError(f"event {name} is not a supported output event type")


def _output_eventlog_entry_decode(
    entry, stream_dict, tasks, labelio=False, log_stderr_level=LOG_TRACE
):
    """
    Decode RFC 24 output eventlog entry ``entry``, appending the result
    to the stream_dict[stream_name] list if the entry is in the set of
    requested ``tasks``. If stream_dict[stream_name] does not exist,
    the entry will be created.
    """
    if not isinstance(tasks, Taskset):
        raise ValueError("tasks argument must be a Taskset, got " + type(tasks))
    event = _parse_output_eventlog_entry(entry, labelio)

    #  Determine stream name of this event:
    stream = None
    if event.name == "data":
        if event.rank.intersect(tasks):
            stream = event.stream
    elif event.name == "log":
        if event.level <= log_stderr_level:
            stream = "stderr"
        else:
            stream = "log"
    elif event.name in "exception":
        if event.rank.intersect(tasks):
            stream = "stderr"
    elif event.name == "exception":
        stream = "stderr"
    else:
        return

    text = event.render()
    if text:
        stream_dict.setdefault(stream, []).append(text)


def _parse_output_eventlog(
    eventlog, tasks="*", labelio=False, log_stderr_level=LOG_TRACE
):
    """
    Given an eventlog, return a JobOutput tuple with stdout, stderr,
    and log streams broken out.
    """
    stream_dict = {"stdout": [], "stderr": [], "log": []}
    tasks = Taskset(tasks)

    for line in eventlog.splitlines():
        _output_eventlog_entry_decode(
            line, stream_dict, tasks, labelio, log_stderr_level
        )

    # Join lines and return result
    results = ["".join(stream_dict.get(k)) for k in ("stdout", "stderr", "log")]
    return JobOutput(*results)


def job_output(
    flux_handle,
    jobid,
    tasks="*",
    labelio=False,
    nowait=False,
    log_stderr_level=LOG_TRACE,
):
    """
    Synchronously fetch output for a job.

    If ``labelio`` is True, then each line of output will be labeled with
    the source task rank.

    If ``nowait`` is True, then currently available output is returned
    without waiting for the output eventlog to be complete (i.e. for the
    job to finish). In this case, a ``FileNotFound`` exception is raised
    if the job output evenlog does not exist (job has not yet started
    writing output), or the jobid is not valid.

    If ``nowait`` is False (the default), then this function will block
    until the job output is complete, i.e. the output eventlog has received
    EOF on all streams. In this case a ``FileNotFound`` exception will be
    raised only if the jobid is not valid.

    Log messages at or below ``log_stderr_level`` will be sent to
    stderr. All other messages will appear in the ``log`` stream. By
    default, ``log_stderr_level=LOG_TRACE``, so all messages are sent to
    ``stderr``. To separate all log messages in the ``log`` stream, set
    ``log_stderr_level=-1`` (``LOG_QUIET``).

    Args:
        flux_handle (Flux): Flux handle
        jobid (int, JobID, str): target jobid
        tasks (str): idset of task ranks to include in output (default=all)
        labelio (bool): prefix lines of output with source task rank
        log_stderr_level (int): combine log messages at or below level with
            stderr (default=LOG_TRACE)
    Returns:
        JobOutput: JobOutput tuple containing output for ``stdout``,
            ``stderr``, and ``log`` streams.
    Raises:
        :py:exc:`FileNotFoundError`: jobid does not exist or no output found
        :py:exc:`flux.job.JobException`: Job received an exception
    """
    jobid = JobID(jobid)
    if nowait:
        # Do not wait, fetch current output eventlog if available and
        # return JobOutput object
        eventlog = job_kvs_lookup(flux_handle, jobid, keys=["guest.output"])
        if eventlog is None:
            msg = f"job {jobid} does not exist or output not ready"
            raise FileNotFoundError(msg)
        return _parse_output_eventlog(
            eventlog["guest.output"], tasks, labelio, log_stderr_level
        )

    stream_dict = {"stdout": [], "stderr": [], "log": []}
    tasks = Taskset(tasks)

    #  Now block until output eventlog is ready:
    try:
        event_wait(flux_handle, jobid, "shell.init", "guest.exec.eventlog")
    except OSError:
        #  event_wait() could fail due to a job exception before the 'start'
        #  event. Check for a fatal job exception in the job eventlog
        #  and raise it as such if so. O/w, reraise the event_wait() exception.
        #
        event = event_wait(flux_handle, jobid, "exception")
        if event is not None and event.context["severity"] == 0:
            raise JobException(event) from None
        raise

    #  Output eventlog is ready, synchronously gather all output
    for event in event_watch(flux_handle, jobid, "guest.output"):
        _output_eventlog_entry_decode(
            event, stream_dict, tasks, labelio, log_stderr_level
        )

    #  Join lines and return JobOutput result
    results = ["".join(stream_dict.get(k)) for k in ("stdout", "stderr", "log")]
    return JobOutput(*results)


class JobOutputEventWatch(FutureExt):
    """
    A class for watching job output events.

    See output_watch_events_async() for full documentation.
    """

    def __init__(self, flux_handle, jobid, labelio=False, nowait=False):
        self.labelio = labelio
        self.nowait = nowait

        #  Capture when 'start' and 'finish' events have been posted to
        #  the main eventlog:
        self.started = False
        self.finished = False

        #  Capture when the end of the output eventlog has been reached
        #  (indicated by None returned from watching the eventlog)
        self.closed = False
        super().__init__(self._watch_init, JobID(jobid), flux_handle=flux_handle)

    def get_event(self, autoreset=True):
        event = self.get()
        if event is None:
            return None
        if autoreset:
            self.reset()
        return _parse_output_eventlog_entry(event, labelio=self.labelio)

    def _watch_output(self, future):
        #  Watch output events and propagate to output_watch_future.
        #
        #  Note: handle and propagate OSError from future.get_event() for
        #  reasons noted in wait_for_start_event() below.
        #
        event = None
        try:
            event = future.get_event(autoreset=False)
            if event is None:
                self.closed = True
                #  Fulfill this future with None (indicating no more data)
                #  if the 'finish' event is also posted in the main eventlog.
                #  This is done to ensure any exception has been captured.
                #
                #  If nowait is enabled, then the main eventlog may not be
                #  monitored, so assume job is finished. This may miss logging
                #  job exceptions in some cases, but it is assumed that users
                #  of the `nowait` option are using it because the main
                #  eventlog is already being monitored.
                #
                if self.nowait or self.finished:
                    self.fulfill()
                return
            self.fulfill(event)
        except OSError as exc:
            self.fulfill_error(exc.errno, exc.strerror)

        if event is not None:
            future.reset()

    def _wait_for_shell_init(self, future, jobid):
        #  Wait for the 'shell.init' event, then the output eventlog
        #  should be available.
        #
        #  Note: handle and propagate OSError from future.get_event() for
        #  reasons noted in wait_for_start_event() below.
        #
        event = None
        try:
            event = future.get_event()
        except OSError as exc:
            self.fulfill_error(exc.errno, exc.strerror)

        if event is not None and event.name == "shell.init":
            event_watch_async(
                future.get_flux(), int(jobid), eventlog="guest.output"
            ).then(self._watch_output)
            future.cancel(stop=True)

    def _wait_for_start_event(self, future, jobid):
        #  Wait for 'start' event or another error or job exception.
        #
        #  Note: future.get_event() raises OSError if the future was
        #  fulfilled with an error. An exception raised within a callback
        #  will result in flux_reactor_stop_error(3) being called, which
        #  is not the correct behavior in an async context. Instead,
        #  the error should be propagated to self so that the caller can
        #  handle it if desired.
        #
        try:
            event = future.get_event()
        except OSError as exc:
            if exc.errno == errno.ENOENT:
                # Translate strerror to more helpful string:
                exc.strerror = f"job {jobid} not found"
            self.fulfill_error(exc.errno, exc.strerror)
            return
        if event is None:
            #  If the eventlog ended without a start event, then the output
            #  eventlog can't exist, so fulfill the future with an error
            #  indicating the job never started.
            #
            #  Note: we can't use ENODATA here, since that is an expected
            #  end of data error and is ignored. Also ENOENT could be
            #  confused with the FileNotFoundError returned for invalid
            #  jobid. Therefore, use EIO, which is at least somewhat related
            #  to missing output file. More important is the message that
            #  the job never started anyway.
            #
            #  Note we also don't want to raise a JobException since an
            #  unhandled exception will terminate the reactor.
            #
            if not self.started:
                self.fulfill_error(
                    errnum=errno.EIO, errstr=f"job {jobid} never started"
                )
            return
        if event.name == "exception":
            #  Emit a JobExceptionEvent in the output since expectation is
            #  than an exception message will appear on stderr:
            #
            self.fulfill(event)
        elif event.name == "start":
            #  Note that we've seen a 'start' event and proceed to watch the
            #  exec eventlog for the 'shell.init' event:
            #
            self.started = True
            event_watch_async(
                future.get_flux(), int(jobid), eventlog="guest.exec.eventlog"
            ).then(self._wait_for_shell_init, jobid)
        elif event.name == "finish":
            self.finished = True

            #  Exceptions after 'finish' are not reported, so stop watching
            #  the main eventlog:
            future.cancel(stop=True)

            #  Fulfill this future with None (indicating no more data)
            #  if output has closed. This is done to ensure any all output
            #  and any exceptions before the finish event have been captured.
            #
            if self.closed:
                self.fulfill()

    def _watch_init(self, future, jobid):
        if self.nowait:
            #  If nowait == True, go straight to watching output and
            #  skip intermediate eventlog watches:
            event_watch_async(
                future.get_flux(), int(jobid), eventlog="guest.output"
            ).then(self._watch_output)
        else:
            #  Initialize output watcher, start with main eventlog, which is
            #  monitored for the 'start' event and also any job exceptions.
            #
            event_watch_async(future.get_flux(), int(jobid)).then(
                self._wait_for_start_event, jobid
            )


class JobOutputWatch(FutureExt):
    """
    A class for watching job output.

    See output_watch_async() for full documentation.
    """

    def __init__(
        self,
        flux_handle,
        jobid,
        labelio=False,
        log_stderr_level=LOG_TRACE,
        nowait=False,
    ):
        self.labelio = labelio
        self.log_stderr_level = log_stderr_level
        self.nowait = nowait
        super().__init__(self._watch_init, JobID(jobid), flux_handle=flux_handle)

    def _watch_init(self, future, jobid):
        #  Start watching job output events. The `output_event_watch`
        #  callback will then process these events and fulfill this Future
        #  as lines of stdout, stderr, or log output arrive.
        #
        flux_handle = future.get_flux()
        JobOutputEventWatch(
            flux_handle, jobid, labelio=self.labelio, nowait=self.nowait
        ).then(self._output_event_watch)

    def _fulfill_output(self, event):
        if event is None:
            self.fulfill((None, None))
            return
        if event.name == "data" and event.data is not None:
            self.fulfill((event.stream, event.render()))
        elif event.name in ("log", "redirect", "exception"):
            stream = "stderr"
            if event.name == "log" and event.level > self.log_stderr_level:
                stream = "log"
            self.fulfill((stream, event.render() + "\n"))

    def _output_event_watch(self, future):
        #   Output event watch callback.
        #
        #   Note: Use a try/except block here to handle the case where
        #   the underlying output_event_watch_async() future is fulfilled
        #   with an error (future.get_event() raises an OSError in this case)
        #   If an OSError is raised, then error is simply propagated to the
        #   output_watch_future (which was returned to the user)
        #
        try:
            #  Get event and only propagate events that will result in one
            #  or more lines of output. "redirect" and "exception" events
            #  result in an informational message to stderr (see getline())
            #  so they are propagated as well:
            #
            event = future.get_event()
            if (
                event is None
                or (event.name == "data" and event.data is not None)
                or (event.name == "log")
                or (event.name == "redirect")
                or (event.name == "exception")
            ):
                self._fulfill_output(event)
        except OSError as exc:
            self.fulfill_error(exc.errno, exc.strerror)

    def get_output(self):
        """
        Return a tuple of (stream, data) containing the next chunk of
        output from a JobOutputWatch Future. Possible values for stream
        include: "stdout", "stderr", or "log."

        When no more output is available, (None, None) will be returned.
        """
        result = self.get()
        self.reset()
        return result


class JobOutputWatchLines(FutureExt):
    """
    A class for watching lines of job output.

    See output_watch_lines_async() for full documentation.
    """

    def __init__(
        self,
        flux_handle,
        jobid,
        labelio=False,
        log_stderr_level=LOG_TRACE,
        nowait=False,
        keepends=False,
    ):
        self.labelio = labelio
        self.log_stderr_level = log_stderr_level
        self.nowait = nowait
        self.keepends = keepends
        super().__init__(self._watch_lines_init, JobID(jobid), flux_handle=flux_handle)

    def _watch_lines_init(self, future, jobid):
        #  Start watching job output events. The `output_event_watch`
        #  callback will then process these events and fulfill this Future
        #  as lines of stdout, stderr, or log output arrive.
        #
        flux_handle = future.get_flux()
        JobOutputWatch(
            flux_handle,
            jobid,
            labelio=self.labelio,
            log_stderr_level=self.log_stderr_level,
            nowait=self.nowait,
        ).then(self._output_watch_cb)

    def _output_watch_cb(self, future):
        #  Split data into multiple lines and fulfill future once per line
        try:
            stream, lines = future.get_output()
        except OSError as exc:
            self.fulfill_error(exc.errno, exc.strerror)
            return
        if lines is not None:
            for line in lines.splitlines(keepends=self.keepends):
                self.fulfill((stream, line))
        else:
            self.fulfill((None, None))

    def getline(self):
        """
        Return a tuple of (stream, line) for the next line of available
        output from a JobOutputWatch Future. Possible values for stream
        include: "stdout", "stderr", or "log."

        When no more output is available, (None, None) will be returned.
        """
        result = self.get()
        self.reset()
        return result


def output_event_watch_async(flux_handle, jobid, labelio=False, nowait=False):
    """
    Asynchronously get output event updates for a job.

    Returns a JobOutputEventWatch Future. Call .get_event() from the callback
    to get the next available output event from the Future. The event will
    be of type OutputEvent, LogEvent, OutputHeaderEvent, RedirectEvent, or
    JobExceptionEvent (check 'name' attribute or type to get type of event)

    If the job has a fatal exception before the 'start' event, then the
    future will be fulfilled with an error with errno=EIO and message
    'job {jobid} never started'.

    Args:
        flux_handle (Flux): Flux handle
        jobid (int, JobID, str): jobid to watch
        labelio (bool): label lines of output with source tasks (default: False)
        nowait (bool): Assume output eventlog already exists and skip watching
            precursor eventlogs.
    Returns:
        JobOutputEventWatch: JobOutputEventWatch Future
    """
    return JobOutputEventWatch(flux_handle, jobid, labelio=labelio, nowait=nowait)


def output_watch_async(
    flux_handle,
    jobid,
    labelio=False,
    log_stderr_level=LOG_TRACE,
    nowait=False,
):
    """
    Asynchronously get output data for a job.

    This function returns a JobOutputWatch Future. Use future.get_output()
    to get the available output, which returns a (stream, data) tuple,
    where stream is one of 'stdout', 'stderr', or 'log'.

    If the job receives an exception while watching output, an appropriate
    error message is emitted to the stderr stream. Similarly, redirect
    events generate a "redirected to" message on stderr.

    If the job has a fatal exception before the 'start' event, then the
    future will be fulfilled with an error with errno=EIO and message
    'job {jobid} never started'.

    Args:
        flux_handle (Flux): Flux handle
        jobid (JobID): jobid to watch
        labelio (bool): label lines of output with source tasks (default=False)
        log_stderr_level (int): emit log messages at this level or below to
            stderr instead of the "log" stream.  (default=LOG_TRACE, i.e. all
            log messages are copied to stderr)
        nowait (bool): Assume output eventlog already exists and skip watching
            precursor eventlogs.
    Returns:
        JobOutputWatch: JobOutputWatch Future
    """
    return JobOutputWatch(
        flux_handle,
        jobid,
        labelio=labelio,
        log_stderr_level=log_stderr_level,
        nowait=nowait,
    )


def output_watch_lines_async(
    flux_handle,
    jobid,
    labelio=False,
    log_stderr_level=LOG_TRACE,
    nowait=False,
    keepends=False,
):
    """
    Asynchronously get lines of output for a job.

    This function returns a JobOutputWatchLines Future. Use future.getline()
    to get the next line of output, which returns a (stream, line) tuple,
    where stream is one of 'stdout', 'stderr', or 'log'.

    If the job receives an exception while watching output, an appropriate
    error message is emitted to the stderr stream. Similarly, redirect
    events generate a "redirected to"  message to stderr.

    If the job has a fatal exception before the 'start' event, then the
    future will be fulfilled with an error with errno=EIO and message
    'job {jobid} never started'.

    Note:
        The current implementation may return a partial line as a full line
        from ``getline()`` if a partial line is written into a job output
        eventlog entry. That is, this implementation does not currently line
        buffer and wait for full lines to fulfill the JobOutputWatchLines
        future. (This may be fixed in a future release, at which time this
        note will be removed).

    Args:
        flux_handle (Flux): Flux handle
        jobid (int, JobID, str): jobid to watch
        labelio (bool): label lines of output with source task (default=False)
        log_stderr_level (int): emit log messages at this level or below to
            stderr instead of the "log" stream.  (default=LOG_TRACE, i.e. all
            log messages are copied to stderr)
        nowait (bool): Assume output eventlog already exists and skip watching
            precursor eventlogs.
        keepends (bool): If True, keep line breaks in the result.
    Returns:
        JobOutputWatchLines: JobOutputWatchLines Future
    """
    return JobOutputWatchLines(
        flux_handle,
        jobid,
        labelio=labelio,
        log_stderr_level=log_stderr_level,
        nowait=nowait,
        keepends=keepends,
    )


def output_event_watch(flux_handle, jobid, labelio=False, nowait=False):
    """
    Synchronously watch job output events via a generator.

    This function will block until the first output event is available.

    Example:
        >>> for event in output_event_watch(flux_handle, jobid):
        ...     if event.name == "data" and event.data is not None:
        ...         print(f"{event.stream}: {event.data}"

    Args:
        flux_handle (Flux): Flux handle
        jobid (int, JobID, str): jobid to watch
        labelio (bool): label lines of output with source tasks (default: False)
        nowait (bool): If True, assume output eventlog already exists and skip
            watching precursor eventlogs.
    """
    jobid = JobID(jobid)
    watcher = output_event_watch_async(
        flux_handle, jobid, labelio=labelio, nowait=nowait
    )
    event = watcher.get_event()
    while event is not None:
        yield event
        event = watcher.get_event()


def output_watch(
    flux_handle,
    jobid,
    labelio=False,
    log_stderr_level=LOG_TRACE,
    nowait=False,
):
    """
    Synchronously fetch job output via a generator.

    This function will block until job output is available.

    Example:
        >>> for stream, data in output_watch(flux_handle, jobid):
        ...     print(f"{stream}: {data}")

    Args:
        flux_handle (flux.Flux): Flux handle
        jobid (int, JobID, str): jobid to watch
        labelio (bool): label lines of output with source tasks (default:
            False)
        log_stderr_level (int): return log messages at or below this level
            to stderr instead of the "log" stream (default=LOG_TRACE, i.e.
            all log messages are sent to stderr)
        nowait (bool): If True, assume output eventlog already exists and skip
            watching precursor eventlogs.
    """
    jobid = JobID(jobid)
    watcher = output_watch_async(
        flux_handle,
        jobid,
        labelio=labelio,
        log_stderr_level=log_stderr_level,
        nowait=nowait,
    )
    stream, data = watcher.get_output()
    while data is not None:
        yield stream, data
        stream, data = watcher.get_output()


def output_watch_lines(
    flux_handle,
    jobid,
    labelio=False,
    log_stderr_level=LOG_TRACE,
    nowait=False,
    keepends=False,
):
    """
    Synchronously fetch job output lines via a generator.

    This function will block until the first line of output is available.

    Example:
        >>> for stream, line in output_watch(flux_handle, jobid):
        ...     print(f"{stream}: {line}")

    Note:
        The current implementation may return a partial line as a full
        line if a partial line is written into a job output eventlog
        entry. That is, this implementation does not currently line buffer
        and wait for full lines before returning the next ``stream, line``
        pair. This may be fixed in a future release, at which time this
        note will be removed).

    Args:
        flux_handle (flux.flux): Flux handle
        jobid (int, JobID, str): jobid to watch
        labelio (bool): label lines of output with source tasks (default:
            False)
        log_stderr_level (int): return log messages at or below this level
            to stderr instead of the "log" stream (default=LOG_TRACE, i.e.
            all log messages are sent to stderr)
        nowait (bool): If True, assume output eventlog already exists and skip
            watching precursor eventlogs.
        keepends (bool): If True, keep line breaks in the result.
    """
    jobid = JobID(jobid)
    watcher = output_watch_lines_async(
        flux_handle,
        jobid,
        labelio=labelio,
        log_stderr_level=log_stderr_level,
        nowait=nowait,
        keepends=keepends,
    )
    stream, line = watcher.getline()
    while line is not None:
        yield stream, line
        stream, line = watcher.getline()
