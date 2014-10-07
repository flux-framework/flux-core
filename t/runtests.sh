#!/bin/sh
rm -rf test-results trash-directory*
SHELL_PATH="/bin/sh"
code=0

if test -z "$srcdir"; then
	srcdir="."
fi

for test in ${srcdir}/t[0-9]*.t; do
	echo "*** $test ***"
	$SHELL_PATH $test
	if [ $? -ne 0 ]; then
		code=1
	fi
done

$SHELL_PATH ${srcdir}/aggregate-results.sh test-results/*

exit $code
