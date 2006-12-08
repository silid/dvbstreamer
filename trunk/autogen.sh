#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
echo processing $srcdir

(
cd $srcdir
aclocalinclude="$ACLOCAL_FLAGS"
echo "Creating $srcdir/aclocal.m4 ..."
test -r $srcdir/aclocal.m4 || touch $srcdir/aclocal.m4
echo "Making $srcdir/aclocal.m4 writable ..."
test -r $srcdir/aclocal.m4 && chmod u+w $srcdir/aclocal.m4
echo "Running libtoolize..."
libtoolize --force --copy
echo "Running aclocal $aclocalinclude ..."
aclocal $aclocalinclude
echo "Running autoheader..."
autoheader
echo "Running automake --gnu $am_opt ..."
automake --add-missing --gnu $am_opt
echo "Running autoconf ..."
autoconf
)
