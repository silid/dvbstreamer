#
# Plugins
#
AM_CFLAGS =\
     -I$(top_srcdir)/include

pluginsdir = @DVBSTREAMER_PLUGINDIR@

plugins_LTLIBRARIES = udpoutput.la nulloutput.la fileoutput.la

udpoutput_la_SOURCES = \
    udp.c \
    udpoutput.c

udpoutput_la_LDFLAGS = -module -no-undefined -avoid-version

nulloutput_la_SOURCES = \
    nulloutput.c

nulloutput_la_LDFLAGS = -module -no-undefined -avoid-version

fileoutput_la_SOURCES = \
    fileoutput.c

fileoutput_la_LDFLAGS = -module -no-undefined -avoid-version