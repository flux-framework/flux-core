#!/bin/sh
#
# Run an extra libtoolize before autoreconf to ensure that
# libtool macros can be found if libtool is in PATH, but its
# macros are not in default aclocal search path.
#
echo "Running libtoolize --automake --copy ... "
libtoolize --automake --copy 
echo "Running autoreconf --verbose --install -I config"
autoreconf --verbose --install -I config
echo "Cleaning up ..."
mv aclocal.m4 config/
rm -rf autom4te.cache
echo "Now run ./configure."

