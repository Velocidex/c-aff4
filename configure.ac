#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([AFF4], [0.1], [aff4@googlegroups.com])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_MACRO_DIR([m4])

SO_VERSION=0:0:0

AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([src/config.h])

# Optionally use gtest
AC_ARG_WITH([gtest],
  [AS_HELP_STRING([--with-gtest], [location of Google Test source])],
  [GTEST_DIR=$withval])
AC_SUBST([GTEST_DIR])

AC_ARG_WITH([gmock],
  [AS_HELP_STRING([--with-gmock], [location of Google Mock source])],
  [GMOCK_DIR=$withval])
AC_SUBST([GMOCK_DIR])

AC_LANG(C++)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([raptor2], [raptor_new_term_from_counted_uri_string])
AC_CHECK_LIB([uuid], [uuid_generate])
AC_CHECK_LIB([z], [compress])
AC_CHECK_LIB([yaml-cpp], [main])
AC_CHECK_LIB([pcre++], [main])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

AM_PROG_AR
AM_CONDITIONAL(GCC, test "$GCC" = yes)   # let the Makefile know if we're gcc

# Checks for library functions.
AC_CHECK_FUNCS([ftruncate localtime_r memset setenv])

AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_SUBST(SO_VERSION)

AC_CONFIG_FILES([
        Makefile
        src/Makefile
        ])
AC_OUTPUT
