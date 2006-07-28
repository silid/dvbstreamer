## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration

INCLUDES =

AM_CFLAGS =\
	-I$(srcdir)/../include\
	 -Wall\
	 -g

bin_PROGRAMS = dvbstreamer dvbctrl

dvbstreamer_SOURCES = \
	main.c\
	dvb.c\
	parsezap.c\
	multiplexes.c\
	services.c\
	udp.c\
	ts.c\
	udpoutput.c\
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
    pluginmgr.c

dvbstreamer_LDFLAGS =

dvbstreamer_LDADD = \
	 -ldvbpsi -lpthread -lsqlite3 -lreadline -ltermcap

dvbctrl_SOURCES = \
    dvbctrl.c\
    messages.c\
    logging.c

dvbctrl_LDFLAGS =

dvbctrl_LDADD =
