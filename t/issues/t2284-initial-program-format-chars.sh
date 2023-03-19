#!/bin/sh -e
# broker cmdline preserves format characters

for s in %h %g %%h %f; do
    echo "Running flux broker echo $s"
    output=$(flux broker -Sbroker.rc1_path= -Sbroker.rc3_path= echo $s)
    test "$output" = "$s"
done
exit 0
