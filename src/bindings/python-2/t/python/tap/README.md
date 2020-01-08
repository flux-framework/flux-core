# [pycotap: Tiny Python TAP Test Runner](https://el-tramo.be/pycotap)

`pycotap` is a simple Python test runner for ``unittest`` that outputs
[Test Anything Protocol](http://testanything.org) results to standard output
(similar to what [`tape`](https://www.npmjs.com/package/tape) does for JavaScript).

Contrary to other TAP runners for Python, `pycotap` ...

- ... prints TAP (and *only* TAP) to standard output instead of to a separate file,
  allowing you to pipe it directly to TAP pretty printers and processors
  (such as the ones listed on
  the [`tape` page](https://www.npmjs.com/package/tape#pretty-reporters)). By
  piping it to other consumers, you can avoid the need to add
  specific test runners to your test code. Since the TAP results
  are printed as they come in, the consumers can directly display results while
  the tests are run.
- ... only contains a TAP reporter, so no parsers, no frameworks, no dependencies, ...
- ... is configurable: you can choose how you want the test output and test result
  diagnostics to end up in your TAP output (as TAP diagnostics, YAML blocks, or
  attachments). The defaults are optimized for a [Jenkins](http://jenkins-ci.org) based
  flow.


## Installation

You can install the package directly from [PIP](https://pypi.python.org):

    pip install pycotap

Alternatively, you can build and install the package yourself:

    python setup.py install

Since the module just consists of one file, you can also just drop the file into
your project somewhere.


## Usage

Create a test suite, and pass it to a `TAPTestRunner`.
For example:

    import unittest
    from pycotap import TAPTestRunner

    class MyTests(unittest.TestCase):
      def test_that_it_passes(self):
        self.assertEqual(0, 0)

      @unittest.skip("not finished yet")
      def test_that_it_skips(self):
        raise Exception("Does not happen")

      def test_that_it_fails(self):
        self.assertEqual(1, 0)

    if __name__ == '__main__':
      suite = unittest.TestLoader().loadTestsFromTestCase(MyTests)
      TAPTestRunner().run(suite)

Running the test prints the TAP results to standard output:

    $ python ./test_example.py
    not ok 1 __main__.MyTests.test_that_it_fails
    ok 2 __main__.MyTests.test_that_it_passes
    ok 3 __main__.MyTests.test_that_it_skips # Skipped: not finished yet
    1..3

Alternatively, you can pipe the test to any TAP pretty printer, such as
[faucet](https://github.com/substack/faucet) or
[tap-dot](https://github.com/scottcorgan/tap-dot):

    $ python ./test_example.py  | faucet
    ⨯ __main__.MyTests.test_that_it_fails
    ✓ __main__.MyTests.test_that_it_passes
    ✓ __main__.MyTests.test_that_it_skips # Skipped: not finished yet
    ⨯ fail  1


    $ python ./test_example.py  | tap-dot
    x  ..

      3 tests
      2 passed
      1 failed

      Failed Tests:   There was 1 failure
        x __main__.MyTests.test_that_it_fails


## API

### `TAPTestRunner([message_log], [test_output_log])`

- `message_log` (Optional; Default: `LogMode.LogToYAML`):
  What to do with test messages (e.g. assertion failure details).
  See `LogMode` for possible values.
- `test_output_log` (Optional; Default: `LogMode.LogToDiagnostics`):
  What to do with output printed by the tests.
  See `LogMode` for possible values.


### `LogMode`

Enumeration of different destinations to log information. Possible values:

- `LogMode.LogToError`: Log all output to standard error. This means no output
  information will end up in the TAP stream, and so will not be processed by any
  processors.
- `LogMode.LogToDiagnostics` (Default): Put output in a diagnostics message
  after the test result. This means all output will end up in the TAP stream. How
  this is displayed depends on the processor.
- `LogMode.LogToYAML`: Put output in a YAML block.
- `LogMode.LogToAttachment`: Put output in a downloadable attachment in a YAML block.
  This is an extension supported by e.g. [`tap4j`](http://tap4j.org).
