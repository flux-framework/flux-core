#!/usr/bin/env python
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
from pycotap import TAPTestRunner
from sideflux import run_beside_flux

class TestHandle(unittest.TestCase):
  def setUp(self):
    """Create a handle, connect to flux"""
    self.f = core.Flux()

  def test_create_handle(self):
    """Successfully connected to flux"""
    self.assertIsNotNone(self.f)

  def test_rpc_ping(self):
    """Sending a ping"""
    r = self.f.rpc_send('live.ping', {'seq' : 1, 'pad' : 'stuff'})
    self.assertEqual(r['seq'], 1)
    self.assertEqual(r['pad'], 'stuff')

class TestEvent(unittest.TestCase):
  def setUp(self):
    """Create a handle, connect to flux"""
    self.f = core.Flux()

  def test_t1_0_sub(self):
    """Subscribe to an event"""
    self.assertGreaterEqual(
        self.f.event_subscribe("testevent.1"),
        0)

  def test_t1_1_unsub(self):
    """Unsubscribe from an event"""
    self.assertGreaterEqual(
        self.f.event_unsubscribe("testevent.1"),
        0)

  def test_full_event(self):
    """Subscribe send receive and unpack event"""
    self.assertGreaterEqual(
        self.f.event_subscribe("testevent.1"),
        0)
    self.assertGreaterEqual(
        self.f.event_send("testevent.1", {'test' : 'yay!'}),
        0)
    evt = self.f.event_recv() 
    self.assertIsNotNone(evt)
    self.assertEqual(evt.topic, 'testevent.1')
    pld = evt.payload
    self.assertIsNotNone(pld)
    self.assertEqual(pld['test'], 'yay!')
    self.assertIsNotNone(evt.payload_str)
    print evt.payload_str

class TestTimer(unittest.TestCase):
  def setUp(self):
    """Create a handle, connect to flux"""
    self.f = core.Flux()


  def test_timer_add_negative(self):
    """Add a negative timer"""
    with self.assertRaises(EnvironmentError):
      self.f.timer_watcher_create(-500, lambda x,y: x.fatal_error('timer should not run'))

  def test_s1_0_timer_add(self):
    """Add a timer"""
    with self.f.timer_watcher_create(10000, lambda x,y,z,w: x.fatal_error('timer should not run')) as tid:
      self.assertIsNotNone(tid)

  def test_s1_1_timer_remove(self):
    """Remove a timer"""
    to = self.f.timer_watcher_create(10000, lambda x,y: x.fatal_error('timer should not run'))
    to.stop()
    to.destroy()

  def test_timer_with_reactor(self):
    """Register a timer and run the reactor to ensure it can stop it"""
    timer_ran = [False]
    def cb(x, y, z, w):
      timer_ran[0] = True
      x.reactor_stop()
    with self.f.timer_watcher_create(0.1, cb) as timer:
      self.assertIsNotNone(timer, msg="timeout create")
      ret = self.f.reactor_start()
      self.assertEqual(ret, 0, msg="Reactor exit")
      self.assertTrue(timer_ran[0], msg="Timer did not run successfully")


class TestKVS(unittest.TestCase):
  def setUp(self):
    """Create a handle, connect to flux"""
    self.f = core.Flux()


  def test_kvs_dir_open(self):
    with flux.kvs.get_dir(self.f) as d:
      self.assertIsNotNone(d)

  def test_kvs_dir_open(self):
    with flux.kvs.get_dir(self.f) as d:
      self.assertIsNotNone(d)

  def set_and_check_context(self, key, value, msg=''):
    kd = flux.kvs.KVSDir(self.f)
    kd[key] = value
    kd.commit()
    nv = kd[key]
    self.assertEqual(value, nv)

  def test_set_none(self):
    with self.assertRaises(KeyError):
      self.set_and_check_context('None', None)

  def test_set_int(self):
    self.set_and_check_context('int', 10)

  def test_set_float(self):
    self.set_and_check_context('float', 10.5)

  def test_set_string(self):
    self.set_and_check_context('string', "stuff")

  def test_set_list(self):
    self.set_and_check_context('list', [1,2,3,4])

  def test_set_dict(self):
    self.set_and_check_context('dict', {'thing': 'stuff',
      'other thing' : 'more stuff'})

  def test_set_deep(self):
    self.set_and_check_context('a.b.c.e.f.j.k', 5)

  def test_iterator(self):
    keys = [ 'testdir1a.' + str(x) for x in range(1,15)]
    with flux.kvs.get_dir(self.f) as kd:
      for k in keys:
        kd[k] = "bar"

    with flux.kvs.get_dir(self.f, 'testdir1a') as kd:
      print kd.keys()
      for k,v in kd.items():
        self.assertEqual(v, 'bar')
        print("passed {}".format(k))
f = None
if __name__ == '__main__':
  with run_beside_flux(2):
    unittest.main(testRunner=TAPTestRunner())
  # unittest.main()
