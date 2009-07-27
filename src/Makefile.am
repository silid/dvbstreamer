## Process this file with automake to produce Makefile.in

INCLUDES = @INCLTDL@

AM_CFLAGS =\
     -I$(top_srcdir)/include -Wall -Werror -D_GNU_SOURCE


bin_PROGRAMS = dvbstreamer dvbctrl setupdvbstreamer $(fstreamer_app)

#
# DVBStreamer
#
dvbstreamer_SOURCES = \
    main.c\
    tuning.c \
    dvbadapter.c\
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
    commands/cmd_servicefilter.c\
    commands/cmd_info.c\
    commands/cmd_scanning.c\
    commands/cmd_epg.c\
    remoteintf.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c \
    epgtypes.c \
    epgchannel.c \
    utf8.c \
    events.c \
    properties.c \
    threading/messageq.c \
    threading/deferredproc.c \
    $(atsc_src) \
    $(dvb_src)

if ENABLE_ATSC
atsc_src =\
    atsctext.c \
    psipprocessor.c
else
atsc_src =
endif

if ENABLE_DVB
dvb_src =\
    sdtprocessor.c\
    nitprocessor.c \
    tdtprocessor.c \
    dvbtext.c
else
dvb_src =
endif

dvbstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive

dvbstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline @GETTIME_LIB@ @ICONV_LIB@ @READLINE_TERMCAP@ @LIBLTDL@

if ENABLE_FSTREAMER
fstreamer_app = fdvbstreamer
#
# File DVBStreamer
#
fdvbstreamer_SOURCES = \
    main.c\
    tuning.c \
    fileadapter.c\
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
    commands/cmd_servicefilter.c\
    commands/cmd_info.c\
    commands/cmd_scanning.c\
    commands/cmd_epg.c\
    remoteintf.c\
    list.c\
    deliverymethod.c\
    pluginmgr.c \
    sectionprocessor.c \
    pesprocessor.c \
    subtableprocessor.c \
    epgtypes.c \
    epgchannel.c \
    utf8.c \
    events.c \
    properties.c \
    threading/messageq.c \
    threading/deferredproc.c \
    $(atsc_src) \
    $(dvb_src)

fdvbstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive

fdvbstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline @GETTIME_LIB@ @ICONV_LIB@ @READLINE_TERMCAP@ @LIBLTDL@
else
fstreamer_app =
endif

#
# dvbctrl
#
dvbctrl_SOURCES = \
    dvbctrl.c\
    logging.c

dvbctrl_LDFLAGS =

dvbctrl_LDADD = -lreadline @READLINE_TERMCAP@ 


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
    events.c \
    list.c \
    lnb.c

setupdvbstreamer_LDFLAGS =

setupdvbstreamer_LDADD = -lsqlite3 -lpthread

