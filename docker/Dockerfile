# PLATFORM INDEPENDENT BASE FOR ALL BUILDERS
FROM ubuntu:18.04 as aff4-builder-base

RUN sed -i '/^#\sdeb-src /s/^# *//' "/etc/apt/sources.list"

RUN apt-get update && \
        apt-get install -y --no-install-recommends \
                build-essential \
                libtool-bin \
                dpkg-dev \
                pkgconf \
                curl \
                autoconf \
                automake \
                cmake \
                ccache \
                vim \
                less \
                ca-certificates


ENV AFF4_DEPS_PATH /aff4-deps
ENV AFF4_DEPS_SRC_PATH /aff4-deps-src
RUN mkdir ${AFF4_DEPS_PATH}
RUN mkdir ${AFF4_DEPS_SRC_PATH}

WORKDIR ${AFF4_DEPS_SRC_PATH}
ADD Makefile-deps Makefile

# Fetch all dependencies.
RUN make fetch-deps

# WIN32 BUILDER
FROM aff4-builder-base as aff4-builder-win32
ENV AFF4_BUILD_OUTPUT_PATH=/src/build/win32
# Install the mingw toolchain
RUN apt-get update && \
        apt-get install -y --no-install-recommends \
                g++-mingw-w64-i686

# Explicitly set the gcc/g++ compilers to the posix version
RUN update-alternatives --set i686-w64-mingw32-g++ /usr/bin/i686-w64-mingw32-g++-posix
RUN update-alternatives --set i686-w64-mingw32-gcc /usr/bin/i686-w64-mingw32-gcc-posix

# Environment variables for building
ENV CXXFLAGS "-I${AFF4_DEPS_PATH}/include -DRAPTOR_STATIC -mnop-fun-dllimport"
ENV CFLAGS "-I${AFF4_DEPS_PATH}/include -DRAPTOR_STATIC -mnop-fun-dllimport"
ENV LDFLAGS "-L${AFF4_DEPS_PATH}/lib -static -static-libstdc++"
ENV PKG_CONFIG_PATH "${AFF4_DEPS_PATH}/lib/pkgconfig/"
ENV TOOLCHAIN_PREFIX="i686-w64-mingw32-"
ENV CC "${TOOLCHAIN_PREFIX}gcc"
ENV CXX "${TOOLCHAIN_PREFIX}g++"
ENV AUTOCONF_HOSTFLAG "--host=i686-w64-mingw32"
ENV CMAKE_TOOLCHAIN_FILE ${AFF4_DEPS_SRC_PATH}/toolchain-mingw32.cmake
ADD toolchain-mingw32.cmake ${CMAKE_TOOLCHAIN_FILE}
ENV AFF4_CMAKE_FLAGS "-DCMAKE_INSTALL_PREFIX:PATH=${AFF4_DEPS_PATH}"
ENV AFF4_CMAKE_TOOLCHAIN_FLAGS "-DCMAKE_TOOLCHAIN_FILE:PATH=${CMAKE_TOOLCHAIN_FILE}"

# Now actually build and install the dependencies.
RUN make install-deps

ADD entrypoint.sh /entrypoint.sh
RUN chmod 755 /entrypoint.sh
ENTRYPOINT [ "/entrypoint.sh" ]

RUN mkdir /src
WORKDIR /src

# TODO: create linux-based builder
#FROM aff4-builder-base as aff4-builder-linux
#ENV AFF4_BUILD_OUTPUT_PATH=/src/build/linux
