#
# Plugins
#
AM_CFLAGS =\
     -I$(top_srcdir)/include  -Wall -Werror 

pluginsdir = @DVBSTREAMER_PLUGINDIR@

plugins_LTLIBRARIES = \
	udpoutput.la \
	nulloutput.la \
	fileoutput.la \
	lcnquery.la \
	extractpes.la \
	nownext.la \
	datetime.la \
	schedule.la \
	epgtoxmltv.la \
	atsctoepg.la

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

lcnquery_la_SOURCES = \
    lcnquery.c

lcnquery_la_LDFLAGS = -module -no-undefined -avoid-version

extractpes_la_SOURCES = \
    extractpes.c

extractpes_la_LDFLAGS = -module -no-undefined -avoid-version

nownext_la_SOURCES = \
    nownext.c

nownext_la_LDFLAGS = -module -no-undefined -avoid-version

datetime_la_SOURCES = \
    datetime.c

datetime_la_LDFLAGS = -module -no-undefined -avoid-version

schedule_la_SOURCES = \
    schedule.c

schedule_la_LDFLAGS = -module -no-undefined -avoid-version

epgtoxmltv_la_SOURCES = \
    epgtoxmltv.c

epgtoxmltv_la_LDFLAGS = -module -no-undefined -avoid-version

atsctoepg_la_SOURCES = \
    atsctoepg.c

atsctoepg_la_LDFLAGS = -module -no-undefined -avoid-version
