noinst_PROGRAMS = stream
lib_LTLIBRARIES = libes2ts.la

libes2ts_includedir = $(includedir)/libes2ts
libes2ts_include_HEADERS = \
	libes2ts/es2ts.h \
	libes2ts/xorg-list.h

libes2ts_la_SOURCES = \
	es2ts.c \
	$(include_HEADERS)
libes2ts_la_CFLAGS = @PTHREAD_CFLAGS@ @LIBAV_CFLAGS@ -fPIC
libes2ts_la_LIBADD = @PTHREAD_LIBS@ @LIBAV_LIBS@

stream_SOURCES = stream.c
stream_LDADD = libes2ts.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libes2ts.pc
