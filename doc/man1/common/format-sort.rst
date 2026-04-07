If the format string begins with ``sort:k1[,k2,...]``, then ``k1[,k2,...]``
will be taken to be a comma-separated list of keys on which to sort
the displayed output. If a sort key starts with ``-``, then the key
will be sorted in reverse order.

Sort keys can be any valid field name. Fields that may be empty or unset
will sort before non-empty values. When sorting fields that contain mixed
types, the sort order is: empty/None < numbers (including booleans) < strings.
Booleans are treated as numeric values (False=0, True=1).

For example, to sort by a numeric field with empty values first::

   --format='sort:nnodes {id} {nnodes} {status}'

Or to sort in reverse order (largest first, empty values last)::

   --format='sort:-nnodes {id} {nnodes} {status}'

Multiple sort keys can be specified, with earlier keys taking precedence::

   --format='sort:state,-t_submit {id} {state} {t_submit}'
