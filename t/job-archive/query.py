###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Query a job-archive db for testing purposes

# Usage: flux python query.py [OPTIONS] dbpath query

import sys
import argparse
import sqlite3

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-t", "--timeout", type=str, metavar="MS", help="Set busytimeout"
    )
    parser.add_argument(
        "dbpath", type=str, metavar="DBPATH", nargs=1, help="database path"
    )
    parser.add_argument("query", type=str, metavar="QUERY", nargs=1, help="query")
    args = parser.parse_args()

    try:
        dburi = "file:" + args.dbpath[0] + "?mode=ro"
        con = sqlite3.connect(dburi, uri=True)
    except sqlite3.Error as e:
        print(e)
        sys.exit(1)

    if args.timeout:
        con.execute("PRAGMA busy_wait = " + args.timeout)

    con.row_factory = sqlite3.Row
    cursor = con.cursor()

    try:
        cursor.execute(args.query[0])
    except sqlite3.Error as e:
        print(e)
        sys.exit(1)

    rows = cursor.fetchall()

    for row in rows:
        for key in row.keys():
            print(key + " = " + str(row[key]))

    con.close()
    sys.exit(0)

# vim: tabstop=4 shiftwidth=4 expandtab
