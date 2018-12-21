#!/usr/bin/env python

# pylint: disable=C0325

import sys, os

sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import unittest
import re

if sys.hexversion >= 0x03000000:
    from io import StringIO
else:
    from StringIO import StringIO

from pycotap import TAPTestRunner, LogMode


class TAPTestRunnerTest(unittest.TestCase):
    class OutputTest(unittest.TestCase):
        def test_failing(self):
            print("Foo")
            self.assertEqual(1, 2)
            print("Bar")

        def test_passing(self):
            print("Foo")
            sys.stderr.write("Baz\n")
            print("Bar")

    def setUp(self):
        self.output_stream = StringIO()
        self.error_stream = StringIO()

    def run_test(self, test_class, **kwargs):
        suite = unittest.TestLoader().loadTestsFromTestCase(test_class)
        return TAPTestRunner(
            output_stream=self.output_stream, error_stream=self.error_stream, **kwargs
        ).run(suite)

    def process_output(self, output):
        return re.sub(
            r"File \".*\"", 'File "test.py"', re.sub(r"line \d+", "line X", output)
        )

    def test_all_test_outcomes(self):
        class Test(unittest.TestCase):
            def test_passing(self):
                self.assertEqual(1, 1)

            def test_failing(self):
                self.assertEqual(1, 2)

            @unittest.skip("Not finished yet")
            def test_skipped(self):
                self.assertEqual(1, 2)

        self.run_test(
            Test,
            message_log=LogMode.LogToDiagnostics,
            test_output_log=LogMode.LogToDiagnostics,
        )
        self.assertEqual(
            self.process_output(self.output_stream.getvalue()),
            (
                "TAP version 13\n"
                "not ok 1 __main__.Test.test_failing\n"
                "# Traceback (most recent call last):\n"
                '#   File "test.py", line X, in test_failing\n'
                "#     self.assertEqual(1, 2)\n"
                "# AssertionError: 1 != 2\n"
                "ok 2 __main__.Test.test_passing\n"
                "ok 3 __main__.Test.test_skipped # SKIP Not finished yet\n"
                "1..3\n"
            ),
        )
        self.assertEqual("", self.error_stream.getvalue())

    def test_log_output_to_diagnostics(self):
        self.run_test(
            TAPTestRunnerTest.OutputTest,
            message_log=LogMode.LogToDiagnostics,
            test_output_log=LogMode.LogToDiagnostics,
        )
        self.assertEqual(
            self.process_output(self.output_stream.getvalue()),
            (
                "TAP version 13\n"
                "not ok 1 __main__.OutputTest.test_failing\n"
                "# Foo\n"
                "# Traceback (most recent call last):\n"
                '#   File "test.py", line X, in test_failing\n'
                "#     self.assertEqual(1, 2)\n"
                "# AssertionError: 1 != 2\n"
                "ok 2 __main__.OutputTest.test_passing\n"
                "# Foo\n"
                "# Baz\n"
                "# Bar\n"
                "1..2\n"
            ),
        )
        self.assertEqual("", self.error_stream.getvalue())

    def test_log_output_to_yaml(self):
        self.run_test(
            TAPTestRunnerTest.OutputTest,
            message_log=LogMode.LogToYAML,
            test_output_log=LogMode.LogToYAML,
        )
        self.assertEqual(
            self.process_output(self.output_stream.getvalue()),
            (
                "TAP version 13\n"
                "not ok 1 __main__.OutputTest.test_failing\n"
                "  ---\n"
                "    output: |\n"
                "      Foo\n"
                "      Traceback (most recent call last):\n"
                '        File "test.py", line X, in test_failing\n'
                "          self.assertEqual(1, 2)\n"
                "      AssertionError: 1 != 2\n"
                "  ...\n"
                "ok 2 __main__.OutputTest.test_passing\n"
                "  ---\n"
                "    output: |\n"
                "      Foo\n"
                "      Baz\n"
                "      Bar\n"
                "  ...\n"
                "1..2\n"
            ),
        )
        self.assertEqual("", self.error_stream.getvalue())

    def test_log_output_to_error(self):
        self.run_test(
            TAPTestRunnerTest.OutputTest,
            message_log=LogMode.LogToError,
            test_output_log=LogMode.LogToError,
        )
        self.assertEqual(
            self.output_stream.getvalue(),
            (
                "TAP version 13\n"
                "not ok 1 __main__.OutputTest.test_failing\n"
                "ok 2 __main__.OutputTest.test_passing\n"
                "1..2\n"
            ),
        )
        self.assertEqual(
            self.process_output(self.error_stream.getvalue()),
            (
                "Foo\n"
                "Traceback (most recent call last):\n"
                '  File "test.py", line X, in test_failing\n'
                "    self.assertEqual(1, 2)\n"
                "AssertionError: 1 != 2\n"
                "\n"
                "Foo\n"
                "Baz\n"
                "Bar\n"
            ),
        )

    def test_log_to_error_order(self):
        self.output_stream = self.error_stream
        self.run_test(
            TAPTestRunnerTest.OutputTest,
            message_log=LogMode.LogToError,
            test_output_log=LogMode.LogToError,
        )
        self.assertEqual(
            self.process_output(self.output_stream.getvalue()),
            (
                "TAP version 13\n"
                "Foo\n"
                "Traceback (most recent call last):\n"
                '  File "test.py", line X, in test_failing\n'
                "    self.assertEqual(1, 2)\n"
                "AssertionError: 1 != 2\n"
                "\n"
                "not ok 1 __main__.OutputTest.test_failing\n"
                "Foo\n"
                "Baz\n"
                "Bar\n"
                "ok 2 __main__.OutputTest.test_passing\n"
                "1..2\n"
            ),
        )

    def test_different_error_and_test_output_log(self):
        self.run_test(
            TAPTestRunnerTest.OutputTest,
            message_log=LogMode.LogToYAML,
            test_output_log=LogMode.LogToDiagnostics,
        )
        self.assertEqual(
            self.process_output(self.output_stream.getvalue()),
            (
                "TAP version 13\n"
                "not ok 1 __main__.OutputTest.test_failing\n"
                "# Foo\n"
                "  ---\n"
                "    message: |\n"
                "      Traceback (most recent call last):\n"
                '        File "test.py", line X, in test_failing\n'
                "          self.assertEqual(1, 2)\n"
                "      AssertionError: 1 != 2\n"
                "  ...\n"
                "ok 2 __main__.OutputTest.test_passing\n"
                "# Foo\n"
                "# Baz\n"
                "# Bar\n"
                "1..2\n"
            ),
        )
        self.assertEqual("", self.error_stream.getvalue())


if __name__ == "__main__":
    # unittest.main()
    TAPTestRunner().run(unittest.TestLoader().loadTestsFromTestCase(TAPTestRunnerTest))
