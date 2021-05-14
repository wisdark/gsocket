#! /bin/sh
#  ^^^^^^^Mutli OS must use /bin/sh (alpine uses ash, debian uses dash)

# This script is executed inside a docker container.
# It is used to build gs-netcat as staticly linked binary for various OSes.

test -d /gsocket-src || { echo >&2 "/gsocket-src does not exists."; exit 255; }
test -d /gsocket-build || { echo >&2 "/gsocket-build does not exists."; exit 255; }

cd /gsocket-src && \
./configure --prefix=/root/usr --enable-static && \
make clean all
strip tools/gs-netcat
# Test execute the binary
tools/gs-netcat -g || { rm -f tools/gs-netcat; exit 255; }

