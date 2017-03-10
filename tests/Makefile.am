ACLOCAL_AMFLAGS = -I m4

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src \
  -I$(top_srcdir)/tests \
  -I$(top_srcdir)/third_party/gtest/ -I$(top_srcdir)/third_party/gtest/include

# These are good warnings to turn on by default
if GCC
AM_CXXFLAGS = -O0 -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare
endif

# The -no-undefined flag allows libtool to generate shared libraries for
# Cygwin and MinGW.  LIBSTDCXX_LA_LINKER_FLAG is used to fix a Solaris bug.
# -version-info tells libtool what to put after the '.so' in the filename.
AM_LDFLAGS = -no-undefined $(LIBSTDCXX_LA_LINKER_FLAG) \
             -version-info @SO_VERSION@

# Support static builds.
if STATIC_BUILD
STATIC_LIBLDFLAGS = -static -static-libstdc++ -static-libgcc
STATIC_LINKERLDFLAGS = -all-static
endif

## unittests to run when people type 'make check'.
TESTS = aff4-test
TESTS_ENVIRONMENT =

check_SCRIPTS =

# Every time you add a unittest to check_SCRIPTS, add it here too
noinst_SCRIPTS =

# Libraries that are built but not installed.
noinst_LTLIBRARIES = libgtest.la

libgtest_la_SOURCES = $(top_srcdir)/third_party/gtest/src/gtest-all.cc
libgtest_la_CXXFLAGS = -g -Wall -O0 -pthread
libgtest_la_LIBADD = -lpthread

bin_PROGRAMS = aff4-test

aff4_test_SOURCES =  \
	aff4tests.cc  data_store_test.cc stream_test.cc zip_test.cc	\
	aff4_image_test.cc rdf_tests.cc aff4_map_tests.cc directory_test.cc

aff4_test_LDADD = libgtest.la ../src/libaff4.la
aff4_test_CXXFLAGS = -std=c++11 -g -Wall -O0
aff4_test_LDFLAGS = $(STATIC_LINKERLDFLAGS)
