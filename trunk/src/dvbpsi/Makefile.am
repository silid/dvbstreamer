AM_CFLAGS =\
     -I$(top_srcdir)/include \
     -I$(top_srcdir)/include/dvbpsi \
     -Wall -Werror
     
noinst_LIBRARIES = libdvbpsi.a 

libdvbpsi_a_SOURCES= \
	demux.c \
	descriptor.c \
	dvbpsi.c \
	psi.c \
	$(mpeg2_descriptors_src) \
	$(mpeg2_tables_src) \
	$(dvb_src) \
	$(atsc_src)
	
if ENABLE_DVB
dvb_src = \
	datetime.c \
	$(dvb_tables_src) \
	$(dvb_descriptors_src)
else
dvb_src =
endif

if ENABLE_DVB
atsc_src = \
	$(atsc_tables_src) \
	$(atsc_descriptors_src)
else
atsc_src =
endif

mpeg2_descriptors_src = \
	descriptors/dr_02.c \
	descriptors/dr_03.c \
	descriptors/dr_04.c \
	descriptors/dr_05.c \
	descriptors/dr_06.c \
	descriptors/dr_07.c \
	descriptors/dr_08.c \
	descriptors/dr_09.c \
	descriptors/dr_0a.c \
	descriptors/dr_0b.c \
	descriptors/dr_0c.c \
	descriptors/dr_0d.c \
	descriptors/dr_0e.c \
	descriptors/dr_0f.c \
	descriptors/dr_83.c

dvb_descriptors_src = \
	descriptors/dr_42.c \
	descriptors/dr_43.c \
	descriptors/dr_47.c \
	descriptors/dr_48.c \
	descriptors/dr_4d.c \
	descriptors/dr_4e.c \
	descriptors/dr_52.c \
	descriptors/dr_55.c \
	descriptors/dr_56.c \
	descriptors/dr_59.c \
	descriptors/dr_5a.c \
	descriptors/dr_62.c \
	descriptors/dr_69.c \
	descriptors/dr_73.c \
	descriptors/dr_76.c

atsc_descriptors_src =

mpeg2_tables_src =  \
	tables/pat.c \
	tables/pmt.c \
	tables/cat.c
	
dvb_tables_src = \
	tables/eit.c \
	tables/sdt.c \
	tables/nit.c \
	tables/tdttot.c

atsc_tables_src =\
	tables/atsc_mgt.c \
	tables/atsc_stt.c \
	tables/atsc_vct.c \
	tables/atsc_eit.c \
	tables/atsc_ett.c




