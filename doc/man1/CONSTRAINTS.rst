
A constraint query string is formed by a series of terms.

A term has the form ``operator:operand``, e.g. ``hosts:compute[1-10]``.

Terms may optionally be joined with boolean operators and parenthesis to
allow the formation of more complex constraints or queries.

Boolean operators include logical AND (``&``, ``&&``, or ``and``), logical OR
(``|``, ``||``, or ``or``), and logical negation (``not``).

Terms separated by whitespace are joined with logical AND by default.

Quoting of terms is supported to allow whitespace and other reserved
characters in operand, e.g. ``foo:'this is args'``.

Negation is supported in front of terms such that ``-op:operand`` is
equivalent to ``not op:operand``. Negation is not supported in front of
general expressions, e.g. ``-(a|b)`` is a syntax error.

The full specification of Constraint Query Syntax can be found in RFC 35.

