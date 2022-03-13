#!/usr/bin/env python3

import sys
import sqlite3

if len(sys.argv) < 4:
    print("change-checkpoint.py <file> <key> <value>")
    sys.exit(1)
path = sys.argv[1].encode("utf-8", errors="surrogateescape").decode()
conn = sqlite3.connect(path)
cursor = conn.cursor()
s = (
    'REPLACE INTO checkpt (key,value) values ("'
    + sys.argv[2]
    + '", "'
    + sys.argv[3]
    + '")'
)
cursor.execute(s)
conn.commit()
sys.exit(0)
