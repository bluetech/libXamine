ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libXamine.pc

AM_CPPFLAGS = \
	-include $(top_builddir)/src/config.h

AM_CFLAGS = $(BASE_CFLAGS)

lib_LTLIBRARIES = libXamine.la

include_HEADERS = \
	src/xamine.h

libXamine_la_SOURCES = \
	src/xamine.c \
	src/utils.c \
	src/utils.h

libXamine_la_CFLAGS = $(AM_CFLAGS) $(LIBXML_CFLAGS)
libXamine_la_LIBADD = $(LIBXML_LIBS)

# Tests

test_ev_LDADD = libXamine.la -lxcb $(LIBXML_LIBS)
test_ev_CFLAGS = $(AM_CFLAGS) $(LIBXML_CFLAGS)

check_PROGRAMS = \
	test/ev
