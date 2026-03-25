###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""ResourceCount: parsed RFC 14/22 resource count with iteration support.

A :class:`ResourceCount` wraps one of three count forms that may appear in a
jobspec resource vertex:

- a simple integer (fixed count)
- an RFC 14 range dict ``{"min": N, "max": M, "operator": OP, "operand": K}``
- an RFC 45 string (``"2-8:2:*"``, ``"2+"``, IDset ``"1,7-9"``, …)

The design of the parsing interface mirrors ``libjob/count.h``::

    count_create(json_t *)   →  ResourceCount.from_count_spec(int | dict | str)
    count_first / count_next →  ResourceCount.valid_counts(available)
"""

from flux.idset import IDset


def _expand_sequence(mn, mx, operator, operand):
    """Expand a stepped RFC 14 arithmetic sequence into an IDset.

    Args:
        mn, mx (int): Inclusive bounds of the sequence.
        operator (str): ``"+"`` (additive), ``"*"`` (multiplicative),
            or ``"^"`` (exponential).
        operand (int): Step value.

    Returns:
        IDset: All values in the sequence from *mn* to *mx*.

    Raises:
        ValueError: For unsupported operators, invalid operand/min combinations,
            or sequences that would be empty or excessively large.
    """
    if operator not in ("+", "*", "^"):
        raise ValueError(f"RFC 14 count: operator {operator!r} is not supported")
    if operator in ("*", "^") and operand <= 1:
        raise ValueError(
            f"RFC 14 count: operator {operator!r} requires operand > 1, "
            f"got {operand}"
        )
    if operator == "^" and mn < 2:
        raise ValueError(f"RFC 14 count: operator '^' requires min >= 2, got {mn}")
    if operator == "+" and operand <= 0:
        raise ValueError(
            f"RFC 14 count: additive operator requires operand > 0, got {operand}"
        )
    values = []
    v = mn
    while v <= mx:
        values.append(v)
        if operator == "+":
            v = v + operand
        elif operator == "*":
            v = v * operand
        else:  # "^"
            v = v**operand
        if len(values) > 65536:
            raise ValueError("RFC 14 count: sequence too large to expand")
    if not values:
        raise ValueError(f"RFC 14 count: empty sequence (min={mn}, max={mx})")
    return IDset(",".join(str(x) for x in values))


class ResourceCount:
    """Parsed RFC 14/22 resource count with full iteration support.

    Wraps the three count forms that appear in a jobspec resource vertex
    (integer, RFC 14 range dict, RFC 45 / IDset string) into a uniform object
    that schedulers can iterate over candidate values.

    Attributes:
        min (int): Minimum (or fixed) count value.
        max (int | None): Maximum count value.  ``None`` means unbounded.
        _values (IDset | None): Explicit ordered set of valid values for
            stepped arithmetic and IDset count forms.  ``None`` for simple
            contiguous integer ranges and unbounded ranges.

    See Also:
        :meth:`from_count_spec` — construct from a parsed-jobspec count field.
    """

    __slots__ = ("min", "max", "_values")

    def __init__(self, mn, mx, values=None):
        self.min = mn
        self.max = mx
        self._values = values

    @classmethod
    def from_count_spec(cls, v, default=1):
        """Parse an RFC 14/22 count spec and return a :class:`ResourceCount`.

        Modeled after ``count_create()`` / ``count_decode()`` in
        ``libjob/count.h``: accepts the same three representations that
        appear in a JSON-parsed jobspec resource vertex.

        Supported forms:

        - **Integer**: ``4`` → ``ResourceCount(4, 4)``
        - **Range dict**: ``{"min": 2, "max": 8}`` → ``ResourceCount(2, 8)``
        - **Unbounded dict**: ``{"min": 2}`` → ``ResourceCount(2, None)``
        - **Stepped dict**:
          ``{"min": 2, "max": 8, "operator": "*", "operand": 2}``
          → ``ResourceCount(2, 8, IDset("2,4,8"))``
        - **RFC 45 range string**: ``"2-5"`` → ``ResourceCount(2, 5)``;
          ``"2+"`` → ``ResourceCount(2, None)``
        - **Stepped RFC 45 string**: ``"2-8:2:*"``
          → ``ResourceCount(2, 8, IDset("2,4,8"))``
        - **IDset string**: ``"1,7-9"``
          → ``ResourceCount(1, 9, IDset("1,7-9"))``

        Strings may optionally be surrounded by square brackets per RFC 45.

        ``_values`` is ``None`` for simple contiguous ranges and unbounded
        ranges; set to an :class:`~flux.idset.IDset` for stepped and
        non-contiguous IDset forms.

        Args:
            v: Count spec — an ``int``, ``dict``, or ``str`` as it appears in
                a JSON-decoded jobspec.  Any other type is treated as a fixed
                count equal to *default*.
            default (int): Fallback fixed count when *v* is not a recognised
                type.  Defaults to ``1``.

        Raises:
            ValueError: For unbounded ranges with a non-unit step (cannot be
                expanded to a finite set), unsupported operators, or
                counts less than 1.
        """
        if isinstance(v, int):
            if v < 1:
                raise ValueError(f"RFC 14 count: integer count must be >= 1, got {v}")
            return cls(v, v)
        if isinstance(v, dict):
            operator = v.get("operator", "+")
            operand = int(v.get("operand", 1))
            mn = int(v.get("min", default))
            if mn < 1:
                raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
            mx = v.get("max")
            if mx is None:
                if operator != "+" or operand != 1:
                    raise ValueError(
                        f"RFC 14 unbounded count with operator={operator!r} "
                        f"operand={operand} cannot be expanded to a finite set"
                    )
                return cls(mn, None)
            mx = int(mx)
            if mx < mn:
                raise ValueError(
                    f"RFC 14 count: max must be >= min, got max={mx} min={mn}"
                )
            if operator == "+" and operand == 1:
                return cls(mn, mx)
            return cls(mn, mx, _expand_sequence(mn, mx, operator, operand))
        if isinstance(v, str):
            s = v.strip()
            if s.startswith("[") and s.endswith("]"):
                s = s[1:-1]
            parts = s.split(":")
            # Unbounded RFC 45 range: "min+" or "min+:operand:operator"
            # Detected by the pre-colon segment ending with "+".
            if parts[0].endswith("+"):
                mn = int(parts[0][:-1])
                if mn < 1:
                    raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
                operand = int(parts[1]) if len(parts) > 1 else 1
                operator = parts[2] if len(parts) > 2 else "+"
                if operator != "+" or operand != 1:
                    raise ValueError(
                        f"RFC 45 unbounded count with operator={operator!r} "
                        f"operand={operand} cannot be expanded to a finite set"
                    )
                return cls(mn, None)
            # Bounded RFC 45 range with explicit operand: "min-max:operand[:operator]"
            if len(parts) > 1:
                lo_str, hi_str = parts[0].split("-")
                mn, mx = int(lo_str), int(hi_str)
                if mn < 1:
                    raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
                if mx < mn:
                    raise ValueError(
                        f"RFC 14 count: max must be >= min, got max={mx} min={mn}"
                    )
                operand = int(parts[1])
                operator = parts[2] if len(parts) > 2 else "+"
                if operator == "+" and operand == 1:
                    return cls(mn, mx)
                return cls(mn, mx, _expand_sequence(mn, mx, operator, operand))
            # IDset with commas (non-contiguous values): "1,7-9"
            if "," in s:
                ids = IDset(s)
                mn = ids.first()
                if mn < 1:
                    raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
                return cls(mn, ids.last(), ids)
            # Simple "min-max" range or plain integer
            if "-" in s:
                lo_str, hi_str = s.split("-", 1)
                mn = int(lo_str)
                if mn < 1:
                    raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
                mx = int(hi_str)
                if mx < mn:
                    raise ValueError(
                        f"RFC 14 count: max must be >= min, got max={mx} min={mn}"
                    )
                return cls(mn, mx)
            mn = int(s)
            if mn < 1:
                raise ValueError(f"RFC 14 count: min must be >= 1, got {mn}")
            return cls(mn, mn)
        return cls(default, default)

    def scaled(self, factor):
        """Return a new ResourceCount with all values multiplied by *factor*.

        Returns *self* unchanged when *factor* is 1 (avoids an allocation).
        """
        if factor == 1:
            return self
        mx = self.max * factor if self.max is not None else None
        if self._values is not None:
            new_ids = IDset(",".join(str(v * factor) for v in self._values))
            return ResourceCount(self.min * factor, mx, new_ids)
        return ResourceCount(self.min * factor, mx)

    def valid_counts(self, available):
        """Yield valid count values <= *available*, largest first.

        Mirrors ``count_first()`` / ``count_next()`` from ``libjob/count.h``
        but presents the sequence in descending order so callers can greedily
        try the largest feasible allocation first.

        Args:
            available (int): Upper bound (e.g. number of available resources).

        Yields:
            int: Valid count values in descending order, from
                ``min(available, max)`` (or just *available* if unbounded)
                down to ``min``.  Nothing is yielded when
                ``available < min``.
        """
        cap = min(available, self.max) if self.max is not None else available
        if cap < self.min:
            return
        if self._values is not None:
            for v in sorted(self._values, reverse=True):
                if self.min <= v <= cap:
                    yield v
        else:
            yield from range(cap, self.min - 1, -1)

    def __repr__(self):
        if self._values is not None:
            return f"ResourceCount({self.min}, {self.max}, values={self._values!r})"
        return f"ResourceCount({self.min}, {self.max})"
