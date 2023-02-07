##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import json
import re

import ply.yacc as yacc
from ply import lex


class ConstraintSyntaxError(Exception):
    """
    Specialized SyntaxError exception to allow ConstraintParser to throw
    a SyntaxError without PLY trying to force recovery.

    """

    pass


class ConstraintLexer(object):
    """
    Simple constraint query syntax lexical analyzer based on RFC 35.
    Used mainly as the lexer for BaseConstraintParser
    """

    #  Different quoting states for single vs double quotes:
    states = (
        ("squoting", "exclusive"),
        ("dquoting", "exclusive"),
    )

    tokens = (
        "NOT",
        "AND",
        "OR",
        "NEGATE",
        "LPAREN",
        "RPAREN",
        "TOKEN",
        "QUOTE",
    )

    # Ignore whitespace in default state
    t_ignore = " \t\r\n\f\v"

    # Tokens in 'quoting' state
    t_squoting_ignore = ""
    t_dquoting_ignore = ""

    def __init__(self, **kw_args):
        super().__init__()
        self.lexer = lex.lex(module=self, **kw_args)
        self.parens_level = 0
        self.last_lparens = 0
        self.last_rparens = 0
        self.last_quote = None
        self.quote_start = None
        self.pending_token = None

    def input(self, data):
        self.lexer.push_state("INITIAL")
        self.parens_level = 0
        self.last_lparens = 0
        self.last_rparens = 0
        self.last_quote = None
        self.quote_start = None
        self.lexer.input(data)

    def __getattr__(self, attr):
        return getattr(self.lexer, attr)

    def t_ANY_error(self, t):
        raise ConstraintSyntaxError(
            f"Illegal character '{t.value[0]}' at position {t.lexer.lexpos}"
        )

    #  Define special tokens as functions before t_TOKEN so they are
    #  guaranteed to take precedence.
    #  c.f. http://www.dabeaz.com/ply/ply.html#ply_nn6

    def t_NEGATE(self, t):
        r"-"
        return t

    def t_NOT(self, t):
        r"not\b"
        return t

    def t_AND(self, t):
        r"&{1,2}|and\b"
        return t

    def t_OR(self, t):
        r"\|{1,2}|or\b"
        return t

    def t_LPAREN(self, t):
        r"\("
        self.parens_level += 1
        self.last_lparens = t.lexer.lexpos - 1
        return t

    def t_RPAREN(self, t):
        r"\)"
        self.parens_level -= 1
        self.last_rparens = t.lexer.lexpos - 1
        return t

    def t_TOKEN(self, t):
        r"[^()|&\s\"\']+"
        if self.pending_token is not None:
            t.value = self.pending_token.value + t.value
            self.pending_token = None
        elif t.value.endswith(":"):
            #  Save a token that ends with ':' to possibly combine with
            #  any following token. This allows op:"quoted string"
            self.pending_token = t
            return None
        return t

    def t_eof(self, t):
        if self.pending_token is not None:
            val = self.pending_token.value
            raise ConstraintSyntaxError(f"Missing argument to token '{val}'")
        return None

    def t_QUOTE(self, t):
        r"'|\""  # fmt: skip
        self.quote_start = t.lexer.lexpos
        self.last_quote = t.lexer.lexpos - 1
        if t.value == "'":
            t.lexer.begin("squoting")
        else:
            t.lexer.begin("dquoting")

    #  quoting state:
    def t_squoting_TOKEN(self, t):
        r"([^'])+"
        return self.t_TOKEN(t)

    def t_squoting_QUOTE(self, t):
        r"'"
        self.last_quote = None
        t.lexer.begin("INITIAL")

    def t_squoting_eof(self, t):
        pos = self.quote_start
        raise ConstraintSyntaxError(f'Unclosed quote "\'" at position {pos}')

    def t_dquoting_TOKEN(self, t):
        r"([^\"])+"
        return self.t_TOKEN(t)

    def t_dquoting_QUOTE(self, t):
        r"\""  # fmt: skip
        self.last_quote = None
        t.lexer.begin("INITIAL")

    def t_dquoting_eof(self, t):
        pos = self.quote_start
        raise ConstraintSyntaxError(f"Unclosed quote '\"' at position {pos}")


class ConstraintParser:
    r"""
    Base constraint query syntax parser class.

    This class implements an RFC 35 compliant constraint query syntax
    parser with the following simplified grammar:
    ::

       expr : expr expr
        | expr and expr
        | expr or expr
        | not expr
        | '(' expr ')'
        | '-' term
        | term

       and : &{1,2}|and|AND

       or : \|{1,2}|or|OR

       not : not|NOT

       term : \w*:?.+  # i.e. [operator:]operand

    Where a term is a constraint operation which has the form
    '[operator:]operand'. If the token does not include a `:`, then a
    class default operator may optionally be substituted, e.g. "operand"
    becomes "default:operand".

    By default, ``operand`` is included as a single entry in the RFC 31
    values array for the operator, i.e. ``{"operator":["operand"]}``.
    However, if ``operator`` appears in the self.split_values dict
    of the parser object, then the corresponding string will be used
    to split ``value`` into multiple entries. E.g. if
    ::

        split_values = { "op": "," }

    Then ``op:foo,bar`` would result in ``{"op":["foo","bar"]}``.

    Terms are joined by AND unless OR is specified, such that ``a b c``
    is the same as ``a && b && c``. A term can be negated with ``-``
    (e.g. ``-a b c`` is equivlant to ``(not a)&b&c``), but to negate a
    whole expression, NOT must be used (e.g. ``-(a|b)`` is a syntax error,
    use ``not (a|b)`` instead).

    As a result of parsing, an RFC 31 constraint object is returned as
    a Python dict.

    Attributes:
        operator_map (dict): A mapping of operator names, used to specify
            default and shorthand operator names. To configura a default
            operator, specify ``None`` as a key, e.g.
            ::

               operator_map = { None: "name" }

        split_values (dict): A mapping of operator name to string on which
            to split values of that operator. For instance ``{"op": ","}``
            would autosplit operator ``op`` values on comma.

        combined_terms (set): A set of operator terms whose values can be
            combined when joined with the AND logical operator. E.g. if
            "test" is in ``combined_terms``, then
            ::

                test:a test:b

            would produce
            ::

                {"test": ["a", "b"]}

            instead of
            ::

                {"and": [{"test": ["a"]}, {"test": ["b"]}]}

    Subclasses can set the values of the above attributes to create a
    custom parser with default operators, split value handling, and
    set of combined terms.

    E.g.:
    ::

      class MyConstraintParser(ConstraintParser):
          operator_map = { None: "default", "foo": "long_foo" }
          split_values = { "default": "," }
          combined_terms = set("default")

    By default there is no mapping for ``None`` which means each term
    requires an operator.

    """

    precedence = (
        ("left", "OR"),
        ("left", "AND"),
        ("right", "NOT"),
        ("right", "NEGATE"),
    )

    # Mapping of operator shorthand names to full names
    # Subclasses should provide this mapping
    operator_map = {}

    # Mapping of operator to a string on which to split the operator's
    # value. The value is typically stored as one entry in an array,
    # but if set, the split string can be used to return multiple entries.
    split_values = {}

    # Combined terms
    combined_terms = set()

    def __init__(
        self, lexer=None, optimize=True, debug=False, write_tables=False, **kw_args
    ):
        super().__init__()
        self.lexer = ConstraintLexer() if lexer is None else lexer
        self.tokens = self.lexer.tokens
        self.query = None
        self.parser = yacc.yacc(
            module=self,
            optimize=optimize,
            debug=debug,
            write_tables=write_tables,
            **kw_args,
        )

    def parse(self, query, **kw_args):
        self.query = query
        return self.parser.parse(query, lexer=self.lexer, debug=0, **kw_args)

    def p_error(self, p):
        if p is None:
            if self.lexer.parens_level > 0:
                pos = self.lexer.last_lparens
                raise ConstraintSyntaxError(f"Unclosed parenthesis in position {pos}")
            raise ConstraintSyntaxError(f"Unexpected end of input in '{self.query}'")
        if self.lexer.parens_level < 0:
            raise ConstraintSyntaxError(
                "Mismatched parentheses starting at position {self.lexer.last_rparens}."
            )
        raise ConstraintSyntaxError(f"Invalid token '{p.value}' at position {p.lexpos}")

    def combine_like_terms(self, p1, p2):
        combined_terms = {}
        terms = []
        entries = [p1, p2]

        # First, attempt to combine any "and" terms
        if "and" in p1:
            p1["and"].append(p2)
            entries = [p1]

        # Then, combine any requested combined terms
        for entry in entries:
            key = list(entry)[0]
            if key not in self.combined_terms:
                terms.append(entry)
            elif key not in combined_terms:
                combined_terms[key] = entry[key]
                terms.append(entry)
            else:
                combined_terms[key].extend(entry[key])
        if len(terms) == 1:
            return terms[0]
        else:
            return {"and": terms}

    @staticmethod
    def invalid_operator(op):
        match = re.search(r"[^\w.+@-]", op)
        if not match:
            return None
        return match[0]

    def p_expression_space(self, p):
        """
        expression : expression expression %prec AND
        """
        p[0] = self.combine_like_terms(p[1], p[2])

    def p_expression_and(self, p):
        """
        expression : expression AND expression
        """
        p[0] = self.combine_like_terms(p[1], p[3])

    def p_expression_or(self, p):
        """
        expression : expression OR expression
        """
        if "or" in p[1]:
            # Combine this `or` with a previous one
            p[1]["or"].append(p[3])
            p[0] = p[1]
        else:
            p[0] = {"or": [p[1], p[3]]}

    def p_expression_unot(self, p):
        """
        expression : NOT expression
                   | NEGATE token
        """
        p[0] = {"not": [p[2]]}

    def p_expression_parens(self, p):
        """
        expression : LPAREN expression RPAREN
        """
        p[0] = p[2]

    def p_token(self, p):
        """
        expression : token
        """
        p[0] = p[1]

    def p_expression_token(self, p):
        """
        token : TOKEN
        """
        op, colon, value = p[1].partition(":")
        if not colon:
            if None not in self.operator_map:
                raise ConstraintSyntaxError(f'Missing required operator in "{p[1]}"')
            op = self.operator_map[None]
            value = p[1]
        elif op in self.operator_map:
            op = self.operator_map[op]

        invalid = self.invalid_operator(op)
        if invalid:
            raise ConstraintSyntaxError(
                f"invalid character '{invalid}' in operator '{op}:'"
            )

        if op in self.split_values:
            p[0] = {op: value.split(self.split_values[op])}
        else:
            p[0] = {op: [value]}

    def p_quoted_token(self, p):
        """
        token : QUOTE TOKEN QUOTE
        """
        p[0] = p[2]


if __name__ == "__main__":
    """
    Test command which can be run as flux python -m flux.constraint.parser
    Also used to generate ply's parsetab.py in defined outputdir.
    """

    argparser = argparse.ArgumentParser(prog="constraint.parser")
    argparser.add_argument(
        "--outputdir",
        metavar="DIR",
        type=str,
        help="Set outputdir for parsetab.py generation",
    )
    argparser.add_argument(
        "--default-op",
        metavar="NAME",
        type=str,
        help="Set a default operator to substitute for bare terms",
    )
    argparser.add_argument(
        "--debug",
        action="store_true",
        help="Emit lexer debug information before parsing expression",
    )
    argparser.add_argument(
        "expression",
        metavar="EXPRESSION",
        type=str,
        nargs="*",
        help="Expression to parse",
    )
    args = argparser.parse_args()

    if args.outputdir:
        print(f"Generating constraint parsetab.py in {args.outputdir}")

    class TConstraintParser(ConstraintParser):
        if args.default_op:
            operator_map = {None: args.default_op}

    parser = TConstraintParser(
        optimize=False, debug=True, write_tables=True, outputdir=args.outputdir
    )
    if args.expression:

        s = " ".join(args.expression)
        if args.debug:
            print(f"parsing expression `{s}'")
        if args.debug:
            lexer = ConstraintLexer()
            lexer.input(s)
            while True:
                tok = lexer.token()
                if not tok:
                    break
                print(tok)

        print(json.dumps(parser.parse(s)))
