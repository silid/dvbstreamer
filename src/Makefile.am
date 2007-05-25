## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration

INCLUDES = @INCLTDL@

AM_CFLAGS =\
     -I$(top_srcdir)/include -Wall -Werror


bin_PROGRAMS = dvbstreamer dvbctrl setupdvbstreamer

#
# DVBStreamer
#
dvbstreamer_SOURCES = \
    main.c\
    tuning.c \
    dvb.c\
    objects.c \
    multiplexes.c\
    services.c\
    pids.c\
    ts.c\
    patprocessor.c\
    pmtprocessor.c\
    servicefilter.c\
    dbase.c\
    cache.c\
    logging.c\
    commands.c\
    commands/cmd_pids.c\
    commands/cmd_servicefilter.c\
    commands/cmd_info.c\
    commands/cmd_scanning.c\
    remoteintf.c\
    outputs.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c \
    epgdbase.c \
    utf8.c \
    sdtprocessor.c\
    nitprocessor.c \
    tdtprocessor.c \
    psipprocessor.c \
    dvbtext.c

dvbstreamer_CFLAGS = -I$(top_srcdir)/include -DLOGGING_CHECK_DAEMON -Wall -Werror

dvbstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive 

dvbstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline @GETTIME_LIB@ @READLINE_TERMCAP@ @LIBLTDL@

#
# dvbctrl
#
dvbctrl_SOURCES = \
    dvbctrl.c\
    logging.c

dvbctrl_LDFLAGS =

dvbctrl_LDADD =


#
# dvbctrl
#
setupdvbstreamer_SOURCES = \
    setup.c\
    logging.c\
    parsezap.c\
    multiplexes.c\
    services.c\
    dbase.c \
    objects.c \
    lnb.c

setupdvbstreamer_LDFLAGS =

setupdvbstreamer_LDADD = -lsqlite3 -lpthread

