## Process this file with automake to produce Makefile.in

INCLUDES = @INCLTDL@

AM_CFLAGS =\
     -I$(top_srcdir)/include -Wall -Werror -D_GNU_SOURCE


bin_PROGRAMS = dvbstreamer dvbctrl setupdvbstreamer $(fstreamer_app) convertdvbdb

common_src = \
    main.c\
    tuning.c \
    ts.c\
    multiplexes.c\
    services.c\
    pids.c\
    dbase.c\
    standard/mpeg2/mpeg2.c\
    standard/mpeg2/patprocessor.c\
    standard/mpeg2/pmtprocessor.c\
    servicefilter.c\
    cache.c\
    commands.c\
    commands/cmd_servicefilter.c\
    commands/cmd_info.c\
    commands/cmd_scanning.c\
    commands/cmd_epg.c\
    dispatchers.c \
    remoteintf.c\
    deliverymethod.c\
    pluginmgr.c \
    epgtypes.c \
    epgchannel.c \
    utf8.c \
    events.c \
    objects.c \
    list.c\
    logging.c\
    properties.c \
    threading/messageq.c \
    threading/deferredproc.c \
    lnb.c \
    yamlutils.c \
    constants.c 

if ENABLE_ATSC
atsc_src =\
    standard/atsc/atsc.c \
    standard/atsc/atsctext.c \
    standard/atsc/psipprocessor.c
else
atsc_src =
endif

if ENABLE_DVB
dvb_src =\
    standard/dvb/dvb.c\
    standard/dvb/sdtprocessor.c\
    standard/dvb/nitprocessor.c \
    standard/dvb/tdtprocessor.c \
    standard/dvb/dvbtext.c
else
dvb_src =
endif

#
# DVBStreamer
#
dvbstreamer_SOURCES = \
    dvbadapter.c\
    $(common_src) \
    $(atsc_src) \
    $(dvb_src)


dvbstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive

dvbstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline -lev -lyaml @GETTIME_LIB@ @ICONV_LIB@ @READLINE_TERMCAP@ @LIBLTDL@


if ENABLE_FSTREAMER
fstreamer_app = fdvbstreamer
#
# File DVBStreamer
#
fdvbstreamer_SOURCES = \
    fileadapter.c\
    $(common_src) \
    $(atsc_src) \
    $(dvb_src)

fdvbstreamer_LDFLAGS = -rdynamic -Wl,-whole-archive -Wl,dvbpsi/libdvbpsi.a -Wl,-no-whole-archive

fdvbstreamer_LDADD = \
	  -lpthread -lsqlite3 -lreadline -lev  -lyaml @GETTIME_LIB@ @ICONV_LIB@ @READLINE_TERMCAP@ @LIBLTDL@
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
# setupdvbstreamer
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
    lnb.c \
    yamlutils.c

setupdvbstreamer_LDFLAGS =

setupdvbstreamer_LDADD = -lsqlite3 -lpthread -lyaml

#
# convertdvbdb
#
convertdvbdb_SOURCE = \
	convertdvbdb.c

convertdvbdb_LDFLAGS =

convertdvbdb_LDADD =-lsqlite3


