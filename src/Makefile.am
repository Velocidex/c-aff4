## Process this file with automake to produce Makefile.in

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I m4

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src

# These are good warnings to turn on by default
if GCC
AM_CXXFLAGS = -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare
endif

# The -no-undefined flag allows libtool to generate shared libraries for
# Cygwin and MinGW.  LIBSTDCXX_LA_LINKER_FLAG is used to fix a Solaris bug.
# -version-info tells libtool what to put after the '.so' in the filename.
AM_LDFLAGS = -no-undefined $(LIBSTDCXX_LA_LINKER_FLAG) \
             -version-info @SO_VERSION@

# The libraries (.so's) to build and install.
lib_LTLIBRARIES = libaff4.la

libaff4_includedir = $(includedir)/aff4/
libaff4_include_HEADERS = \
	aff4_base.h    aff4_image.h  aff4_io.h\
        aff4_simple.h  config.h      lexicon.h  rdf.h\
	aff4_errors.h  aff4_init.h   aff4_registry.h \
	aff4_utils.h   data_store.h  libaff4.h  zip.h

libaff4_la_CXXFLAGS = -std=c++11 -g -Wall -O0
libaff4_la_SOURCES = \
	aff4_image.cc  aff4_init.cc  data_store.cc  libaff4.cc \
	 rdf.cc  stream_test.cc  zip.cc

libaff4_la_LIBADD =  -lz -lyaml-cpp -luuid -lraptor2 -lpcre++ -lglog

bin_PROGRAMS = stream_test
stream_test_LDADD = libaff4.la
stream_test_CXXFLAGS = -std=c++11 -g -Wall -O0
stream_test_SOURCES = stream_test.cc
