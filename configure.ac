#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([AFF4], [0.1], [aff4-users@googlegroups.com], [],
                [http://www.aff4.org/])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_LANG_SOURCE

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
  echo apt-get install libraptor2-dev
  exit -1
])

AC_CHECK_LIB(uuid, uuid_generate)

AC_CHECK_LIB([z], [compress], [], [
  echo zlib not found. On debian systems this is provided by
  echo apt-get install zlib1g-dev
  exit -1
])

AC_ARG_ENABLE([yaml],
  AS_HELP_STRING([--enable-yaml], [Enable YAML support.]))

AS_IF([test "x$enable_yaml" = "xyes"],
  AC_CHECK_LIB([yaml-cpp], [main], [
    AC_SUBST(HAVE_LIBYAML_CPP, ["#define AFF4_HAS_LIBYAML_CPP"])
  ], [
    echo libyaml-cpp not found. On debian systems this is provided by
    echo apt-get install libyaml-cpp-dev
    exit -1
  ])
)
AM_CONDITIONAL([HAVE_LIBYAML_CPP], [test x$ac_cv_lib_yaml_cpp_main = xyes])

AC_CHECK_LIB([pcre++], [main]. [], [
  echo Libpcre++ not found. On debian systems this is provided by
  echo apt-get install libpcre++-dev
  exit -1
])

AC_CHECK_LIB([glog], [main], [], [
  echo Google logging library not found. On debian systems this is provided by
  echo apt-get install libgoogle-glog-dev
  exit -1
])

AC_CHECK_LIB([snappy], [main], [], [
  echo Google snappy compression library not found. On debian systems this is provided by
  echo apt-get install libsnappy-dev
  exit -1
])

AC_CHECK_HEADER([tclap/CmdLine.h], [main], [], [
  echo The TCLAP flag parsing library is not found. On Debian systems this is provided by
  echo apt-get install libtclap-dev
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

AC_CHECK_SIZEOF([off_t])
AC_CHECK_SIZEOF([size_t])

AM_PROG_AR
LT_INIT
AM_CONDITIONAL(GCC, test "$GCC" = yes)   # let the Makefile know if we're gcc

AC_ARG_ENABLE([static-binaries],
  AS_HELP_STRING([--enable-static-binaries], [Build completely static binaries.]))

AM_CONDITIONAL([STATIC_BUILD], [test x$enable_static_binaries = xyes])

# Check for windows.
MINGW_AC_WIN32_NATIVE_HOST
URIPARSER_CHECK

AM_CONDITIONAL([WIN32_NATIVE_HOST], [test x$mingw_cv_win32_host = xyes])

# Checks for library functions.
AC_CHECK_FUNCS([ftruncate localtime_r memset setenv])

AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_SUBST(SO_VERSION)

AC_CONFIG_FILES([
        Makefile
        src/Makefile
        src/aff4_config.h
        tests/Makefile
        ])
AC_OUTPUT
