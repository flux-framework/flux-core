#!/usr/bin/env python
# coding=utf-8

# Copyright (c) 2015 Remko Tronçon (https://el-tramo.be)
# Released under the MIT license
# See COPYING for details


import unittest
import sys
import base64

if sys.hexversion >= 0x03000000:
    from io import StringIO
else:
    from StringIO import StringIO

# Log modes
class LogMode(object):
    LogToError, LogToDiagnostics, LogToYAML, LogToAttachment = range(4)


class TAPTestResult(unittest.TestResult):
    def __init__(self, output_stream, error_stream, message_log, test_output_log):
        super(TAPTestResult, self).__init__(self, output_stream)
        self.output_stream = output_stream
        self.error_stream = error_stream
        self.orig_stdout = None
        self.orig_stderr = None
        self.message = error_stream
        self.test_output = None
        self.message_log = message_log
        self.test_output_log = test_output_log
        self.output_stream.write("TAP version 13\n")

    def print_raw(self, text):
        self.output_stream.write(text)
        self.output_stream.flush()

    def print_result(self, result, test, directive=None):
        self.output_stream.write("%s %d %s" % (result, self.testsRun, test.id()))
        if directive:
            self.output_stream.write(" # " + directive)
        self.output_stream.write("\n")
        self.output_stream.flush()

    def ok(self, test, directive=None):
        self.print_result("ok", test, directive)

    def not_ok(self, test):
        self.print_result("not ok", test)

    def startTest(self, test):
        self.orig_stdout = sys.stdout
        self.orig_stderr = sys.stderr
        if self.message_log == LogMode.LogToError:
            self.message = self.error_stream
        else:
            self.message = StringIO()
        if self.test_output_log == LogMode.LogToError:
            self.test_output = self.error_stream
        else:
            self.test_output = StringIO()

        if self.message_log == self.test_output_log:
            self.test_output = self.message

        sys.stdout = sys.stderr = self.test_output
        super(TAPTestResult, self).startTest(test)

    def stopTest(self, test):
        super(TAPTestResult, self).stopTest(test)
        sys.stdout = self.orig_stdout
        sys.stderr = self.orig_stderr
        if self.message_log == self.test_output_log:
            logs = [(self.message_log, self.message, "output")]
        else:
            logs = [
                (self.test_output_log, self.test_output, "test_output"),
                (self.message_log, self.message, "message"),
            ]
        for log_mode, log, log_name in logs:
            if log_mode != LogMode.LogToError:
                output = log.getvalue()
                if len(output):
                    if log_mode == LogMode.LogToYAML:
                        self.print_raw("  ---\n")
                        self.print_raw("    " + log_name + ": |\n")
                        self.print_raw(
                            "      " + output.rstrip().replace("\n", "\n      ") + "\n"
                        )
                        self.print_raw("  ...\n")
                    elif log_mode == LogMode.LogToAttachment:
                        self.print_raw("  ---\n")
                        self.print_raw("    " + log_name + ":\n")
                        self.print_raw("      File-Name: " + log_name + ".txt\n")
                        self.print_raw("      File-Type: text/plain\n")
                        self.print_raw(
                            "      File-Content: " + base64.b64encode(output) + "\n"
                        )
                        self.print_raw("  ...\n")
                    else:
                        self.print_raw(
                            "# " + output.rstrip().replace("\n", "\n# ") + "\n"
                        )

    def addSuccess(self, test):
        super(TAPTestResult, self).addSuccess(test)
        self.ok(test)

    def addError(self, test, err):
        super(TAPTestResult, self).addError(test, err)
        self.message.write(self.errors[-1][1] + "\n")
        self.not_ok(test)

    def addFailure(self, test, err):
        super(TAPTestResult, self).addFailure(test, err)
        self.message.write(self.failures[-1][1] + "\n")
        self.not_ok(test)

    def addSkip(self, test, reason):
        super(TAPTestResult, self).addSkip(test, reason)
        self.ok(test, "SKIP " + reason)

    def addExpectedFailure(self, test, err):
        super(TAPTestResult, self).addExpectedFailure(test, err)
        self.message.write(self.expectedFailures[-1][1] + "\n")
        self.ok(test)

    def addUnexpectedSuccess(self, test):
        super(TAPTestResult, self).addUnexpectedSuccess(self, test)
        self.not_ok(test)

    def printErrors(self):
        self.print_raw("1..%d\n" % self.testsRun)


class TAPTestRunner(object):
    def __init__(
        self,
        message_log=LogMode.LogToYAML,
        test_output_log=LogMode.LogToDiagnostics,
        output_stream=sys.stdout,
        error_stream=sys.stderr,
    ):
        self.output_stream = output_stream
        self.error_stream = error_stream
        self.message_log = message_log
        self.test_output_log = test_output_log

    def run(self, test):
        result = TAPTestResult(
            self.output_stream,
            self.error_stream,
            self.message_log,
            self.test_output_log,
        )
        test(result)
        result.printErrors()

        return result
