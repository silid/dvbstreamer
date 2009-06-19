#
# Plugins
#
AM_CFLAGS =\
     -I$(top_srcdir)/include  -Wall -Werror -D_GNU_SOURCE

pluginsdir = @DVBSTREAMER_PLUGINDIR@

plugins_LTLIBRARIES = \
	udpoutput.la \
	nulloutput.la \
	fileoutput.la \
	outputs.la \
	extractpes.la \
	manualfilters.la \
	sicapture.la \
	traffic.la \
	eventsdispatcher.la \
	sectionfilters.la \
	$(atsc_plugins) \
	$(dvb_plugins)

if ENABLE_ATSC
atsc_plugins=\
	atsctoepg.la
else
atsc_plugins=
endif

if ENABLE_DVB
dvb_plugins=\
	lcnquery.la \
	nownext.la \
	datetime.la \
	schedule.la
else
dvb_plugins=
endif

udpoutput_la_SOURCES = \
    udp.c \
    udpoutput.c \
    sap.c

udpoutput_la_LDFLAGS = -module -no-undefined -avoid-version

nulloutput_la_SOURCES = \
    nulloutput.c

nulloutput_la_LDFLAGS = -module -no-undefined -avoid-version

fileoutput_la_SOURCES = \
    fileoutput.c

fileoutput_la_LDFLAGS = -module -no-undefined -avoid-version

outputs_la_SOURCES = \
    outputs.c

outputs_la_LDFLAGS = -module -no-undefined -avoid-version

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

atsctoepg_la_SOURCES = \
    atsctoepg.c

atsctoepg_la_LDFLAGS = -module -no-undefined -avoid-version

manualfilters_la_SOURCES = \
    manualfilters.c

manualfilters_la_LDFLAGS = -module -no-undefined -avoid-version

sicapture_la_SOURCES = \
    sicapture.c

sicapture_la_LDFLAGS = -module -no-undefined -avoid-version

traffic_la_SOURCES = \
    traffic.c

traffic_la_LDFLAGS = -module -no-undefined -avoid-version

eventsdispatcher_la_SOURCES = \
    eventsdispatcher.c

eventsdispatcher_la_LDFLAGS = -module -no-undefined -avoid-version

sectionfilters_la_SOURCES = \
    sectionfilters.c

sectionfilters_la_LDFLAGS = -module -no-undefined -avoid-version
