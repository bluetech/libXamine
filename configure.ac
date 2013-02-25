AC_PREREQ([2.62])
AC_INIT([libXamine], 0.1, [xcb@lists.freedesktop.org])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign dist-bzip2 no-dist-gzip subdir-objects color-tests parallel-tests])
AM_MAINTAINER_MODE([enable])

AC_USE_SYSTEM_EXTENSIONS

LT_INIT

m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.16 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.16)
XORG_DEFAULT_OPTIONS
XORG_MEMORY_CHECK_FLAGS

XORG_TESTSET_CFLAG([BASE_CFLAGS], [-fvisibility=hidden])

PKG_CHECK_MODULES(LIBXML, libxml-2.0)
AC_SUBST(LIBXML_CFLAGS)
AC_SUBST(LIBXML_LIBS)

AC_CONFIG_FILES([
    Makefile
    libXamine.pc
])
AC_OUTPUT
