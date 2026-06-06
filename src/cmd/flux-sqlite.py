#!/usr/bin/env python3
##############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import sys

import flux


def query(args):
    """Execute SQL query against content-sqlite database"""
    h = flux.Flux()
    try:
        future = h.rpc(
            "content-sqlite.query",
            {"query": args.query, "force": args.force},
        )
        result = future.get()

        if "error" in result:
            print(f"Error: {result['error']}", file=sys.stderr)
            sys.exit(1)

        rows = result.get("rows", [])
        columns = result.get("columns", [])

        if not rows:
            return

        # Calculate column widths for column mode
        widths = []
        if args.column and columns and rows:
            for i, col in enumerate(columns):
                max_width = len(str(col))
                for row in rows:
                    if i < len(row):
                        max_width = max(max_width, len(str(row[i])))
                widths.append(max_width)

        # Print headers if requested
        if args.header and columns:
            if args.column:
                header_line = "  ".join(
                    col.ljust(widths[i]) for i, col in enumerate(columns)
                )
                print(header_line)
                print("-" * len(header_line))
            else:
                print("|".join(columns))

        # Print rows
        for row in rows:
            if args.column:
                if columns:
                    print(
                        "  ".join(
                            str(row[i]).ljust(widths[i]) if i < len(row) else ""
                            for i in range(len(columns))
                        )
                    )
                else:
                    print("  ".join(str(val) for val in row))
            else:
                print("|".join(str(val) for val in row))

    except OSError as e:
        print(f"RPC error: {e}", file=sys.stderr)
        sys.exit(1)


def backup(args):
    """Create database backup using SQLite backup API"""
    h = flux.Flux()
    try:
        future = h.rpc("content-sqlite.backup", {"path": args.path})
        future.get()
        print(f"Backup created: {args.path}")
    except OSError as e:
        print(f"RPC error: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(prog="flux-sqlite")
    subparsers = parser.add_subparsers(
        title="subcommands",
        description="",
        dest="subcommand",
    )
    subparsers.required = True

    # Query subcommand
    query_parser = subparsers.add_parser(
        "query",
        help="Execute SQL query",
    )
    query_parser.add_argument(
        "query",
        help="SQL query to execute (SELECT, PRAGMA, DELETE, or VACUUM)",
    )
    query_parser.add_argument(
        "-H",
        "--header",
        action="store_true",
        help="Print column headers",
    )
    query_parser.add_argument(
        "-c",
        "--column",
        action="store_true",
        help="Column mode output",
    )
    query_parser.add_argument(
        "--force",
        action="store_true",
        help="Allow destructive operations (DELETE, VACUUM)",
    )
    query_parser.set_defaults(func=query)

    # Backup subcommand
    backup_parser = subparsers.add_parser(
        "backup",
        help="Create database backup",
    )
    backup_parser.add_argument(
        "path",
        help="Path to backup file",
    )
    backup_parser.set_defaults(func=backup)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
