#!/usr/bin/env python

from __future__ import print_function
import argparse
import re
import csv
import math
import json
import logging
import heapq
from abc import ABCMeta, abstractmethod
from datetime import datetime, timedelta
from collections import Sequence, namedtuple

import six
import flux
import flux.job
import flux.util
import flux.kvs
import flux.constants


def create_resource(res_type, count, with_child=[]):
    assert isinstance(with_child, Sequence), "child resource must be a sequence"
    assert not isinstance(with_child, str), "child resource must not be a string"
    assert count > 0, "resource count must be > 0"

    res = {"type": res_type, "count": count}

    if len(with_child) > 0:
        res["with"] = with_child
    return res


def create_slot(label, count, with_child):
    slot = create_resource("slot", count, with_child)
    slot["label"] = label
    return slot


class Job(object):
    def __init__(self, nnodes, ncpus, submit_time, elapsed_time, timelimit, exitcode=0):
        self.nnodes = nnodes
        self.ncpus = ncpus
        self.submit_time = submit_time
        self.elapsed_time = elapsed_time
        self.timelimit = timelimit
        self.exitcode = exitcode
        self.start_time = None
        self.state_transitions = {}
        self._jobid = None
        self._jobspec = None
        self._submit_future = None
        self._start_msg = None

    @property
    def jobspec(self):
        if self._jobspec is not None:
            return self._jobspec

        assert self.ncpus % self.nnodes == 0
        core = create_resource("core", self.ncpus / self.nnodes)
        slot = create_slot("task", 1, [core])
        if self.nnodes > 0:
            resource_section = create_resource("node", self.nnodes, [slot])
        else:
            resource_section = slot

        jobspec = {
            "version": 1,
            "resources": [resource_section],
            "tasks": [
                {
                    "command": ["sleep", "0"],
                    "slot": "task",
                    "count": {"per_slot": 1},
                }
            ],
            "attributes": {"system": {"duration": self.timelimit}},
        }

        self._jobspec = jobspec
        return self._jobspec

    def submit(self, flux_handle):
        jobspec_json = json.dumps(self.jobspec)
        logger.log(9, jobspec_json)
        flags = 0
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug("Submitting job with FLUX_JOB_DEBUG enabled")
            flags = flux.constants.FLUX_JOB_DEBUG
        self._submit_future = flux.job.submit_async(flux_handle, jobspec_json, flags=flags)

    @property
    def jobid(self):
        if self._jobid is None:
            if self._submit_future is None:
                raise ValueError("Job was not submitted yet. No ID assigned.")
            logger.log(9, "Waiting on jobid")
            self._jobid = flux.job.submit_get_id(self._submit_future)
            self._submit_future = None
            logger.log(9, "Received jobid: {}".format(self._jobid))
        return self._jobid

    @property
    def complete_time(self):
        if self.start_time is None:
            raise ValueError("Job has not started yet")
        return self.start_time + self.elapsed_time

    def start(self, flux_handle, start_msg, start_time):
        self.start_time = start_time
        self._start_msg = start_msg.copy()
        flux_handle.respond(
            self._start_msg, payload={"id": self.jobid, "type": "start", "data": {}}
        )

    def complete(self, flux_handle):
        # TODO: emit "finish" event
        flux_handle.respond(
            self._start_msg,
            payload={"id": self.jobid, "type": "finish", "data": {"status" : 0}}
        )
        # TODO: emit "done" event
        flux_handle.respond(
            self._start_msg,
            payload={"id": self.jobid, "type": "release", "data": {"ranks" : "all", "final": True}}
        )

    def cancel(self, flux_handle):
        flux.job.RAW.cancel(flux_handle, self.jobid, "Canceled by simulator")

    def insert_apriori_events(self, simulation):
        # TODO: add priority to `add_event` so that all submits for a given time
        # can happen consecutively, followed by the waits for the jobids
        simulation.add_event(self.submit_time, lambda: simulation.submit_job(self))

    def record_state_transition(self, state, time):
        self.state_transitions[state] = time

class EventList(six.Iterator):
    def __init__(self):
        self.time_heap = []
        self.time_map = {}
        self._current_time = None

    def add_event(self, time, callback):
        if self._current_time is not None and time <= self._current_time:
            logger.warn(
                "Adding a new event at a time ({}) <= the current time ({})".format(
                    time, self._current_time
                )
            )

        if time in self.time_map:
            self.time_map[time].append(callback)
        else:
            new_event_list = [callback]
            self.time_map[time] = new_event_list
            heapq.heappush(self.time_heap, (time, new_event_list))

    def __len__(self):
        return len(self.time_heap)

    def __iter__(self):
        return self

    def min(self):
        if self.time_heap:
            return self.time_heap[0]
        else:
            return None

    def max(self):
        if self.time_heap:
            time = max(self.time_map.keys())
            return self.time_map[time]
        else:
            return None

    def __next__(self):
        try:
            time, event_list = heapq.heappop(self.time_heap)
            self.time_map.pop(time)
            self._current_time = time  # used for warning messages in `add_event`
            return time, event_list
        except (IndexError, KeyError):
            raise StopIteration()


class Simulation(object):
    def __init__(
            self,
            flux_handle,
            event_list,
            job_map,
            submit_job_hook=None,
            start_job_hook=None,
            complete_job_hook=None,
    ):
        self.event_list = event_list
        self.job_map = job_map
        self.current_time = 0
        self.flux_handle = flux_handle
        self.pending_inactivations = set()
        self.job_manager_quiescent = True
        self.submit_job_hook = submit_job_hook
        self.start_job_hook = start_job_hook
        self.complete_job_hook = complete_job_hook

    def add_event(self, time, callback):
        self.event_list.add_event(time, callback)

    def submit_job(self, job):
        if self.submit_job_hook:
            self.submit_job_hook(self, job)
        logger.debug("Submitting a new job")
        job.submit(self.flux_handle)
        self.job_map[job.jobid] = job
        logger.info("Submitted job {}".format(job.jobid))

    def start_job(self, jobid, start_msg):
        job = self.job_map[jobid]
        if self.start_job_hook:
            self.start_job_hook(self, job)
        job.start(self.flux_handle, start_msg, self.current_time)
        logger.info("Started job {}".format(job.jobid))
        self.add_event(job.complete_time, lambda: self.complete_job(job))
        logger.debug("Registered job {} to complete at {}".format(job.jobid, job.complete_time))

    def complete_job(self, job):
        if self.complete_job_hook:
            self.complete_job_hook(self, job)
        job.complete(self.flux_handle)
        logger.info("Completed job {}".format(job.jobid))
        self.pending_inactivations.add(job)

    def record_job_state_transition(self, jobid, state):
        job = self.job_map[jobid]
        job.record_state_transition(state, self.current_time)
        if state == 'INACTIVE' and job in self.pending_inactivations:
            self.pending_inactivations.remove(job)
            if self.is_quiescent():
                self.advance()

    def advance(self):
        try:
            self.current_time, events_at_time = next(self.event_list)
        except StopIteration:
            logger.info("No more events in event list, running post-sim analysis")
            self.post_verification()
            logger.info("Ending simulation")
            self.flux_handle.reactor_stop(self.flux_handle.get_reactor())
            return
        logger.info("Fast-forwarding time to {}".format(self.current_time))
        for event in events_at_time:
            event()
        logger.debug("Sending quiescent request for time {}".format(self.current_time))
        self.flux_handle.rpc("job-manager.quiescent", {"time": self.current_time}).then(
            lambda fut, arg: arg.quiescent_cb(), arg=self
        )
        self.job_manager_quiescent = False

    def is_quiescent(self):
        return self.job_manager_quiescent and len(self.pending_inactivations) == 0

    def quiescent_cb(self):
        logger.debug("Received a response indicating the system is quiescent")
        self.job_manager_quiescent = True
        if self.is_quiescent():
            self.advance()

    def post_verification(self):
        for jobid, job in six.iteritems(self.job_map):
            if 'INACTIVE' not in job.state_transitions:
                job_kvs_dir = flux.job.convert_id(jobid, "dec", "kvs")
                logger.warn("Job {} had not reached the inactive state by simulation termination time.".format(jobid))
                logger.debug("Job {}'s eventlog:".format(jobid))
                eventlog = flux.kvs.get_key_raw(self.flux_handle, job_kvs_dir + ".eventlog")
                for line in eventlog.splitlines():
                    json_event = json.loads(line)
                    logger.debug(json_event)

def datetime_to_epoch(dt):
    return int((dt - datetime(1970, 1, 1)).total_seconds())


re_dhms = re.compile(r"^\s*(\d+)[:-](\d+):(\d+):(\d+)\s*$")
re_hms = re.compile(r"^\s*(\d+):(\d+):(\d+)\s*$")


def walltime_str_to_timedelta(walltime_str):
    (days, hours, mins, secs) = (0, 0, 0, 0)
    match = re_dhms.search(walltime_str)
    if match:
        days = int(match.group(1))
        hours = int(match.group(2))
        mins = int(match.group(3))
        secs = int(match.group(4))
    else:
        match = re_hms.search(walltime_str)
        if match:
            hours = int(match.group(1))
            mins = int(match.group(2))
            secs = int(match.group(3))
    return timedelta(days=days, hours=hours, minutes=mins, seconds=secs)


@six.add_metaclass(ABCMeta)
class JobTraceReader(object):
    def __init__(self, tracefile):
        self.tracefile = tracefile

    @abstractmethod
    def validate_trace(self):
        pass

    @abstractmethod
    def read_trace(self):
        pass


def job_from_slurm_row(row):
    kwargs = {}
    if "ExitCode" in row:
        kwargs["exitcode"] = "ExitCode"

    submit_time = datetime_to_epoch(
        datetime.strptime(row["Submit"], "%Y-%m-%dT%H:%M:%S")
    )
    elapsed = walltime_str_to_timedelta(row["Elapsed"]).total_seconds()
    if elapsed <= 0:
        logger.warn("Elapsed time ({}) <= 0".format(elapsed))
    timelimit = walltime_str_to_timedelta(row["Timelimit"]).total_seconds()
    if elapsed > timelimit:
        logger.warn(
            "Elapsed time ({}) greater than Timelimit ({})".format(elapsed, timelimit)
        )
    nnodes = int(row["NNodes"])
    ncpus = int(row["NCPUS"])
    if nnodes > ncpus:
        logger.warn(
            "Number of Nodes ({}) greater than Number of CPUs ({}), setting NCPUS = NNodes".format(
                nnodes, ncpus
            )
        )
        ncpus = nnodes
    elif ncpus % nnodes != 0:
        old_ncpus = ncpus
        ncpus = math.ceil(ncpus / nnodes) * nnodes
        logger.warn(
            "Number of Nodes ({}) does not evenly divide the Number of CPUs ({}), setting NCPUS to an integer multiple of the number of nodes ({})".format(
                nnodes, old_ncpus, ncpus
            )
        )

    return Job(nnodes, ncpus, submit_time, elapsed, timelimit, **kwargs)


class SacctReader(JobTraceReader):
    required_fields = ["Elapsed", "Timelimit", "Submit", "NNodes", "NCPUS"]

    def __init__(self, tracefile):
        super(SacctReader, self).__init__(tracefile)
        self.determine_delimiter()

    def determine_delimiter(self):
        """
        sacct outputs data with '|' as the delimiter by default, but ',' is a more
        common delimiter in general.  This is a simple heuristic to figure out if
        the job trace is straight from sacct or has had some post-processing
        done that converts the delimiter to a comma.
        """
        with open(self.tracefile) as infile:
            first_line = infile.readline()
        self.delim = '|' if '|' in first_line else ','

    def validate_trace(self):
        with open(self.tracefile) as infile:
            reader = csv.reader(infile, delimiter=self.delim)
            header_fields = set(next(reader))
        for req_field in SacctReader.required_fields:
            if req_field not in header_fields:
                raise ValueError("Job file is missing '{}'".format(req_field))

    def read_trace(self):
        """
        You can obtain the necessary information from the sacct command using the -o flag.
        For example: sacct -o nnodes,ncpus,timelimit,state,submit,elapsed,exitcode
        """
        with open(self.tracefile) as infile:
            lines = [line for line in infile.readlines() if not line.startswith('#')]
            reader = csv.DictReader(lines, delimiter=self.delim)
            jobs = [job_from_slurm_row(row) for row in reader]
        return jobs


def insert_resource_data(flux_handle, num_ranks, cores_per_rank):
    """
    Populate the KVS with the resource data of the simulated system
    An example of the data format: {"0": {"Package": 7, "Core": 7, "PU": 7, "cpuset": "0-6"}}
    """
    if num_ranks <= 0:
        raise ValueError("Requires at least one rank")

    kvs_key = "resource.hwloc.by_rank"
    resource_dict = {}
    for rank in range(num_ranks):
        resource_dict[rank] = {}
        for key in ["Package", "Core", "PU"]:
            resource_dict[rank][key] = cores_per_rank
        resource_dict[rank]["cpuset"] = (
            "0-{}".format(cores_per_rank - 1) if cores_per_rank > 1 else "0"
        )
    put_rc = flux.kvs.put(flux_handle, kvs_key, resource_dict)
    if put_rc < 0:
        raise ValueError("Error inserting resource data into KVS, rc={}".format(put_rc))
    flux.kvs.commit(flux_handle)


def job_state_cb(flux_handle, watcher, msg, simulation):
    '''
    example payload: {u'transitions': [[63652757504, u'CLEANUP'], [63652757504, u'INACTIVE']]}
    '''
    logger.log(9, "Received a job state cb. msg payload: {}".format(msg.payload))
    for jobid, state in msg.payload['transitions']:
        simulation.record_job_state_transition(jobid, state)

def get_loaded_modules(flux_handle):
    modules = flux_handle.rpc("cmb.lsmod").get()
    return modules["mods"]


def load_missing_modules(flux_handle):
    # TODO: check that necessary modules are loaded
    # if not, load them
    # return an updated list of loaded modules
    loaded_modules = get_loaded_modules(flux_handle)
    pass


def reload_scheduler(flux_handle):
    sched_module = "sched-simple"
    # Check if there is a module already loaded providing 'sched' service,
    # if so, reload that module
    for module in get_loaded_modules(flux_handle):
        if "sched" in module["services"]:
            sched_module = module["name"]

    logger.debug("Reloading the '{}' module".format(sched_module))
    flux_handle.rpc("cmb.rmmod", payload={"name": "sched-simple"}).get()
    path = flux.util.modfind("sched-simple")
    flux_handle.rpc("cmb.insmod", payload=json.dumps({"path": path, "args": []})).get()


def job_exception_cb(flux_handle, watcher, msg, cb_args):
    logger.warn("Detected a job exception, but not handling it")


def sim_exec_start_cb(flux_handle, watcher, msg, simulation):
    payload = msg.payload
    logger.log(9, "Received sim-exec.start request. Payload: {}".format(payload))
    jobid = payload["id"]
    simulation.start_job(jobid, msg)


def exec_hello(flux_handle):
    logger.debug("Registering sim-exec with job-manager")
    flux_handle.rpc("job-manager.exec-hello", payload={"service": "sim-exec"}).get()


def service_add(f, name):
    future = f.service_register(name)
    return f.future_get(future, None)


def service_remove(f, name):
    future = f.service_unregister(name)
    return f.future_get(future, None)


def setup_watchers(flux_handle, simulation):
    watchers = []
    services = set()
    for type_mask, topic, cb, args in [
        (flux.constants.FLUX_MSGTYPE_EVENT, "job-state", job_state_cb, simulation),
        (
            flux.constants.FLUX_MSGTYPE_REQUEST,
            "sim-exec.start",
            sim_exec_start_cb,
            simulation,
        ),
    ]:
        if type_mask == flux.constants.FLUX_MSGTYPE_EVENT:
            flux_handle.event_subscribe(topic)
        watcher = flux_handle.msg_watcher_create(
            cb, type_mask=type_mask, topic_glob=topic, args=args
        )
        watcher.start()
        watchers.append(watcher)
        if type_mask == flux.constants.FLUX_MSGTYPE_REQUEST:
            service_name = topic.split(".")[0]
            if service_name not in services:
                service_add(flux_handle, service_name)
                services.add(service_name)
    return watchers, services


def teardown_watchers(flux_handle, watchers, services):
    for watcher in watchers:
        watcher.stop()
    for service_name in services:
        service_remove(flux_handle, service_name)


Makespan = namedtuple('Makespan', ['beginning', 'end'])

class SimpleExec(object):
    def __init__(self, num_nodes, cores_per_node):
        self.num_nodes = num_nodes
        self.cores_per_node = cores_per_node
        self.num_free_nodes = num_nodes
        self.used_core_hours = 0

        self.makespan = Makespan(
            beginning=float('inf'),
            end=-1,
        )

    def update_makespan(self, current_time):
        if current_time < self.makespan.beginning:
            self.makespan = self.makespan._replace(beginning=current_time)
        if current_time > self.makespan.end:
            self.makespan = self.makespan._replace(end=current_time)

    def submit_job(self, simulation, job):
        self.update_makespan(simulation.current_time)

    def start_job(self, simulation, job):
        self.num_free_nodes -= job.nnodes
        if self.num_free_nodes < 0:
            logger.error("Scheduler over-subscribed nodes")
        if (job.ncpus / job.nnodes) > self.cores_per_node:
            logger.error("Scheduler over-subscribed cores on the node")

    def complete_job(self, simulation, job):
        self.num_free_nodes += job.nnodes
        self.used_core_hours += (job.ncpus * job.elapsed_time) / 3600
        self.update_makespan(simulation.current_time)

    def post_analysis(self, simulation):
        if self.makespan.beginning > self.makespan.end:
            logger.warn("Makespan beginning ({}) greater than end ({})".format(
                self.makespan.beginning,
                self.makespan.end,
            ))

        total_num_cores = self.num_nodes * self.cores_per_node
        print("Makespan (hours): {:.1f}".format((self.makespan.end - self.makespan.beginning) / 3600))
        total_core_hours = (total_num_cores * (self.makespan.end - self.makespan.beginning)) / 3600
        print("Total Core-Hours: {:,.1f}".format(total_core_hours))
        print("Used Core-Hours: {:,.1f}".format(self.used_core_hours))
        print("Average Core-Utilization: {:.2f}%".format((self.used_core_hours / total_core_hours) * 100))


logger = logging.getLogger("flux-simulator")


@flux.util.CLIMain(logger)
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("job_file")
    parser.add_argument("num_ranks", type=int)
    parser.add_argument("cores_per_rank", type=int)
    parser.add_argument("--log-level", type=int)
    args = parser.parse_args()

    if args.log_level:
        logger.setLevel(args.log_level)

    flux_handle = flux.Flux()

    exec_validator = SimpleExec(args.num_ranks, args.cores_per_rank)
    simulation = Simulation(
        flux_handle,
        EventList(),
        {},
        submit_job_hook=exec_validator.submit_job,
        start_job_hook=exec_validator.start_job,
        complete_job_hook=exec_validator.complete_job,
    )
    reader = SacctReader(args.job_file)
    reader.validate_trace()
    jobs = list(reader.read_trace())
    for job in jobs:
        job.insert_apriori_events(simulation)

    load_missing_modules(flux_handle)
    insert_resource_data(flux_handle, args.num_ranks, args.cores_per_rank)
    reload_scheduler(flux_handle)

    watchers, services = setup_watchers(flux_handle, simulation)
    exec_hello(flux_handle)
    simulation.advance()
    flux_handle.reactor_run(flux_handle.get_reactor(), 0)
    teardown_watchers(flux_handle, watchers, services)
    exec_validator.post_analysis(simulation)

if __name__ == "__main__":
    main()
