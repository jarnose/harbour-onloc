#!/bin/sh
# Holds a release tag to the versions in both specs, the daemon's own version
# string and a CHANGELOG section.
#   tools/check_version.sh v1.0.0
set -e
cd "$(dirname "$0")/.."
tag=${1#v}
fail=0
for spec in rpm/harbour-onloc.spec daemon/onloc-daemon.spec; do
    v=$(sed -n 's/^Version:[[:space:]]*//p' "$spec")
    if [ "$tag" != "$v" ]; then
        echo "tag $1 does not match Version: $v in $spec" >&2
        fail=1
    fi
done
d=$(sed -n 's/^#define ONLOCD_VERSION "\(.*\)"/\1/p' src/daemon/daemon.cpp)
if [ "$tag" != "$d" ]; then
    echo "tag $1 does not match ONLOCD_VERSION $d in src/daemon/daemon.cpp" >&2
    fail=1
fi
grep -q "^## $tag\$" CHANGELOG.md || { echo "CHANGELOG.md has no section '## $tag'" >&2; fail=1; }
[ $fail -eq 0 ] || exit 1
echo "version $tag ok"
