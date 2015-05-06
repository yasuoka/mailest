#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([mailest], [0.9.6], [https://github.com/yasuoka/mailest/issues])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_YACC
AC_PROG_RANLIB

CPPFLAGS="$CPPFLAGS -I/usr/include/qdbm -I/usr/include/estraier"

AC_ARG_ENABLE([multi-threads],
    [AS_HELP_STRING(
	[--enable-multi-threads], [enable multi threads (default yes)])],
    [], [enable_multi_threads=yes])

AC_ARG_WITH([libestdraft],
    [AS_HELP_STRING(
	[--with-libestdraft], [use libestdraft (default yes)])],
    [], [with_libestdraft=yes])

AM_CONDITIONAL([ENABLE_MULTI_THREADS], [test x"$enable_multi_threads" != x"no"])
AM_CONDITIONAL([WITH_LIBESTDRAFT], [test x"$with_libestdraft" != x"no"])

# Checks for header files.
AC_CHECK_HEADERS([estraier.h])

AC_CHECK_LIB([qdbm], [cbmalloc],, [AC_MSG_ERROR([requires qdbm library])])
AC_CHECK_LIB([estraier], [est_doc_new],,
    [AC_MSG_ERROR([requires estraier library])])

AS_IF([test x"$enable_multi_threads" != x"no"],
    AC_CHECK_LIB([pthread], [pthread_create]))
AC_CHECK_LIB([event], [event_base_loop])

AC_REPLACE_FUNCS([strlcpy])
AC_REPLACE_FUNCS([strlcat])
AC_REPLACE_FUNCS([strtonum])
AC_REPLACE_FUNCS([reallocarray])
AC_REPLACE_FUNCS([open_memstream])

# Checks for library functions.
AC_CHECK_FUNCS([strlcat])

AC_CONFIG_FILES([GNUmakefile replace/GNUmakefile mailestd/GNUmakefile libestdraft/GNUmakefile])

AM_MAINTAINER_MODE([disable])

AC_OUTPUT
