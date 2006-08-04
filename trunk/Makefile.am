## DVBStreamer main Makefile

SUBDIRS = libltdl include src src/plugins

dvbstreamerdocdir = ${prefix}/doc/DVBStreamer
dvbstreamerdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS\
	TODO

pkginclude_DATA = \
    config.h

EXTRA_DIST = $(dvbstreamerdoc_DATA) $(pkginclude_DATA)

# Copy all the spec files. Of cource, only one is actually used.
dist-hook:
	for specfile in *.spec; do \
		if test -f $$specfile; then \
			cp -p $$specfile $(distdir); \
		fi \
	done

