#!/bin/sh
# Runs what .github/workflows/build.yml runs before it builds the RPMs, in the
# same order and with the same commands. Run it before every push: a
# translation file that is out of date (any new qsTr()/tr() string) fails CI
# exactly like a failing test, and nothing else catches it.
#
# Needs g++, python3 and Qt's lupdate/lrelease (qttools5-dev-tools). A stale
# translation file is rewritten, which is the fix, and shows up in `git status`.
set -e
cd "$(dirname "$0")/.."
export PATH=/usr/lib/x86_64-linux-gnu/qt5/bin:$PATH

echo "== Qt-free test suites"
tests/run.sh >/dev/null && echo ok

echo "== QML"
python3 tools/check_qml.py

echo "== translations"
# -locations none: line numbers would make every edit to a file with a
# string in it look like a stale catalogue.
lupdate -locations none -no-obsolete src qml \
    -ts translations/harbour-onloc.ts translations/harbour-onloc-fi.ts >/dev/null
if ! git diff --exit-code -- translations >/dev/null; then
    echo "translations/ was out of date and has been regenerated: translate and commit it." >&2
    git diff --stat -- translations >&2
    exit 1
fi
lrelease translations/harbour-onloc-fi.ts -qm /tmp/onloc-fi.qm | grep "0 unfinished"
echo "all clear"
