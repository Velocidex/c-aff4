## Process this file with automake to produce Makefile.in

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I m4

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/third_party/include -DHAVE_SSTREAM \
	-D_FILE_OFFSET_BITS=64

# These are good warnings to turn on by default
if GCC
AFF4CXXFLAGS = -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare \
	-std=c++11
endif

# On windows we need to specify this to avoid a bug in gcc
# (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52991)
if WIN32_NATIVE_HOST
WINCXXFLAGS = -mno-ms-bitfields
else
UNIXLDFLAGS = -luuid
endif

# Default flags for targets without specific overrides.
AM_CXXFLAGS = $(AFF4CXXFLAGS) $(WINCXXFLAGS)


# Support static builds.
if STATIC_BUILD
STATIC_LIBLDFLAGS = -static -static-libstdc++ -static-libgcc
STATIC_LINKERLDFLAGS = -all-static
endif

LIBS = -lz -lraptor2 -lpcre++ -lglog -lsnappy \
	-luriparser $(UNIXLDFLAGS) -lpthread

if HAVE_LIBYAML_CPP
LIBS_LIBYAML_CPP = -lyaml-cpp
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
	aff4_base.h    aff4_config.h aff4_image.h  aff4_io.h\
        aff4_simple.h  config.h      lexicon.h  rdf.h\
	aff4_errors.h  aff4_init.h   aff4_registry.h \
	aff4_utils.h   data_store.h  libaff4.h  zip.h \
	aff4_map.h tclap_parsers.h aff4_directory.h \
        aff4_imager_utils.h aff4_file.h

libaff4_la_SOURCES = \
	aff4_image.cc aff4_init.cc data_store.cc libaff4.cc \
	rdf.cc zip.cc aff4_imager_utils.cc aff4_map.cc lexicon.cc \
	aff4_directory.cc aff4_file.cc

libaff4_la_LDFLAGS = $(STATIC_LIBLDFLAGS)

if WIN32_NATIVE_HOST
libaff4_la_LIBADD = -lrpcrt4 -lshlwapi $(LIBS) $(LIBS_LIBYAML_CPP)
else
libaff4_la_LIBADD =  $(LIBS) $(LIBS_LIBYAML_CPP)
endif


bin_PROGRAMS = aff4imager
aff4imager_LDADD = libaff4.la $(LIBS) $(LIBS_LIBYAML_CPP)

aff4imager_LDFLAGS = $(STATIC_LINKERLDFLAGS)
aff4imager_SOURCES = aff4imager.cc
