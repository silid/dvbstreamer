## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration

INCLUDES =

AM_CFLAGS =\
	-I$(srcdir)/../include\
	 -Wall\
	 -g

bin_PROGRAMS = dvbstreamer

dvbstreamer_SOURCES = \
	main.c\
	dvb.c\
	parsezap.c\
	multiplexes.c\
	services.c\
	udpsend.c\
	ts.c\
	udpoutput.c\
	patprocessor.c\
	pmtprocessor.c\
	dbase.c\
	cache.c\
	logging.c

dvbstreamer_LDFLAGS = 

dvbstreamer_LDADD = \
	 -ldvbpsi -lpthread -lsqlite3 -lreadline -ltermcap

