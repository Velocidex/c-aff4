## Process this file with automake to produce Makefile.in

# Make sure that when we re-make ./configure, we get the macros we need
ACLOCAL_AMFLAGS = -I m4

# This is so we can #include <package/foo>
AM_CPPFLAGS = -I$(top_srcdir) -DHAVE_SSTREAM -I$(top_srcdir)/../osx/

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

if WINDOWS
bin_PROGRAMS = winpmem

winpmem_LDFLAGS = $(STATIC_LINKERLDFLAGS)
winpmem_CXXFLAGS = -std=c++11 -Wall
winpmem_SOURCES = pmem_imager.cc win_pmem.cc resources/winpmem/resources.cc
winpmem_LDADD = -lrpcrt4 -lshlwapi $(top_srcdir)/aff4/libaff4.la -lyaml-cpp
endif

if LINUX
bin_PROGRAMS = linpmem
linpmem_LDFLAGS = $(STATIC_LINKERLDFLAGS)
linpmem_CXXFLAGS = -std=c++11 -Wall
linpmem_SOURCES = pmem_imager.cc linux_pmem.cc
linpmem_LDADD = $(top_srcdir)/aff4/libaff4.la
all-local: linpmem
endif

if OSX
bin_PROGRAMS = osxpmem

osxpmem_LDFLAGS = $(STATIC_LINKERLDFLAGS)
osxpmem_CXXFLAGS = -std=c++11 -Wall
osxpmem_SOURCES = pmem_imager.cc osxpmem.cc
osxpmem_LDADD = $(top_srcdir)/aff4/libaff4.la -lyaml-cpp

# In order to build a deployable bundle we must have all the libraries
# installed on the system first.
install-exec-hook:
	@echo Creating deployable bundle.
	rm -rf osxpmem.app osxpmem.zip
	mkdir osxpmem.app/
	cp README.md $(bindir)/osxpmem osxpmem.app/  || echo You must run make install before building a bundle.
	dylibbundler -x osxpmem.app/osxpmem -b -d osxpmem.app/libs/ -p @executable_path/libs/ -of -od -cd
	cp -a resources/MacPmem.kext osxpmem.app/
	chmod -R o-wx osxpmem.app/
	zip -r osxpmem.zip osxpmem.app/

endif
