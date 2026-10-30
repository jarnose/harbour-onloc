#!/bin/sh
# Builds and runs the Qt-free tests with plain g++, once per language mode.
#
# Both modes on purpose. -std=c++17 (strict) is what a bare g++ gives; the
# Sailfish build uses gnu++17, where `unix` is a predefined macro for 1 and
# turns any variable of that name into a compile error far from the cause.
#
# Usage: tests/run.sh      (from anywhere)
set -e
cd "$(dirname "$0")"

run() {
    name=$1; shift
    for std in c++17 gnu++17; do
        echo "== $name ($std)"
        g++ -std=$std -Wall -Wextra -Werror -I ../src "test_$name.cpp" "$@" -o "/tmp/onloc_$name"
        "/tmp/onloc_$name"
    done
}

run mercator ../src/map/mercator.cpp
run vectortile ../src/map/vectortile.cpp
