#
# Plugins
#
AM_CFLAGS =\
     -I$(top_srcdir)/include -D_GNU_SOURCE -fno-strict-aliasing

pluginsdir = @DVBSTREAMER_PLUGINDIR@

plugins_LTLIBRARIES = \
	udpoutput.la \
	fileoutput.la \
	pipeoutput.la \
	outputs.la \
	manualfilters.la \
	sicapture.la \
	eventsdispatcher.la \
	dsmcc.la \
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
	datetime.la \
	dvbtoepg.la
else
dvb_plugins=
endif

udpoutput_la_SOURCES = \
    udp.c \
    udpoutput.c \
    sap.c

udpoutput_la_LDFLAGS = -module -no-undefined -avoid-version

fileoutput_la_SOURCES = \
    fileoutput.c

fileoutput_la_LDFLAGS = -module -no-undefined -avoid-version

pipeoutput_la_SOURCES = \
    pipeoutput.c

pipeoutput_la_LDFLAGS = -module -no-undefined -avoid-version

outputs_la_SOURCES = \
    outputs.c

outputs_la_LDFLAGS = -module -no-undefined -avoid-version

lcnquery_la_SOURCES = \
    lcnquery.c

lcnquery_la_LDFLAGS = -module -no-undefined -avoid-version

datetime_la_SOURCES = \
    datetime.c

datetime_la_LDFLAGS = -module -no-undefined -avoid-version

dvbtoepg_la_SOURCES = \
    dvbtoepg.c \
    freesat_huffman.c

dvbtoepg_la_LDFLAGS = -module -no-undefined -avoid-version

atsctoepg_la_SOURCES = \
    atsctoepg.c

atsctoepg_la_LDFLAGS = -module -no-undefined -avoid-version

manualfilters_la_SOURCES = \
    manualfilters.c

manualfilters_la_LDFLAGS = -module -no-undefined -avoid-version

sicapture_la_SOURCES = \
    sicapture.c

sicapture_la_LDFLAGS = -module -no-undefined -avoid-version

# TODO: Rewrite to use new TSFilterGroup system.
#traffic_la_SOURCES = \
#    traffic.c

#traffic_la_LDFLAGS = -module -no-undefined -avoid-version

eventsdispatcher_la_SOURCES = \
    eventsdispatcher.c

eventsdispatcher_la_LDFLAGS = -module -no-undefined -avoid-version

# TODO: Rewrite to use new TSFilterGroup system.
#sectionfilters_la_SOURCES = \
#   sectionfilters.c

#sectionfilters_la_LDFLAGS = -module -no-undefined -avoid-version

dsmcc_la_SOURCES = \
    dsmcc.c \
    libdsmcc/dsmcc-biop.c \
    libdsmcc/dsmcc-cache.c \
    libdsmcc/dsmcc-carousel.c \
    libdsmcc/dsmcc-descriptor.c \
    libdsmcc/dsmcc-receiver.c \
    libdsmcc/dsmcc-util.c

dsmcc_la_LIBADD = -lz
dsmcc_la_LDFLAGS = -module -no-undefined -avoid-version
