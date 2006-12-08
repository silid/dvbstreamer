## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration

INCLUDES = @INCLTDL@

AM_CFLAGS =\
     -I$(top_srcdir)/include

bin_PROGRAMS = dvbstreamer dvbctrl

#
# DVBStreamer
#
dvbstreamer_SOURCES = \
	main.c\
	dvb.c\
	parsezap.c\
	multiplexes.c\
	services.c\
	pids.c\
	ts.c\
	patprocessor.c\
	pmtprocessor.c\
	sdtprocessor.c\
    servicefilter.c\
	dbase.c\
	cache.c\
	logging.c\
    commands.c\
    binarycomms.c\
    messages.c\
    outputs.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    nitprocessor.c \
    tdtprocessor.c \
    dvbpsi/nit.c \
    dvbpsi/tdttot.c \
    sectionprocessor.c

dvbstreamer_LDFLAGS = -rdynamic

dvbstreamer_LDADD = \
	 -ldvbpsi -lpthread -lsqlite3 -lreadline -ltermcap -lrt @LIBLTDL@

#
# dvbctrl
#
dvbctrl_SOURCES = \
    dvbctrl.c\
    messages.c\
    logging.c

dvbctrl_LDFLAGS =

dvbctrl_LDADD =


