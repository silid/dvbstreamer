## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration

INCLUDES = @INCLTDL@

AM_CFLAGS =\
     -I$(top_srcdir)/include


if ATSC
bin_PROGRAMS = atscstreamer atscctrl setupatscstreamer
else
bin_PROGRAMS = dvbstreamer dvbctrl setupdvbstreamer
endif


# source common to DVB and ATSC streamer
common_src = \
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
    remoteintf.c\
    outputs.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c

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
    remoteintf.c\
    outputs.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c \
    sdtprocessor.c\
    nitprocessor.c \
    tdtprocessor.c

dvbstreamer_CFLAGS = -I$(top_srcdir)/include -DLOGGING_CHECK_DAEMON -DDVB_STREAMER

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
    objects.c 

setupdvbstreamer_LDFLAGS =

setupdvbstreamer_LDADD = -lsqlite3


#
# ATSCStreamer
#
atscstreamer_SOURCES = \
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
    remoteintf.c\
    outputs.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c \
    psipprocessor.c
    
atscstreamer_CFLAGS = -I$(top_srcdir)/include -DLOGGING_CHECK_DAEMON -DATSC_STREAMER

atscstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive 

atscstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline @GETTIME_LIB@ @READLINE_TERMCAP@ @LIBLTDL@
	  
#
# atscctrl
#
atscctrl_SOURCES = \
    dvbctrl.c\
    logging.c

atscctrl_LDFLAGS =

atscctrl_LDADD =


#
# setupatscstreamer
#
setupatscstreamer_SOURCES = \
    setup.c\
    logging.c\
    parsezap.c\
    multiplexes.c\
    services.c\
    dbase.c \
    objects.c 

setupatscstreamer_CFLAGS = -I$(top_srcdir)/include -DATSC_STREAMER

setupatscstreamer_LDFLAGS =

setupatscstreamer_LDADD = -lsqlite3
