#!/bin/sh
# flux: -N4 --exclusive
# flux: --job-name=foo --setattr=user.conf="""
# flux: [config]
# flux:   item = "foo"  # an inline comment
# flux: # another comment
# flux: [tab2]
# flux:   b = 'bar'
# flux: """
foo
