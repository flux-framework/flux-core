.. option:: --requires=CONSTRAINT

   Specify a set of allowable properties and other attributes to consider
   when matching resources for a job. The **CONSTRAINT** is expressed in
   a simple syntax described in RFC 35 (Constraint Query Syntax) which is
   then converted into a JSON object compliant with RFC 31 (Job Constraints
   Specification).

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

   Currently, :option:`--requires` supports the following operators:

   properties
     Require the set of specified properties. Properties may be
     comma-separated, in which case all specified properties are required.
     As a convenience, if a property starts with ``-`` then a matching
     resource must *not* have the specified property. In these commands,
     the ``properties`` operator is the default, so that ``a,b`` is equivalent
     to ``properties:a,b``.

   hostlist
     Require matching resources to  be in the specified hostlist (in RFC
     29 format). ``host`` or ``hosts`` is also accepted.

   ranks
     Require matching resources to be on the specified broker ranks in
     RFC 22 Idset String Representation.

   Examples:

   ``a b c``, ``a&b&c``, or ``a,b,c``
      Require properties a and b and c.

   ``a|b|c``, or ``a or b or c``
      Require property a or b or c.

   ``(a and b) or (b and c)``
      Require properties a and b or b and c.

   ``b|-c``, ``b or not c``
      Require property b or not c.

   ``host:fluke[1-5]``
      Require host in fluke1 through fluke5.

   ``-host:fluke7``
      Exclude host fluke7.

   ``rank:0``
      Require broker rank 0.


