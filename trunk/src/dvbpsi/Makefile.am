AM_CFLAGS =\
     -I$(top_srcdir)/include \
     -I$(top_srcdir)/include/dvbpsi
     
noinst_LIBRARIES = libdvbpsi.a

libdvbpsi_a_SOURCES= \
	demux.c \
	descriptor.c \
	dvbpsi.c \
	psi.c \
	datetime.c \
	$(descriptors_src) \
	$(tables_src)
	
descriptors_src = \
	descriptors/dr_0a.c \
	descriptors/dr_0b.c \
	descriptors/dr_0c.c \
	descriptors/dr_0d.c \
	descriptors/dr_0e.c \
	descriptors/dr_0f.c \
	descriptors/dr_02.c \
	descriptors/dr_03.c \
	descriptors/dr_04.c \
	descriptors/dr_4d.c \
	descriptors/dr_4e.c \
	descriptors/dr_05.c \
	descriptors/dr_06.c \
	descriptors/dr_07.c \
	descriptors/dr_08.c \
	descriptors/dr_09.c \
	descriptors/dr_42.c \
	descriptors/dr_47.c \
	descriptors/dr_48.c \
	descriptors/dr_52.c \
	descriptors/dr_55.c \
	descriptors/dr_56.c \
	descriptors/dr_59.c \
	descriptors/dr_83.c \
	descriptors/dr_62.c

tables_src =  \
	tables/eit.c \
	tables/pat.c \
	tables/pmt.c \
	tables/sdt.c \
	tables/nit.c \
	tables/tdttot.c


