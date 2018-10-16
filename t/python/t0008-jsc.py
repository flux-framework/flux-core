#!/usr/bin/env python
import unittest
import json
from multiprocessing import Queue

import flux.core as core
import flux.jsc as jsc

def __flux_size():
    return 2

def jsc_cb_wait_until_reserved(jcb_str, arg, errnum):
    flux_handle, job_wait_queue = arg
    job_to_wait_for = job_wait_queue.get()

    jcb = json.loads(jcb_str)
    jobid = jcb['jobid']
    new_state = jsc.job_num2state(jcb[jsc.JSC_STATE_PAIR][jsc.JSC_STATE_PAIR_NSTATE])

    if jobid == job_to_wait_for and new_state == 'reserved':
        flux_handle.reactor_stop(flux_handle.get_reactor())

class TestJSC(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = core.Flux()
        self.job_spec = json.dumps({
            'nnodes': 1,
            'ntasks': 1,
            'cmdline': ['sleep', '0'],
            'walltime' : 15,
        })

    @classmethod
    def tearDownClass(self):
        self.f.close()

    def test_00_job_create(self):
        resp = self.f.rpc_send("job.create", self.job_spec)
        jobid = resp['jobid']
        self.assertGreaterEqual(jobid, 0)

    def get_current_statepair(self, jobid):
        resp = jsc.query_jcb(self.f, jobid, jsc.JSC_STATE_PAIR)
        state_pair = resp[jsc.JSC_STATE_PAIR]
        return state_pair

    def test_01_query(self):
        resp = self.f.rpc_send("job.create", self.job_spec)
        jobid = resp['jobid']

        state_pair = self.get_current_statepair(jobid)
        new_state = state_pair[jsc.JSC_STATE_PAIR_NSTATE]

        # test both directions
        self.assertEqual(new_state, jsc.job_state2num("reserved"))
        self.assertEqual(jsc.job_num2state(new_state), "reserved")

    def test_02_update(self):
        resp = self.f.rpc_send("job.create", self.job_spec)
        jobid = resp['jobid']

        target_state = jsc.job_state2num("complete")
        new_state_pair = {jsc.JSC_STATE_PAIR :
                      { jsc.JSC_STATE_PAIR_NSTATE : target_state }}
        jsc.update_jcb(self.f, jobid, jsc.JSC_STATE_PAIR, new_state_pair)

        curr_state_pair = self.get_current_statepair(jobid)
        self.assertEqual(curr_state_pair[jsc.JSC_STATE_PAIR_NSTATE], target_state)

    def test_03_notify(self):
        job_wait_queue = Queue()
        jsc.notify_status(self.f, jsc_cb_wait_until_reserved, (self.f, job_wait_queue))

        resp = self.f.rpc_send("job.create", self.job_spec)
        job_wait_queue.put(resp['jobid'])

        self.assertGreaterEqual(self.f.reactor_run(self.f.get_reactor(), 0), 0)

if __name__ == '__main__':
    from subflux import rerun_under_flux
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner
        unittest.main(testRunner=TAPTestRunner())
