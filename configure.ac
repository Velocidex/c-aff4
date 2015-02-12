#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([AFF4], [0.1], [aff4@googlegroups.com])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_MACRO_DIR([m4])

SO_VERSION=0:0:0

AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([src/config.h])

AC_LANG(C++)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([raptor2], [raptor_new_term_from_counted_uri_string], [], [
  echo raptor RDF library not found. On debian systems this is provided by
  echp apt-get install libraptor2-dev
  exit -1
])

AC_CHECK_LIB([uuid], [uuid_generate], [], [
  echo uuid library not found. On debian systems this is provided by
  echp apt-get install uuid-dev
  exit -1
])

AC_CHECK_LIB([z], [compress], [], [
  echo zlib not found. On debian systems this is provided by
  echp apt-get install zlib1g-dev
  exit -1
])

AC_CHECK_LIB([yaml-cpp], [main], [], [
  echo libyaml-cpp not found. On debian systems this is provided by
  echp apt-get install libyaml-cpp-dev
  exit -1
])

AC_CHECK_LIB([pcre++], [main]. [], [
  echo Libpcre++ not found. On debian systems this is provided by
  echp apt-get install libpcre++-dev
  exit -1
])

AC_CHECK_LIB([glog], [main], [], [
  echo Google logging library not found. On debian systems this is provided by
  echp apt-get install libgoogle-glog-dev
  exit -1
])

AC_CHECK_LIB([gflags], [main], [], [
  echo Google flags library not found. On debian systems this is provided by
  echp apt-get install libgflags-dev
  exit -1
])

AC_CHECK_LIB(pthread, pthread_create)

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
        tests/Makefile
        ])
AC_OUTPUT
