import unittest
import errno
import os
import re
import flux.core as core
from flux.core.inner import ffi, lib, raw
import flux.wrapper


class TestWrapper(unittest.TestCase):
    def test_call_non_existant(self):
        f = core.Flux('loop://')
        with self.assertRaises(flux.wrapper.MissingFunctionError):
            f.non_existant_function_that_should_die("stuff")

    def test_call_insufficient_arguments(self):
        f = core.Flux('loop://')
        with self.assertRaises(flux.wrapper.WrongNumArguments):
            f.request_encode(self)

    def test_call_invalid_argument_type(self):
        f = core.Flux('loop://')
        with self.assertRaises(flux.wrapper.InvalidArguments):
          f.request_encode(self, 15)

    def test_automatic_unwrapping(self):
      flux.core.inner.raw.flux_log(core.Flux('loop://'), 0, 'stuff')

    def test_masked_function(self):
      with self.assertRaisesRegexp(AttributeError, r'.*masks function.*'):
        core.Flux('loop://').rpc_create('topic').pimpl.flux_request_encode('request', 15)

    def test_set_pimpl_handle(self):
      f = core.Flux('loop://')
      r = f.rpc_create('topic')
      r.handle = raw.flux_rpc(f.handle, 'other topic', ffi.NULL, flux.constants.FLUX_NODEID_ANY, 0)

    def test_set_pimpl_handle_invalid(self):
      f = core.Flux('loop://')
      r = f.rpc_create('topic')
      with self.assertRaisesRegexp(TypeError, r'.*expected a.*'):
          r.handle = f.rpc_create("other topic")

    def test_read_basic_value(self):
      self.assertGreater(flux.constants.FLUX_NODEID_ANY, 0)


if __name__ == '__main__':
    unittest.main()
