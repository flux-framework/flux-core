#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Unit tests for flux.brokermod (BrokerModule, request_handler, event_handler).
# A broker is not required; the flux handle is mocked.

import syslog
import types
import unittest
from unittest.mock import MagicMock, patch

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.brokermod import (
    BrokerLogger,
    BrokerModule,
    event_handler,
    request_handler,
    resolve_entry_point,
)
from flux.constants import FLUX_MSGTYPE_EVENT, FLUX_MSGTYPE_REQUEST
from pycotap import TAPTestRunner


class TestDecorators(unittest.TestCase):
    def test_request_handler_sets_metadata(self):
        @request_handler("hello")
        def func(self, msg):
            pass

        self.assertEqual(
            func._flux_handler,
            {
                "msgtype": FLUX_MSGTYPE_REQUEST,
                "topic": "hello",
                "prefix": True,
                "allow_guest": False,
            },
        )

    def test_event_handler_sets_metadata(self):
        @event_handler("panic")
        def func(self, msg):
            pass

        self.assertEqual(
            func._flux_handler,
            {"msgtype": FLUX_MSGTYPE_EVENT, "topic": "panic", "prefix": True},
        )

    def test_request_handler_prefix_false(self):
        @request_handler("broker.hello", prefix=False)
        def func(self, msg):
            pass

        self.assertEqual(func._flux_handler["prefix"], False)
        self.assertEqual(func._flux_handler["topic"], "broker.hello")

    def test_event_handler_prefix_false(self):
        @event_handler("heartbeat", prefix=False)
        def func(self, msg):
            pass

        self.assertEqual(func._flux_handler["prefix"], False)
        self.assertEqual(func._flux_handler["topic"], "heartbeat")


def make_mock_handle(name=b"mymod"):
    """Return a MagicMock Flux handle with ffi/lib mocked to return name."""
    mock_ffi = MagicMock()
    mock_ffi.string.return_value = name
    mock_lib = MagicMock()
    return MagicMock(), mock_ffi, mock_lib


class TestBrokerModule(unittest.TestCase):
    def _make_module(self, cls, name=b"mymod"):
        mock_h, mock_ffi, mock_lib = make_mock_handle(name)
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):
            mod = cls(mock_h)
        return mod, mock_h

    def test_name_from_aux(self):
        """BrokerModule.name reflects the value returned by flux_aux_get."""

        class Mod(BrokerModule):
            pass

        mod, _ = self._make_module(Mod, name=b"mymod")
        self.assertEqual(mod.name, "mymod")

    def test_name_unknown_when_null(self):
        """BrokerModule.name is 'unknown' when flux_aux_get returns NULL."""

        class Mod(BrokerModule):
            pass

        mock_h = MagicMock()
        mock_ffi = MagicMock()
        mock_lib = MagicMock()
        # Make cast() return the same object as NULL so the != check fails
        sentinel = object()
        mock_ffi.NULL = sentinel
        mock_ffi.cast.return_value = sentinel
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):
            mod = Mod(mock_h)
        self.assertEqual(mod.name, "unknown")

    def test_request_handler_registered(self):
        """A @request_handler method is registered via msg_watcher_create."""

        class Mod(BrokerModule):
            @request_handler("hello")
            def hello(self, msg):
                pass

        mod, mock_h = self._make_module(Mod)
        topics = [call[0][2] for call in mock_h.msg_watcher_create.call_args_list]
        self.assertIn("mymod.hello", topics)

    def test_event_handler_registered(self):
        """A @event_handler method is registered via msg_watcher_create and event_subscribe."""

        class Mod(BrokerModule):
            @event_handler("panic")
            def panic(self, msg):
                pass

        mod, mock_h = self._make_module(Mod)
        topics = [call[0][2] for call in mock_h.msg_watcher_create.call_args_list]
        self.assertIn("mymod.panic", topics)
        mock_h.event_subscribe.assert_called_once_with("mymod.panic")

    def test_prefix_false_skips_module_name(self):
        """A handler with prefix=False is registered without the module name prefix."""

        class Mod(BrokerModule):
            @event_handler("heartbeat", prefix=False)
            def hb(self, msg):
                pass

        mod, mock_h = self._make_module(Mod)
        topics = [call[0][2] for call in mock_h.msg_watcher_create.call_args_list]
        self.assertIn("heartbeat", topics)
        self.assertNotIn("mymod.heartbeat", topics)

    def test_multiple_handlers_all_registered(self):
        """All decorated handlers in a class are registered."""

        class Mod(BrokerModule):
            @request_handler("hello")
            def hello(self, msg):
                pass

            @event_handler("panic")
            def panic(self, msg):
                pass

        mod, mock_h = self._make_module(Mod)
        self.assertEqual(mock_h.msg_watcher_create.call_count, 2)

    def test_inherited_handlers_registered(self):
        """Handlers inherited from a base class are also registered."""

        class Base(BrokerModule):
            @request_handler("hello")
            def hello(self, msg):
                pass

        class Derived(Base):
            @event_handler("panic")
            def panic(self, msg):
                pass

        mod, mock_h = self._make_module(Derived)
        topics = [call[0][2] for call in mock_h.msg_watcher_create.call_args_list]
        self.assertIn("mymod.hello", topics)
        self.assertIn("mymod.panic", topics)

    def test_watchers_started(self):
        """Watchers returned by msg_watcher_create are started."""

        class Mod(BrokerModule):
            @request_handler("hello")
            def hello(self, msg):
                pass

        mod, mock_h = self._make_module(Mod)
        watcher = mock_h.msg_watcher_create.return_value
        watcher.start.assert_called_once()

    def test_allow_guest_sets_rolemask(self):
        """allow_guest=True passes FLUX_ROLE_OWNER|FLUX_ROLE_USER as rolemask."""

        class Mod(BrokerModule):
            @request_handler("info", allow_guest=True)
            def info(self, msg):
                pass

        mock_h, mock_ffi, mock_lib = make_mock_handle()
        mock_lib.FLUX_ROLE_OWNER = 1
        mock_lib.FLUX_ROLE_USER = 2
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):
            Mod(mock_h)

        call_kwargs = mock_h.msg_watcher_create.call_args_list[0][1]
        self.assertEqual(call_kwargs.get("rolemask"), 1 | 2)

    def test_no_allow_guest_rolemask_is_none(self):
        """Without allow_guest, rolemask is None (broker default applies)."""

        class Mod(BrokerModule):
            @request_handler("info")
            def info(self, msg):
                pass

        _, mock_h = self._make_module(Mod)
        call_kwargs = mock_h.msg_watcher_create.call_args_list[0][1]
        self.assertIsNone(call_kwargs.get("rolemask"))


def _make_mod(**attrs):
    """Return a types.ModuleType with the given attributes set."""
    mod = types.ModuleType("testmod")
    for k, v in attrs.items():
        setattr(mod, k, v)
    return mod


class TestResolveEntryPoint(unittest.TestCase):
    def test_mod_main_returned_directly(self):
        """resolve_entry_point returns mod_main when it is defined."""

        def mod_main(h, *args):
            pass

        mod = _make_mod(mod_main=mod_main)
        self.assertIs(resolve_entry_point(mod), mod_main)

    def test_single_subclass_synthesizes_entry_point(self):
        """resolve_entry_point synthesizes an entry point from a single BrokerModule subclass."""

        class MyMod(BrokerModule):
            pass

        mod = _make_mod(MyMod=MyMod)
        entry = resolve_entry_point(mod)
        self.assertTrue(callable(entry))

    def test_single_subclass_calls_run(self):
        """The synthesized entry point calls cls(h, *args).run()."""

        run_called = []

        class MyMod(BrokerModule):
            def run(self):
                run_called.append(self._args)

        mod = _make_mod(MyMod=MyMod)
        entry = resolve_entry_point(mod)

        mock_h = MagicMock()
        mock_ffi = MagicMock()
        mock_lib = MagicMock()
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):
            entry(mock_h, "a", "b")

        self.assertEqual(run_called, [("a", "b")])

    def test_multiple_subclasses_raises(self):
        """resolve_entry_point raises ValueError when multiple subclasses are present."""

        class ModA(BrokerModule):
            pass

        class ModB(BrokerModule):
            pass

        mod = _make_mod(ModA=ModA, ModB=ModB)
        with self.assertRaises(ValueError) as ctx:
            resolve_entry_point(mod)
        self.assertIn("multiple BrokerModule subclasses", str(ctx.exception))

    def test_no_entry_point_raises(self):
        """resolve_entry_point raises ValueError when neither mod_main nor a subclass is found."""
        mod = _make_mod()
        with self.assertRaises(ValueError) as ctx:
            resolve_entry_point(mod)
        self.assertIn("no mod_main()", str(ctx.exception))

    def test_base_class_not_counted_as_subclass(self):
        """BrokerModule itself imported into the module namespace is not treated as a subclass."""
        mod = _make_mod(BrokerModule=BrokerModule)
        with self.assertRaises(ValueError):
            resolve_entry_point(mod)

    def test_mod_main_takes_precedence_over_subclass(self):
        """mod_main is preferred over a BrokerModule subclass when both are present."""

        def mod_main(h, *args):
            pass

        class MyMod(BrokerModule):
            pass

        mod = _make_mod(mod_main=mod_main, MyMod=MyMod)
        self.assertIs(resolve_entry_point(mod), mod_main)


class TestBrokerLogger(unittest.TestCase):
    def _make_logger(self):
        mock_h = MagicMock()
        return BrokerLogger(mock_h), mock_h

    def test_default_level_is_debug(self):
        """Default level passes all messages through (LOG_DEBUG)."""
        logger, _ = self._make_logger()
        self.assertEqual(logger.level, syslog.LOG_DEBUG)

    def test_debug_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.debug("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_DEBUG, "hi")

    def test_info_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.info("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_INFO, "hi")

    def test_notice_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.notice("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_NOTICE, "hi")

    def test_warning_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.warning("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_WARNING, "hi")

    def test_error_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.error("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_ERR, "hi")

    def test_critical_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.critical("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_CRIT, "hi")

    def test_alert_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.alert("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_ALERT, "hi")

    def test_emerg_calls_handle_log(self):
        logger, mock_h = self._make_logger()
        logger.emerg("hi")
        mock_h.log.assert_called_once_with(syslog.LOG_EMERG, "hi")

    def test_call_interface_passes_through(self):
        """__call__(level, msg) forwards to handle.log when level is within threshold."""
        logger, mock_h = self._make_logger()
        logger(syslog.LOG_ERR, "raw")
        mock_h.log.assert_called_once_with(syslog.LOG_ERR, "raw")

    def test_message_below_threshold_is_dropped(self):
        """Messages with priority lower than level are silently dropped."""
        logger, mock_h = self._make_logger()
        logger.level = "info"  # LOG_INFO=6; LOG_DEBUG=7 is below threshold
        logger.debug("dropped")
        mock_h.log.assert_not_called()

    def test_message_at_threshold_is_passed(self):
        """Messages exactly at the threshold are forwarded."""
        logger, mock_h = self._make_logger()
        logger.level = "info"
        logger.info("kept")
        mock_h.log.assert_called_once_with(syslog.LOG_INFO, "kept")

    def test_message_above_threshold_is_passed(self):
        """Messages with higher urgency than threshold are always forwarded."""
        logger, mock_h = self._make_logger()
        logger.level = "info"
        logger.error("kept")
        mock_h.log.assert_called_once_with(syslog.LOG_ERR, "kept")

    def test_set_level_string(self):
        """level setter accepts a level-name string and stores the integer."""
        logger, _ = self._make_logger()
        logger.level = "warning"
        self.assertEqual(logger.level, syslog.LOG_WARNING)

    def test_set_level_int(self):
        """level setter accepts a syslog integer constant directly."""
        logger, _ = self._make_logger()
        logger.level = syslog.LOG_ERR
        self.assertEqual(logger.level, syslog.LOG_ERR)

    def test_set_level_invalid_string_raises(self):
        """level setter raises ValueError for an unknown level name."""
        logger, _ = self._make_logger()
        with self.assertRaises(ValueError) as ctx:
            logger.level = "verbose"
        self.assertIn("verbose", str(ctx.exception))

    def test_set_level_invalid_type_raises(self):
        """level setter raises ValueError for a non-integer, non-string value."""
        logger, _ = self._make_logger()
        with self.assertRaises(ValueError):
            logger.level = [1, 2, 3]

    def test_level_name_property(self):
        """level_name returns the string name matching the current level."""
        logger, _ = self._make_logger()
        logger.level = syslog.LOG_INFO
        self.assertEqual(logger.level_name, "info")

    def test_level_name_unknown_int(self):
        """level_name falls back to the integer string for unknown values."""
        logger, _ = self._make_logger()
        logger.level = 99
        self.assertEqual(logger.level_name, "99")

    def test_broker_module_has_log_attribute(self):
        """BrokerModule.__init__ creates a self.log BrokerLogger instance."""
        mock_h = MagicMock()
        mock_ffi = MagicMock()
        mock_lib = MagicMock()
        mock_ffi.string.return_value = b"mymod"
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):

            class Mod(BrokerModule):
                pass

            mod = Mod(mock_h)
        self.assertIsInstance(mod.log, BrokerLogger)

    def test_broker_module_log_forwards_to_handle(self):
        """mod.log.error() reaches handle.log() on the underlying Flux handle."""
        mock_h = MagicMock()
        mock_ffi = MagicMock()
        mock_lib = MagicMock()
        mock_ffi.string.return_value = b"mymod"
        with patch("flux.brokermod.ffi", mock_ffi), patch(
            "flux.brokermod.lib", mock_lib
        ):

            class Mod(BrokerModule):
                pass

            mod = Mod(mock_h)
        mod.log.error("boom")
        mock_h.log.assert_called_once_with(syslog.LOG_ERR, "boom")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
