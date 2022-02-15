#!/usr/bin/env python3

import sys
import sqlite3

if len(sys.argv) < 4:
    print("change-checkpoint.py <file> <key> <value>")
    sys.exit(1)

conn = sqlite3.connect(sys.argv[1])
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
