#!/usr/bin/env python
# coding=utf-8

from setuptools import setup, find_packages

setup(
  name = "pycotap",
  version = "1.0.0",
  packages = find_packages(),

  # Metadata
  author = "Remko Tronçon",
  author_email = "dev@el-tramo.be",
  description = """A tiny test runner that outputs TAP results to standard output.""",
  long_description = """
`pycotap` is a simple Python test runner for ``unittest`` that outputs
`Test Anything Protocol <http://testanything.org>`_ results directly to standard output.

Contrary to other TAP runners for Python, ``pycotap`` ...

- ... prints TAP (and *only* TAP) to standard output instead of to a separate file,
  allowing you to pipe it directly to TAP pretty printers and processors
  (such as the ones listed on
  `the tape page <https://www.npmjs.com/package/tape#pretty-reporters>`_). By
  piping it to other consumers, you can avoid the need to add
  specific test runners to your test code. Since the TAP results
  are printed as they come in, the consumers can directly display results while
  the tests are run.

- ... only contains a TAP reporter, so no parsers, no frameworks, no dependencies, ...

- ... is configurable: you can choose how you want the test output and test result
  diagnostics to end up in your TAP output (as TAP diagnostics, YAML blocks, or
  attachments). The defaults are optimized for a `Jenkins <http://jenkins-ci.org>`_ based
  flow.

Documentation and examples can be found on `the pycotap page
<https://el-tramo.be/pycotap>`_.
""",
  license = "MIT",
  keywords = "tap unittest testing",
  url = "https://el-tramo.be/pycotap",
  classifiers = [
    "Development Status :: 5 - Production/Stable",
    "Intended Audience :: Developers",
    "Topic :: Utilities",
    "License :: OSI Approved :: MIT License",
    "Topic :: Software Development :: Libraries :: Python Modules",
    "Topic :: Software Development :: Testing"
  ],
)
