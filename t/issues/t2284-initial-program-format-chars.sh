#!/bin/sh -e
# broker cmdline preserves format characters

export FLUX_RC1_PATH=""
export FLUX_RC3_PATH=""
for s in %h %g %%h %f; do
    echo "Running flux broker echo $s"
    output=$(flux broker /bin/echo $s)
    test "$output" = "$s"
done
exit 0
