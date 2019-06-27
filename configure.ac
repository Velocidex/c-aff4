#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([AFF4], [2.0 pre-release], [aff4-users@googlegroups.com], [],
                [http://www.aff4.org/])

SO_VERSION=2:0:0

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([aff4])
AC_CONFIG_HEADERS([aff4/config.h])
AC_CANONICAL_HOST
AC_SUBST(ARCH,[host_cpu])

AM_INIT_AUTOMAKE([1.10 no-define foreign subdir-objects])
# Checks for programs.
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AM_PROG_AR
LT_INIT
AC_PROG_LIBTOOL
AC_LANG(C++)
AX_CXX_COMPILE_STDCXX_11([noext],[mandatory])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDC
AC_CHECK_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h unistd.h sstream climits cstring])

AC_CHECK_SIZEOF([off_t])
AC_CHECK_SIZEOF([size_t])

AC_SYS_LARGEFILE

# Checks for library functions.
AC_CHECK_FUNCS([ftruncate localtime_r memset setenv])

# Stick in "-Werror" if you want to be more aggressive.
# (No need to use AC_SUBST on this default substituted environment variable.)
# Only add these additional CFLAGS if we are using GCC. Other C compilers may
# not support them.
if test x"$GCC" == "xyes" ; then
    AX_CHECK_COMPILE_FLAG([ -Wall -Wextra -Wpedantic -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare ],
        [CXXFLAGS="$CXXFLAGS -Wall -Wextra -Wpedantic -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare "])
fi

# Checks for libraries.
PKG_CHECK_MODULES([RAPTOR2], [raptor2], [], [AC_MSG_ERROR([raptor RDF library (libraptor2-dev) not found])])

PKG_CHECK_MODULES([ZLIB], [zlib], [], [AC_MSG_ERROR([zlib library (zlib1g-dev) not found])])
AC_CHECK_LIB([snappy], [main], [], [AC_MSG_ERROR([Google Snappy Compression library (libsnappy-dev) not found])])
AC_CHECK_LIB([lz4], [main], [], [AC_MSG_ERROR([LZ4 Compression library (liblz4-dev) not found])])
AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR([pthread library not found])])

#Check for cpp header only libs
PKG_CHECK_MODULES([TCLAP], [tclap], [], [AC_MSG_ERROR([The TCLAP flag parsing library (libtclap-dev) is not found])])

# configure options.
AC_ARG_WITH([yaml], [AS_HELP_STRING([--with-yaml], [Enable YAML support (default is no)])], [WITH_YAML=$withval], [WITH_YAML=no])
AC_ARG_ENABLE([static-binaries],
   AS_HELP_STRING([--enable-static-binaries], [Build completely static binaries]))

has_compiler_strip_flag=no
AX_CHECK_COMPILE_FLAG([-s],
    [has_compiler_strip_flag=yes])

AC_ARG_ENABLE([strip],
   AS_HELP_STRING([--disable-strip], [Disable the stripping of symbols from binaries]))
test no = "$enable_strip" || enable_strip=yes

if test "x$enable_strip" = "xyes"; then
  if test "x$has_compiler_strip_flag" = "xyes"; then
    CXXFLAGS="$CXXFLAGS -s"
  fi
else
  # Disable strip binary
  AC_SUBST(STRIP,/bin/true)
fi


# Check for optional libraries
AS_IF([test "x$WITH_YAML" = "xyes"],
  [PKG_CHECK_MODULES([YAML_CPP], [yaml-cpp],
        [AC_SUBST(HAVE_LIBYAML_CPP, [AFF4_HAS_LIBYAML_CPP])],
        [AC_MSG_ERROR([libyaml-cpp library (libyamp-cpp-dev) not found])])]
)

# Setup doxygen if present.
AC_CHECK_PROGS([DOXYGEN], [doxygen], [])
if test -z "$DOXYGEN";
   then AC_MSG_WARN([Doxygen not found - continuing without Doxygen support])
fi

# Check for windows.
MINGW_AC_WIN32_NATIVE_HOST
URIPARSER_CHECK
AC_CANONICAL_HOST

build_linux=no
build_windows=no
build_mac=no

# Detect the target system
case "${host_os}" in
 linux*)
   build_linux=yes
 ;;
 cygwin*|mingw*)
   build_windows=yes
 ;;
 darwin*)
   build_mac=yes
 ;;
 *)
   AC_MSG_ERROR(["OS $host_os is not supported"])
 ;;
esac

# Pass the conditionals to automake
AM_CONDITIONAL([LINUX], [test "$build_linux" = "yes"])
AM_CONDITIONAL([WINDOWS], [test "$build_windows" = "yes"])
AM_CONDITIONAL([OSX], [test "$build_mac" = "yes"])

# Set Makefile / config.h values.
AM_CONDITIONAL([HAVE_LIBYAML_CPP], [test "x$WITH_YAML" = "xyes"])
if test "x$WITH_YAML" = "xyes"; then
        AC_DEFINE([AFF4_HAS_LIBYAML_CPP], [1], ["Enable Yaml Support"])
else
        AC_MSG_NOTICE([yaml-cpp disabled])
fi
AM_CONDITIONAL([GCC], test "$GCC" = yes)   # let the Makefile know if we're gcc
AM_CONDITIONAL([STATIC_BUILD], [test x$enable_static_binaries = xyes])
AM_CONDITIONAL([WIN32_NATIVE_HOST], [test x$mingw_cv_win32_host = xyes])

# UUID support is not required on windows but mandatory on unix.
if test "x$mingw_cv_win32_host" = "xno"; then
   PKG_CHECK_MODULES(
        [UUID], [uuid], [AC_DEFINE([HAVE_LIBUUID], [1], ["Have UUID"])],
        [AC_MSG_ERROR([uuid library (uuid-dev) not found])])
fi


AC_SUBST(LIBTOOL_DEPS)
AC_SUBST(SO_VERSION)

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
AM_COND_IF([HAVE_DOXYGEN],
        [AC_CONFIG_FILES([ docs/Doxyfile ])])

AC_CONFIG_FILES([
        Makefile
        aff4/Makefile
        tests/Makefile
        tools/pmem/Makefile
        ])
AC_OUTPUT
