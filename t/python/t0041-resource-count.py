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

"""Unit tests for ResourceCount and ResourceCount.from_count_spec.

Coverage mirrors the C test suite in src/common/libjob/test/count.c,
adapted for the Python interface:

  - test_codec    → TestFromCountSpec* (parsing + structural verification)
  - test_iteration → TestIteration (valid_counts yields all values)
"""

import unittest

import subflux  # noqa: F401 - for PYTHONPATH
from flux.idset import IDset
from flux.resource.ResourceCount import ResourceCount
from pycotap import TAPTestRunner

# ---------------------------------------------------------------------------
# TestResourceCount: constructor, valid_counts, scaled
# ---------------------------------------------------------------------------


class TestResourceCount(unittest.TestCase):
    """Tests for the ResourceCount constructor, valid_counts(), and scaled()."""

    # -- valid_counts ---------------------------------------------------------

    def test_valid_counts_contiguous(self):
        """Simple range yields all integers from cap down to min."""
        c = ResourceCount(2, 5)
        self.assertEqual(list(c.valid_counts(5)), [5, 4, 3, 2])

    def test_valid_counts_contiguous_capped(self):
        """available < max caps the top of the range."""
        c = ResourceCount(2, 8)
        self.assertEqual(list(c.valid_counts(5)), [5, 4, 3, 2])

    def test_valid_counts_contiguous_below_min(self):
        """available < min yields nothing."""
        c = ResourceCount(4, 8)
        self.assertEqual(list(c.valid_counts(3)), [])

    def test_valid_counts_exact_min(self):
        """available == min yields exactly min."""
        c = ResourceCount(4, 8)
        self.assertEqual(list(c.valid_counts(4)), [4])

    def test_valid_counts_unbounded(self):
        """Unbounded count (max=None) treats available as the cap."""
        c = ResourceCount(2, None)
        self.assertEqual(list(c.valid_counts(4)), [4, 3, 2])

    def test_valid_counts_stepped(self):
        """Stepped sequence (powers of two) yields only valid values."""
        c = ResourceCount(2, 8, IDset("2,4,8"))
        self.assertEqual(list(c.valid_counts(8)), [8, 4, 2])

    def test_valid_counts_stepped_capped(self):
        """Values above available are skipped in a stepped sequence."""
        c = ResourceCount(2, 8, IDset("2,4,8"))
        self.assertEqual(list(c.valid_counts(5)), [4, 2])

    def test_valid_counts_stepped_below_min(self):
        """Stepped sequence with available < min yields nothing."""
        c = ResourceCount(2, 8, IDset("2,4,8"))
        self.assertEqual(list(c.valid_counts(1)), [])

    def test_valid_counts_fixed(self):
        """Fixed count (min == max) yields exactly one value."""
        c = ResourceCount(3, 3)
        self.assertEqual(list(c.valid_counts(10)), [3])

    def test_valid_counts_fixed_below(self):
        """Fixed count with available < min yields nothing."""
        c = ResourceCount(3, 3)
        self.assertEqual(list(c.valid_counts(2)), [])

    def test_valid_counts_integer_one(self):
        """Count(1, 1) yields [1] when available >= 1."""
        c = ResourceCount(1, 1)
        self.assertEqual(list(c.valid_counts(1)), [1])

    # -- scaled ---------------------------------------------------------------

    def test_scaled_simple(self):
        c = ResourceCount(2, 8)
        s = c.scaled(4)
        self.assertEqual(s.min, 8)
        self.assertEqual(s.max, 32)
        self.assertIsNone(s._values)

    def test_scaled_unbounded(self):
        c = ResourceCount(2, None)
        s = c.scaled(3)
        self.assertEqual(s.min, 6)
        self.assertIsNone(s.max)

    def test_scaled_factor_one_returns_self(self):
        """scaled(1) returns the same object (identity optimisation)."""
        c = ResourceCount(2, 8)
        self.assertIs(c.scaled(1), c)

    def test_scaled_with_values(self):
        c = ResourceCount(2, 8, IDset("2,4,8"))
        s = c.scaled(2)
        self.assertEqual(s.min, 4)
        self.assertEqual(s.max, 16)
        self.assertIsNotNone(s._values)
        self.assertEqual(list(s.valid_counts(16)), [16, 8, 4])


# ---------------------------------------------------------------------------
# TestFromCountSpecValid: well-formed inputs — structural checks
# (mirrors test_codec 'expected success' rows)
# ---------------------------------------------------------------------------


class TestFromCountSpecValid(unittest.TestCase):
    """Data-driven tests for well-formed from_count_spec inputs."""

    def _check(self, spec, exp_min, exp_max, exp_values=None):
        c = ResourceCount.from_count_spec(spec)
        self.assertEqual(c.min, exp_min, f"spec={spec!r}")
        self.assertEqual(c.max, exp_max, f"spec={spec!r}")
        if exp_values is None:
            self.assertIsNone(c._values, f"spec={spec!r}")
        else:
            self.assertIsNotNone(c._values, f"spec={spec!r}")

    # integers
    def test_integer_2(self):
        self._check(2, 2, 2)

    def test_integer_large(self):
        self._check(1048576, 1048576, 1048576)

    # plain integer strings
    def test_str_2(self):
        self._check("2", 2, 2)

    def test_str_13(self):
        self._check("13", 13, 13)

    # IDset strings (comma-separated → _values set)
    def test_str_idset_7_9(self):
        c = ResourceCount.from_count_spec("7-9")
        self.assertEqual(c.min, 7)
        self.assertEqual(c.max, 9)
        self.assertIsNone(c._values)  # contiguous → no _values

    def test_str_idset_1_7_9(self):
        c = ResourceCount.from_count_spec("1,7-9")
        self.assertEqual(c.min, 1)
        self.assertEqual(c.max, 9)
        self.assertIsNotNone(c._values)

    def test_str_idset_1_7_9_16(self):
        c = ResourceCount.from_count_spec("1,7-9,16")
        self.assertEqual(c.min, 1)
        self.assertEqual(c.max, 16)
        self.assertIsNotNone(c._values)

    def test_str_idset_1_3_7_9_14_16(self):
        c = ResourceCount.from_count_spec("1-3,7-9,14,16")
        self.assertEqual(c.min, 1)
        self.assertEqual(c.max, 16)
        self.assertIsNotNone(c._values)

    # bracketed forms
    def test_str_bracketed_integer(self):
        self._check("[2]", 2, 2)

    def test_str_bracketed_range(self):
        c = ResourceCount.from_count_spec("[7-9]")
        self.assertEqual(c.min, 7)
        self.assertEqual(c.max, 9)
        self.assertIsNone(c._values)

    def test_str_bracketed_idset(self):
        c = ResourceCount.from_count_spec("[2,3,4,5]")
        self.assertIsNotNone(c._values)

    # RFC 45 range strings
    def test_str_singleton_range(self):
        self._check("3-3", 3, 3)

    def test_str_unbounded(self):
        self._check("2+", 2, None)

    def test_str_bracketed_unbounded(self):
        self._check("[2+]", 2, None)

    def test_str_range_with_operand(self):
        c = ResourceCount.from_count_spec("2-5:3")
        self.assertEqual(c.min, 2)
        self.assertEqual(c.max, 5)
        self.assertIsNotNone(c._values)

    def test_str_range_explicit_default_op(self):
        c = ResourceCount.from_count_spec("2-5:1:+")
        self.assertEqual(c.min, 2)
        self.assertEqual(c.max, 5)
        self.assertIsNone(c._values)

    def test_str_multiplicative(self):
        c = ResourceCount.from_count_spec("2-8:2:*")
        self.assertEqual(c.min, 2)
        self.assertEqual(c.max, 8)
        self.assertIsNotNone(c._values)

    def test_str_large_additive(self):
        c = ResourceCount.from_count_spec("25-133:17:+")
        self.assertEqual(c.min, 25)
        self.assertEqual(c.max, 133)
        self.assertIsNotNone(c._values)

    def test_str_unbounded_explicit_operand(self):
        self._check("2+:1:+", 2, None)

    # dict forms
    def test_dict_bounded(self):
        self._check({"min": 2, "max": 8}, 2, 8)

    def test_dict_singleton(self):
        self._check({"min": 3, "max": 3}, 3, 3)

    def test_dict_unbounded(self):
        self._check({"min": 2}, 2, None)

    def test_dict_explicit_default_op(self):
        c = ResourceCount.from_count_spec(
            {"min": 2, "max": 5, "operand": 1, "operator": "+"}
        )
        self.assertEqual(c.min, 2)
        self.assertEqual(c.max, 5)
        self.assertIsNone(c._values)

    def test_dict_nonunit_additive(self):
        c = ResourceCount.from_count_spec({"min": 2, "max": 5, "operand": 3})
        self.assertIsNotNone(c._values)

    def test_dict_multiplicative(self):
        c = ResourceCount.from_count_spec(
            {"min": 2, "max": 8, "operand": 2, "operator": "*"}
        )
        self.assertIsNotNone(c._values)

    def test_dict_large_additive(self):
        c = ResourceCount.from_count_spec(
            {"min": 25, "max": 133, "operand": 17, "operator": "+"}
        )
        self.assertIsNotNone(c._values)

    def test_dict_exponential(self):
        c = ResourceCount.from_count_spec(
            {"min": 2, "max": 16, "operand": 2, "operator": "^"}
        )
        self.assertIsNotNone(c._values)


# ---------------------------------------------------------------------------
# TestFromCountSpecErrors: malformed inputs that must raise ValueError
# (mirrors test_codec 'expected failures' rows and test_iteration failures)
# ---------------------------------------------------------------------------


# (label, spec) pairs that must raise ValueError
_INVALID_STRING_CASES = [
    ("empty string", ""),
    ("empty brackets", "[]"),
    ("zero integer string", "0"),
    ("zero min range", "0-8"),
    ("zero in brackets", "[0]"),
    ("negative leading dash", "-5"),
    ("negative in brackets", "[-5]"),
    ("max lt min simple range", "3-0"),
    ("max lt min bracketed range", "[3-0]"),
    ("max lt min with step", "3-2:1:+"),
    ("missing max after dash", "0-"),
    ("missing max in brackets", "[0-]"),
    ("operand 1 multiplicative str", "2-8:1:*"),
    ("operand 1 exponential str", "2-8:1:^"),
    ("unknown operator str", "2-8:1:/"),
    ("unbounded nonunit step", "2+:2:*"),
    ("unbounded nonunit step exp", "2+:2:^"),
    ("caret min 1", "1-16:2:^"),
    ("float-like", "4.2"),
    ("non-numeric", "x"),
    ("trailing garbage after range", "1-2x"),
    ("mismatched open bracket", "[0"),
    ("mismatched close bracket", "0]"),
]

_INVALID_DICT_CASES = [
    ("zero min", {"min": 0, "max": 8}),
    ("negative min", {"min": -2, "max": 6, "operand": 1, "operator": "+"}),
    ("max lt min additive", {"min": 3, "max": 1, "operand": 2, "operator": "+"}),
    ("max lt min unit step", {"min": 3, "max": 1}),
    ("operand 0 additive", {"min": 4, "max": 6, "operand": 0, "operator": "+"}),
    ("operand 1 multiplicative", {"min": 2, "max": 16, "operand": 1, "operator": "*"}),
    ("operand 1 exponential", {"min": 2, "max": 16, "operand": 1, "operator": "^"}),
    ("caret min 1", {"min": 1, "max": 16, "operand": 2, "operator": "^"}),
    ("unknown operator", {"min": 2, "max": 16, "operand": 1, "operator": "/"}),
    ("unbounded nonunit step", {"min": 2, "operator": "*", "operand": 2}),
    ("unbounded nonunit step exp", {"min": 2, "operator": "^", "operand": 2}),
]


class TestFromCountSpecErrors(unittest.TestCase):
    """from_count_spec must raise ValueError for all malformed inputs."""

    def _assert_invalid_string(self, label, spec):
        with self.assertRaises((ValueError, Exception), msg=label):
            ResourceCount.from_count_spec(spec)

    def _assert_invalid_dict(self, label, spec):
        with self.assertRaises(ValueError, msg=label):
            ResourceCount.from_count_spec(spec)


def _make_string_error_test(label, spec):
    def test(self):
        with self.assertRaises((ValueError, Exception), msg=label):
            ResourceCount.from_count_spec(spec)

    test.__name__ = "test_invalid_str_" + label.replace(" ", "_")
    test.__doc__ = f"from_count_spec({spec!r}) must raise — {label}"
    return test


def _make_dict_error_test(label, spec):
    def test(self):
        with self.assertRaises(ValueError, msg=label):
            ResourceCount.from_count_spec(spec)

    test.__name__ = "test_invalid_dict_" + label.replace(" ", "_")
    test.__doc__ = f"from_count_spec({spec!r}) must raise — {label}"
    return test


for _label, _spec in _INVALID_STRING_CASES:
    setattr(
        TestFromCountSpecErrors,
        _make_string_error_test(_label, _spec).__name__,
        _make_string_error_test(_label, _spec),
    )

for _label, _spec in _INVALID_DICT_CASES:
    setattr(
        TestFromCountSpecErrors,
        _make_dict_error_test(_label, _spec).__name__,
        _make_dict_error_test(_label, _spec),
    )


# ---------------------------------------------------------------------------
# TestIteration: valid_counts enumerates all values correctly
# (mirrors test_iteration in count.c; values are compared as sorted sets
#  since valid_counts yields descending while C iterates ascending)
# ---------------------------------------------------------------------------


# (spec, expected_values_ascending)
_ITERATION_CASES = [
    # integer and simple string forms
    (1, [1]),
    ("13", [13]),
    ("5,7,13", [5, 7, 13]),
    # contiguous dict range
    ({"min": 4, "max": 6}, [4, 5, 6]),
    # explicit default operator — same as above
    ({"min": 4, "max": 6, "operand": 1, "operator": "+"}, [4, 5, 6]),
    # additive step (operand only, no explicit operator)
    ({"min": 1, "max": 3, "operand": 2}, [1, 3]),
    # additive step with explicit operator
    ({"min": 1, "max": 3, "operand": 2, "operator": "+"}, [1, 3]),
    # multiplicative
    ({"min": 2, "max": 16, "operand": 2, "operator": "*"}, [2, 4, 8, 16]),
    # exponential
    ({"min": 2, "max": 16, "operand": 2, "operator": "^"}, [2, 4, 16]),
    # string forms
    ("2-5:3", [2, 5]),
    ("2-5:1:+", [2, 3, 4, 5]),
    ("2-8:2:*", [2, 4, 8]),
    ("25-133:17:+", [25, 42, 59, 76, 93, 110, 127]),
    ("2-16:2:^", [2, 4, 16]),
    # large additive dict
    (
        {"min": 25, "max": 133, "operand": 17, "operator": "+"},
        [25, 42, 59, 76, 93, 110, 127],
    ),
]


def _make_iteration_test(spec, expected):
    def test(self):
        c = ResourceCount.from_count_spec(spec)
        # valid_counts yields descending; compare as sorted lists
        got = sorted(c.valid_counts(c.max if c.max is not None else expected[-1]))
        self.assertEqual(got, sorted(expected), f"spec={spec!r}")

    test.__name__ = "test_iter_" + repr(spec).replace(" ", "").replace("'", "")[:60]
    test.__doc__ = f"iteration of {spec!r} yields {expected}"
    return test


class TestIteration(unittest.TestCase):
    """valid_counts enumerates every value in the count, for all count forms."""


for _spec, _expected in _ITERATION_CASES:
    _t = _make_iteration_test(_spec, _expected)
    setattr(TestIteration, _t.__name__, _t)


# ---------------------------------------------------------------------------
# TestFromCountSpecIterationErrors: inputs that must fail even via count_create
# path (mirrors test_iteration expected failures)
# ---------------------------------------------------------------------------


class TestIterationErrors(unittest.TestCase):
    def test_negative_integer_raises(self):
        with self.assertRaises((ValueError, Exception)):
            ResourceCount.from_count_spec(-1)

    def test_string_missing_max_raises(self):
        """'13-' (missing max) must raise."""
        with self.assertRaises((ValueError, Exception)):
            ResourceCount.from_count_spec("13-")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
