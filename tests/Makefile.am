AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir)

# The -no-undefined flag allows libtool to generate shared libraries for
# Cygwin and MinGW.  LIBSTDCXX_LA_LINKER_FLAG is used to fix a Solaris bug.
# -version-info tells libtool what to put after the '.so' in the filename.
AM_LDFLAGS = -no-undefined $(LIBSTDCXX_LA_LINKER_FLAG) \
             -version-info @SO_VERSION@

# Support static builds.
if STATIC_BUILD
STATIC_LIBLDFLAGS = -static -static-libstdc++ -static-libgcc
STATIC_LINKERLDFLAGS = -static  -static-libstdc++ -static-libgcc
endif

## unittests to run when people type 'make check'.
check_PROGRAMS = aff4-test
TESTS = $(check_PROGRAMS)
TESTS_ENVIRONMENT =

check_SCRIPTS =

# Every time you add a unittest to check_SCRIPTS, add it here too
noinst_SCRIPTS =

aff4_test_SOURCES =  \
	aff4tests.cc  \
	utils_test.cc \
	data_store_test.cc \
	stream_test.cc \
	zip_test.cc	\
	aff4_image_test.cc \
	rdf_tests.cc \
	aff4_map_tests.cc \
	directory_test.cc \
	rdfquery_test.cc \
	aff4_capi.cc

aff4_test_LDADD = $(top_srcdir)/aff4/libaff4.la ${LIBS} ${GLOG_LIBS} ${YAML_CPP_LIBS} ${ZLIB_LIBS} ${RAPTOR2_LIBS} ${TCLAP_LIBS} ${UUID_LIBS}
aff4_test_CXXFLAGS = ${RAPTOR2_CFLAGS} ${UUID_CFLAGS} ${TCLAP_CFLAGS} ${YAML_CPP_CFLAGS} ${ZLIB_CFLAGS} ${GLOG_CFLAGS}
aff4_test_LDFLAGS =  -L$(PREFIX)/lib/ $(STATIC_LINKERLDFLAGS) -lgtest
