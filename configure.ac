AC_INIT([libXamine], 0.1, [josh@freedesktop.org])
AC_CONFIG_SRCDIR([libXamine.pc.in])
AM_INIT_AUTOMAKE([foreign dist-bzip2 subdir-objects])

AC_PROG_LIBTOOL
AC_PROG_CC
AM_PROG_CC_C_O
AC_HEADER_STDC

PKG_CHECK_MODULES(LIBXML, libxml-2.0)
AC_SUBST(LIBXML_CFLAGS)
AC_SUBST(LIBXML_LIBS)

AC_OUTPUT([Makefile libXamine.pc])
