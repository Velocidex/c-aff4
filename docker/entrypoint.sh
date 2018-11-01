#!/bin/bash

function do_configure() {
    ./configure --prefix="${AFF4_BUILD_OUTPUT_PATH}" --disable-shared --enable-static --enable-static-binaries \
        LDFLAGS="${LDFLAGS}" \
        CXXFLAGS="${CXXFLAGS} -g3 -static" \
        ${AUTOCONF_HOSTFLAG}
}

function do_build() {
    make -j4 install-strip
}

function do_help() {
    echo Valid commands are 'configure', 'build', and 'help'.
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
        do_configure
        exit 0
    elif [ "$1" == "build" ]; then
        do_build
        exit 0
    elif [ "$1" == "help" ]; then
        do_help
        exit 0
    fi
fi

# default to executing the arguments as a command.
exec "$@"