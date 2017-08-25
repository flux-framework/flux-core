#!/bin/sh
#
# Run an extra libtoolize before autoreconf to ensure that
# libtool macros can be found if libtool is in PATH, but its
# macros are not in default aclocal search path.
#
echo "Running libtoolize --automake --copy ... "
libtoolize --automake --copy || exit
echo "Running autoreconf --verbose --install"
autoreconf --verbose --install || exit
echo "Moving aclocal.m4 to config/ ..."
mv aclocal.m4 config/ || exit
echo "Now run ./configure."
