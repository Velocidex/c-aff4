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
winpmem_CXXFLAGS = -std=c++11 -g -Wall -O0
winpmem_SOURCES = pmem_imager.cc win_pmem.cc
winpmem_LDADD = -lrpcrt4 -lshlwapi -laff4 -lyaml-cpp


all-local: winpmem$(EXEEXT)
	@echo Appending pmem AFF4 drivers to binary.
	$(STRIP) winpmem$(EXEEXT)
	cat resources/pmem.aff4 >> winpmem$(EXEEXT)
endif

if LINUX
bin_PROGRAMS = linpmem
linpmem_LDFLAGS = $(STATIC_LINKERLDFLAGS)
linpmem_CXXFLAGS = -std=c++11 -g -Wall -O0
linpmem_SOURCES = pmem_imager.cc linux_pmem.cc ../../aff4/compatibility.cc
linpmem_LDADD = $(top_srcdir)/aff4/libaff4.la
all-local: linpmem
endif

if OSX
bin_PROGRAMS = osxpmem

osxpmem_LDFLAGS = $(STATIC_LINKERLDFLAGS)
osxpmem_CXXFLAGS = -std=c++11 -g -Wall -O0
osxpmem_SOURCES = pmem_imager.cc osxpmem.cc
osxpmem_LDADD = -laff4 -lyaml-cpp

all-local: osxpmem
	@echo Creating deployable bundle.
	rm -rf osxpmem.app osxpmem.zip
	dylibbundler -x ./osxpmem -b -d osxpmem.app/libs/ -p @executable_path/libs/ -of -od -cd
	cp README.md osxpmem osxpmem.app/
	rm -f osxpmem
	cp -a resources/MacPmem.kext osxpmem.app/
	chown -R root:wheel osxpmem.app/
	chmod -R o-wx osxpmem.app/
	zip -r osxpmem.zip osxpmem.app/
endif
