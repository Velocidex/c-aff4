
## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = subdir-objects

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir)/src

# On windows we need to specify this to avoid a bug in gcc
# (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52991)
if WINDOWS
WINCXXFLAGS = -mno-ms-bitfields
# On windows we want to compile libpthread statically.
PTHREAD_FLAGS = -Wl,-Bstatic,--whole-archive,-lpthread -Wl,--no-whole-archive
else
UNIXLDFLAGS = ${UUID_CFLAGS}
endif


# Default flags for targets without specific overrides.
AM_CXXFLAGS = $(WINCXXFLAGS)

# Support static builds.
if STATIC_BUILD
STATIC_LIBLDFLAGS = -static -static-libstdc++ -static-libgcc ${PTHREAD_FLAGS}
STATIC_LINKERLDFLAGS = -static -static-libstdc++ -static-libgcc ${PTHREAD_FLAGS}
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
	aff4_base.h \
	aff4_image.h \
	aff4_io.h\
    aff4_simple.h \
    config.h \
    lexicon.h \
    lexicon.inc \
    rdf.h\
	aff4_errors.h \
	aff4_init.h \
	aff4_registry.h \
	aff4_utils.h \
	data_store.h \
	libaff4.h \
	zip.h \
	aff4_map.h \
	tclap_parsers.h \
	aff4_directory.h \
	aff4_imager_utils.h \
	aff4_file.h \
	aff4_symstream.h \
	libaff4-c.h

libaff4_la_SOURCES = \
	aff4_image.cc \
	aff4_init.cc \
	data_store.cc \
	libaff4.cc \
	rdf.cc \
	zip.cc \
	aff4_imager_utils.cc \
	aff4_map.cc \
	lexicon.cc \
	aff4_directory.cc \
	aff4_file.cc \
	aff4_symstream.cc \
	libaff4-c.cc \
	tclap_parsers.cc

libaff4_la_LDFLAGS = $(STATIC_LIBLDFLAGS)
libaff4_la_INCLUDES = ${RAPTOR2_CFLAGS} ${UUID_CFLAGS} ${TCLAP_CFLAGS} ${YAML_CPP_CFLAGS} ${ZLIB_CFLAGS} ${UUID_CFLAGS}

if WINDOWS
libaff4_la_LIBADD = -lrpcrt4 -lshlwapi ${LIBS} ${YAML_CPP_LIBS} ${ZLIB_LIBS} ${RAPTOR2_LIBS} ${TCLAP_LIBS}
else
libaff4_la_LIBADD =  ${LIBS} ${YAML_CPP_LIBS} ${ZLIB_LIBS} ${RAPTOR2_LIBS} ${TCLAP_LIBS} ${UUID_LIBS}
endif

bin_PROGRAMS = aff4imager
aff4imager_LDADD = libaff4.la ${LIBS} ${YAML_CPP_LIBS} ${ZLIB_LIBS} ${RAPTOR2_LIBS} ${TCLAP_LIBS} ${UUID_LIBS}
aff4imager_INCLUDES = ${RAPTOR2_CFLAGS} ${UUID_CFLAGS} ${TCLAP_CFLAGS} ${YAML_CPP_CFLAGS} ${ZLIB_CFLAGS}
aff4imager_LDFLAGS = $(STATIC_LINKERLDFLAGS)

# Linux backwards compatibility shims for static builds.
if LINUX
   aff4imager_SOURCES = aff4imager.cc compatibility.cc
else
   aff4imager_SOURCES = aff4imager.cc
endif
