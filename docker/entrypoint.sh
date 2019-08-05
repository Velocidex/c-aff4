#!/bin/bash

export EXTRA_CXXFLAGS=
export EXTRA_LDFLAGS=
export EXTRA_CONFIG_FLAGS=

function do_configure() {
    ./autogen.sh
    ./configure --prefix="${AFF4_BUILD_OUTPUT_PATH}" --disable-shared --enable-static --enable-static-binaries \
        LDFLAGS="${LDFLAGS} ${EXTRA_LDFLAGS}" \
        CXXFLAGS="${CXXFLAGS} ${EXTRA_CXXFLAGS} -static" \
        ${EXTRA_CONFIG_FLAGS} \
        ${AUTOCONF_HOSTFLAG}
    make clean
}

function do_build() {
    make -j4 install
}


function do_clean() {
    make -j4 clean
}

function do_help() {
    echo Valid commands are 'configure', 'configure-debug', 'build', and 'help'.
    echo If no command is specified, 'configure' and then 'build' will be run.
    echo Any other commands will be executed by the shell.
    exit 0
}

function die() {
    echo "ERROR: $@"
    exit 1
}

[ -z "$AFF4_BUILD_OUTPUT_PATH" ] && die "Environment variable AFF4_BUILD_OUTPUT_PATH not defined!"

if [ "$#" -eq 0 ]; then
    do_configure && do_build
    exit 0
elif [ "$#" -eq 1 ]; then
    if [ "$1" == "configure" ]; then
        # Disable debugging symbols completely
        export EXTRA_CXXFLAGS="-g0 -O2"
        export EXTRA_LDFLAGS="-g0"
        do_configure
        exit 0
    elif [ "$1" == "configure-debug" ]; then
        export EXTRA_CXXFLAGS="-ggdb3 -O0"
        export EXTRA_LDFLAGS="-ggdb3"
        export EXTRA_CONFIG_FLAGS="--disable-strip"
        do_configure
        exit 0
    elif [ "$1" == "build" ]; then
        do_build
        exit 0
    elif [ "$1" == "clean" ]; then
        do_clean
        exit 0
    elif [ "$1" == "help" ]; then
        do_help
        exit 0
    fi
fi

# default to executing the arguments as a command.
exec "$@"
