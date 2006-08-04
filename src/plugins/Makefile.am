#
# Plugins
#
AM_CFLAGS =\
     -I$(top_srcdir)/include \
	 -Wall\
	 -g

pluginsdir = @DVBSTREAMER_PLUGINDIR@

plugins_LTLIBRARIES = udpoutput.la

udpoutput_la_SOURCES = \
    udp.c \
    udpoutput.c

udpoutput_la_LDFLAGS = -module -no-undefined -avoid-version
