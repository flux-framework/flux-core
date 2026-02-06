##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import json

import ply.lex as lex
import ply.yacc as yacc
import yaml


class ShapeSyntaxError(Exception):
    """
    Specialized SyntaxError exception to allow ShapeParser to throw
    a SyntaxError without PLY trying to force recovery.

    """

    pass


class ShapeLexer(object):
    """
    Simple job shape jobspec resources syntax lexical analyzer based on RFC 46.
    Used mainly as the lexer for BaseShapeParser
    """

    #  Different quoting states for single vs double quotes:
    states = (
        ("squoting", "exclusive"),
        ("dquoting", "exclusive"),
        ("count", "exclusive"),
        ("dictkey", "exclusive"),
        ("dictvalue", "exclusive"),
    )

    tokens = (
        "TYPE",
        "SLOT",
        "OPERATOR",
        "IDSET",
        "INTEGER",
        "KEY",
        "FALSE",
        "NULL",
        "TRUE",
        "NUMBER",
        "STRING",
        "QSTRING",
        "ARRAY",
        "LBRACKET",
        "RBRACKET",
        "LBRACE",
        "RBRACE",
    )

    literals = ",;:/+-"

    # Ignore whitespace in default state
    t_ignore = " \t\r\n\f\v"

    # Tokens in 'quoting' state
    t_squoting_ignore = ""
    t_dquoting_ignore = ""
    t_count_ignore = t_ignore
    t_dictkey_ignore = t_dictvalue_ignore = t_ignore

    def __init__(self, **kw_args):
        super().__init__()
        self.lexer = lex.lex(module=self, **kw_args)
        self.bracket_level = 0
        self.last_lbracket = 0
        self.last_rbracket = 0
        self.brace_level = 0
        self.last_lbrace = 0
        self.last_rbrace = 0
        self.slot_count = 0
        self.quote_type = None
        self.quote_start = None
        self.quote_empty = True
        self.array_start = None

    def input(self, data):
        self.lexer.push_state("INITIAL")
        self.bracket_level = 0
        self.last_lbracket = 0
        self.last_rbracket = 0
        self.brace_level = 0
        self.last_lbrace = 0
        self.last_rbrace = 0
        self.slot_count = 0
        self.quote_type = None
        self.quote_start = None
        self.quote_empty = True
        self.array_start = None
        self.lexer.input(data)

    def __getattr__(self, attr):
        return getattr(self.lexer, attr)

    def t_ANY_error(self, t):
        raise ShapeSyntaxError(
            f"Illegal character '{t.value[0]}' at position {t.lexer.lexpos}"
        )

    # Order of functions is important. Define special tokens earlier to have
    # them take precedence.
    # c.f. http://www.dabeaz.com/ply/ply.html#ply_nn6

    def t_squoting_dquoting_eof(self, t):
        raise ShapeSyntaxError(
            f"Unclosed quote {self.quote_type} at position {self.quote_start}"
        )

    def t_squoting_QSTRING(self, t):
        r"[^']+"
        self.quote_empty = False
        return t

    def t_dquoting_QSTRING(self, t):
        r'[^"]+'
        self.quote_empty = False
        return t

    def t_squoting_QUOTE(self, t):
        r"'"
        return self.end_quote_helper(t)

    def t_dquoting_QUOTE(self, t):
        r'"'
        return self.end_quote_helper(t)

    def end_quote_helper(self, t):
        t.lexer.pop_state()
        if self.quote_empty:
            t.value = ""
            t.type = "QSTRING"
        return t

    # This must come after quoting states, so it only lexes starting quote
    def t_ANY_QUOTE(self, t):
        r"\"|'"
        self.quote_start = t.lexer.lexpos
        self.quote_empty = True
        self.quote_type = t.value
        if t.value == "'":
            t.lexer.push_state("squoting")
        else:
            t.lexer.push_state("dquoting")

    def t_TYPE(self, t):
        r"[^={}[\]/;\s\"\']+"
        if t.value.lower() == "slot":
            t.value = "slot"
            t.type = "SLOT"
            self.slot_count += 1
        return t

    def t_EQUALS(self, t):
        r"="
        t.lexer.push_state("count")

    def t_count_OPERATOR(self, t):
        r":[+*^]"
        return t

    def t_count_IDSET(self, t):
        r"[1-9]\d*((?=,)|-[1-9]\d*(?=,))(,[1-9]\d*(-[1-9]\d*)?)*"
        return t

    def t_count_INTEGER(self, t):
        r"[1-9]\d*"
        t.value = int(t.value)
        return t

    def t_count_end(self, t):
        r"(?=[{/;])"
        t.lexer.pop_state()

    def t_dictkey_KEY(self, t):
        r"(?![+-])[^:,{}]+"
        if t.value == "x":
            t.value = "exclusive"
        return t

    def t_dictkey_COLON(self, t):
        r":"
        t.lexer.push_state("dictvalue")
        t.type = ":"
        return t

    def t_dictvalue_ARRAY(self, t):
        r"\[.*]"
        t.lexer.pop_state()
        t.value = json.loads(t.value.replace("'", '"'))
        return t

    def t_dictvalue_NUMBER(self, t):
        r"-?(0|[1-9]\d*)(\.\d*)?((e|E)(-|\+)?\d+)?(?=[},])"
        try:
            t.value = int(t.value)
        except Exception:
            t.value = float(t.value)
        return t

    def t_dictvalue_STRING(self, t):
        r"[^,{}[\]]+"
        if t.value.lower() == "false":
            t.value = False
            t.type = "FALSE"
        elif t.value.lower() == "null":
            t.value = None
            t.type = "NULL"
        elif t.value.lower() == "true":
            t.value = True
            t.type = "TRUE"
        return t

    def t_dictvalue_COMMA(self, t):
        r","
        t.lexer.pop_state()
        t.type = ","
        return t

    def t_dictvalue_RBRACE(self, t):
        r"}"
        t.lexer.pop_state()
        return self.t_ANY_RBRACE(t)

    def t_ANY_LBRACE(self, t):
        r"{"
        self.brace_level += 1
        self.last_lbrace = t.lexer.lexpos - 1
        t.lexer.push_state("dictkey")
        return t

    def t_ANY_RBRACE(self, t):
        r"}"
        self.brace_level -= 1
        self.last_rbrace = t.lexer.lexpos - 1
        t.lexer.pop_state()
        return t

    def t_ANY_LBRACKET(self, t):
        r"\["
        self.bracket_level += 1
        self.last_lbracket = t.lexer.lexpos - 1
        return t

    def t_ANY_RBRACKET(self, t):
        r"]"
        self.bracket_level -= 1
        self.last_rbracket = t.lexer.lexpos - 1
        return t


class ShapeParser:
    r"""
    Base RFC 46 job shape resources syntax parser class.

    This class implements an RFC 46 compliant Jobspec resources syntax
    parser with the following simplified grammar:
    ::

       resources   : list

       list        : '[' vertex (';' vertex)* ']'
                   | vertex

       vertex      : 'slot' ('=' count)? ('{' label (',' item)* '}')? '/' list
                   | type ('=' count)? dict? ('/' list)?

       type        : STRING

       label       : STRING

       count       : '[' count-value ']'
                   | count-value

       count-value : RANGE
                   | IDSET
                   | INTEGER

       dict        : '{' (item (',' item)*)? '}'

       item        : key ':' VALUE
                   | (+|-)? key

       key         : 'x'
                   | STRING


    The result of parsing is an RFC 14 resources list returned as a Python list.

    """

    def __init__(
        self, lexer=None, optimize=True, debug=False, write_tables=False, **kw_args
    ):
        super().__init__()
        self.lexer = ShapeLexer() if lexer is None else lexer
        self.tokens = self.lexer.tokens
        self.query = None
        self.parser = yacc.yacc(
            module=self,
            optimize=optimize,
            debug=debug,
            write_tables=write_tables,
            **kw_args,
        )
        self.default_slot_label = "task"
        self.range_as_dict = False

    def parse(self, query, **kw_args):
        self.query = query
        return self.parser.parse(query, lexer=self.lexer, debug=0, **kw_args)

    def p_error(self, p):
        if p is None:
            if self.lexer.bracket_level > 0:
                pos = self.lexer.last_lbracket
                raise ShapeSyntaxError(f"Unclosed bracket in position {pos}")
            if self.lexer.brace_level > 0:
                pos = self.lexer.last_lbrace
                raise ShapeSyntaxError(f"Unclosed brace in position {pos}")
            raise ShapeSyntaxError(f"Unexpected end of input in '{self.query}'")
        if self.lexer.bracket_level < 0:
            raise ShapeSyntaxError(
                f"Mismatched brackets starting at position {self.lexer.last_rbracket}."
            )
        if self.lexer.brace_level < 0:
            raise ShapeSyntaxError(
                f"Mismatched braces starting at position {self.lexer.last_rbrace}."
            )
        raise ShapeSyntaxError(f"Invalid token '{p.value}' at position {p.lexpos}")

    def p_resources(self, p):
        """
        resources : list
        """
        p[0] = p[1]

    def p_list_brackets(self, p):
        """
        list : LBRACKET vertices RBRACKET
        """
        p[0] = p[2]

    def p_list_single(self, p):
        """
        list : vertex
        """
        p[0] = [p[1]]

    def p_vertices_multi(self, p):
        """
        vertices : vertex ';' vertices
        """
        p[0] = [p[1]] + p[3]

    def p_vertices_single(self, p):
        """
        vertices : vertex
        """
        p[0] = [p[1]]

    def p_vertex_with(self, p):
        """
        vertex : non_slot '/' list
               | slot '/' list
        """
        p[0] = p[1]
        p[0].update({"with": p[3]})

    def p_vertex(self, p):
        """
        vertex : non_slot
        """
        p[0] = p[1]

    def p_non_slot_count_dict(self, p):
        """
        non_slot : TYPE count dict
                 | QSTRING count dict
        """
        p[0] = {"type": p[1], "count": p[2]}
        p[0].update(p[3])

    def p_non_slot_dict(self, p):
        """
        non_slot : TYPE dict
                 | QSTRING dict
        """
        p[0] = {"type": p[1], "count": 1}
        p[0].update(p[2])

    def p_non_slot_count(self, p):
        """
        non_slot : TYPE count
                 | QSTRING count
        """
        p[0] = {"type": p[1], "count": p[2]}

    def p_non_slot(self, p):
        """
        non_slot : TYPE
                 | QSTRING
        """
        p[0] = {"type": p[1], "count": 1}

    def p_slot_label_dict(self, p):
        """
        slot : slot_tc LBRACE key ',' items RBRACE
        """
        p[0] = p[1]
        p[0].update({"label": p[3]})
        p[0].update(p[5])

    def p_slot_label(self, p):
        """
        slot : slot_tc LBRACE key RBRACE
        """
        p[0] = p[1]
        p[0].update({"label": p[3]})

    def p_slot(self, p):
        """
        slot : slot_tc
        """
        if self.lexer.slot_count > 1:
            raise ShapeSyntaxError("Multiple slots require labels")
        p[0] = p[1]
        p[1].update({"label": self.default_slot_label})

    def p_slot_tc_count(self, p):
        """
        slot_tc : SLOT count
        """
        p[0] = {"type": p[1], "count": p[2]}

    def p_slot_tc(self, p):
        """
        slot_tc : SLOT
        """
        p[0] = {"type": p[1], "count": 1}

    def p_count_brackets(self, p):
        """
        count : LBRACKET count_value RBRACKET
        """
        p[0] = p[2]

    def p_count(self, p):
        """
        count : count_value
        """
        p[0] = p[1]

    def p_count_value_range(self, p):
        """
        count_value : INTEGER '-' INTEGER
                    | INTEGER '-' INTEGER ':' INTEGER
                    | INTEGER '-' INTEGER ':' INTEGER OPERATOR
        """
        if self.range_as_dict:
            p[0] = {"min": p[1], "max": p[3], "operand": 1, "operator": "+"}
            if len(p) > 4:
                p[0]["operand"] = p[5]
            if len(p) > 6:
                p[0]["operator"] = p[6][1]
        else:
            p[0] = "".join([str(token) for token in p[1:]])

    def p_count_value_range_nomax(self, p):
        """
        count_value : INTEGER '+'
                    | INTEGER '+' ':' INTEGER
                    | INTEGER '+' ':' INTEGER OPERATOR
        """
        if self.range_as_dict:
            p[0] = {"min": p[1]}
            if len(p) > 3:
                p[0].update(
                    {
                        "max": float("inf"),
                        "operand": p[4],
                        "operator": "+",
                    }
                )
            if len(p) > 5:
                p[0]["operator"] = p[5][1]
        else:
            p[0] = "".join([str(token) for token in p[1:]])

    def p_count_value(self, p):
        """
        count_value : IDSET
                    | INTEGER
        """
        p[0] = p[1]

    def p_dict(self, p):
        """
        dict : LBRACE items RBRACE
        """
        p[0] = p[2]

    def p_dict_empty(self, p):
        """
        dict : LBRACE RBRACE
        """
        p[0] = {}

    def p_items_multi(self, p):
        """
        items : item ',' items
        """
        p[0] = p[1]
        p[0].update(p[3])

    def p_items_single(self, p):
        """
        items : item
        """
        p[0] = p[1]

    def p_item_key_value(self, p):
        """
        item : key ':' value
        """
        p[0] = {p[1]: p[3]}

    def p_item_minus(self, p):
        """
        item : '-' key
        """
        p[0] = {p[2]: False}

    def p_item_plus(self, p):
        """
        item : '+' key
        """
        p[0] = {p[2]: True}

    def p_item_key(self, p):
        """
        item : key
        """
        p[0] = {p[1]: True}

    def p_key(self, p):
        """
        key : KEY
            | QSTRING
        """
        p[0] = p[1]

    def p_value(self, p):
        """
        value : FALSE
              | NULL
              | TRUE
              | dict
              | ARRAY
              | NUMBER
              | STRING
              | QSTRING
        """
        p[0] = p[1]


if __name__ == "__main__":
    """
    Test command which can be run as flux python -m flux.shape.parser
    Also used to generate ply's parsetab.py in defined outputdir.
    """

    argparser = argparse.ArgumentParser(prog="shape.parser")
    argparser.add_argument(
        "--outputdir",
        "-o",
        metavar="DIR",
        type=str,
        help="Set outputdir for parsetab.py generation",
    )
    argparser.add_argument(
        "--debug",
        "-d",
        action="store_true",
        help="Emit lexer debug information before parsing expression",
    )
    argparser.add_argument(
        "--yaml",
        "-y",
        action="store_true",
        help="Output as YAML instead of JSON",
    )
    argparser.add_argument(
        "--rangedict",
        "-r",
        action="store_true",
        help="Store ranges as dicts instead of RFC 45 strings",
    )
    argparser.add_argument(
        "--wrapresources",
        "-w",
        action="store_true",
        help="Wrap result in outer 'resources' key",
    )
    argparser.add_argument(
        "--label",
        "-l",
        action="store",
        default="task",
        help="Label used for single slot if not specified",
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
        print(f"Generating shape parsetab.py in {args.outputdir}")

    parser = ShapeParser(
        optimize=False,
        debug=True,
        write_tables=True,
        outputdir=args.outputdir,
    )
    parser.default_slot_label = args.label
    parser.range_as_dict = args.rangedict

    if args.expression:

        s = " ".join(args.expression)
        if args.debug:
            print(f"parsing expression `{s}'")
            lexer = ShapeLexer()
            lexer.input(s)
            while True:
                tok = lexer.token()
                if not tok:
                    break
                print(tok)

        result = parser.parse(s)
        if args.wrapresources:
            result = {"resources": result}

        if args.yaml:
            print(yaml.dump(result, sort_keys=False))
        else:
            print(json.dumps(result))
